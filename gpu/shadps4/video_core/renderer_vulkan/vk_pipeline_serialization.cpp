// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "sotc_progress.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <optional>
#include <thread>
#include <mutex>
#include <unordered_set>
#include "common/serdes.h"
#include "common/thread.h"
#include "core/emulator_settings.h"
#include "shader_recompiler/frontend/fetch_shader.h"
#include "shader_recompiler/info.h"
#include "video_core/cache_storage.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Serialization {
/* You should increment versions below once corresponding serialization scheme is changed. */
// Above both lines before the upstream merge: generation changed, so old caches are dropped.
static constexpr u32 ShaderBinaryVersion = 34u; // sotc: 33 = barycentrics through a geometry shader, 34 = layout 2
static constexpr u32 ShaderMetaVersion = 20u;
static constexpr u32 PipelineKeyVersion = 3u;
} // namespace Serialization

namespace Vulkan {

namespace {
// sotc: where the boot-time preload spends its time (logged with "Preloaded N pipelines").
struct WarmUpTimes {
    std::chrono::steady_clock::duration load{}, modules{}, pipelines{};
} warmup_times;

struct WarmUpTimer {
    std::chrono::steady_clock::duration& total;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ~WarmUpTimer() {
        total += std::chrono::steady_clock::now() - start;
    }
};

long long Ms(std::chrono::steady_clock::duration d) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

// sotc: a shader permutation is stored under its index in the program ({pgm}_{idx}.spv, and a
// meta named after a hash of the same pair). The index is the order in which this session met the
// permutations, so a session that did not preload all of them can give an index already used on
// disk by another permutation. Writing over it paired the pipelines saved earlier with the wrong
// shader: the driver refused them at the next start (ErrorUnknown on AMD). Such a permutation is
// now left out of the cache, along with every pipeline that uses it; they compile each session.
std::mutex collided_mutex;
std::unordered_set<u64> collided_bins;      // BinaryId of the permutations left out
std::unordered_set<size_t> collided_perms; // perm hashes, as in the pipeline keys

u64 BinaryId(u64 pgm_hash, size_t perm_idx) {
    return pgm_hash ^ (u64(perm_idx) << 56);
}

bool IsCollidedPerm(size_t perm_hash) {
    std::scoped_lock lock{collided_mutex};
    return collided_perms.contains(perm_hash);
}
} // namespace

void RegisterPipelineData(const ComputePipelineKey& key,
                          ComputePipeline::SerializationSupport& sdata) {
    if (!Storage::DataBase::Instance().IsOpened() || IsCollidedPerm(key.value)) {
        return;
    }

    Serialization::Archive ar{};
    Serialization::Writer pldata{ar};

    pldata.Write(Serialization::PipelineKeyVersion);
    pldata.Write(u32{1}); // compute

    key.Serialize(ar);
    sdata.Serialize(ar);

    Storage::DataBase::Instance().Save(Storage::BlobType::PipelineKey,
                                       fmt::format("c_{:#018x}", key.value), ar.TakeOff());
}

void RegisterPipelineData(const GraphicsPipelineKey& key, u64 hash,
                          GraphicsPipeline::SerializationSupport& sdata) {
    if (!Storage::DataBase::Instance().IsOpened()) {
        return;
    }
    for (const auto stage_hash : key.stage_hashes) {
        if (stage_hash && IsCollidedPerm(stage_hash)) {
            return;
        }
    }

    Serialization::Archive ar{};
    Serialization::Writer pldata{ar};

    pldata.Write(Serialization::PipelineKeyVersion);
    pldata.Write(u32{0}); // graphics

    key.Serialize(ar);
    sdata.Serialize(ar);

    Storage::DataBase::Instance().Save(Storage::BlobType::PipelineKey,
                                       fmt::format("g_{:#018x}", hash), ar.TakeOff());
}

void RegisterShaderMeta(const Shader::Info& info,
                        const std::optional<Shader::Gcn::FetchShaderData>& fetch_shader_data,
                        const Shader::StageSpecialization& spec, size_t perm_hash,
                        size_t perm_idx) {
    if (!Storage::DataBase::Instance().IsOpened()) {
        return;
    }
    {
        std::scoped_lock lock{collided_mutex};
        if (collided_bins.contains(BinaryId(info.pgm_hash, perm_idx))) {
            collided_perms.insert(perm_hash);
            return;
        }
    }

    Serialization::Archive ar;
    Serialization::Writer meta{ar};

    meta.Write(Serialization::ShaderMetaVersion);
    meta.Write(Serialization::ShaderBinaryVersion);

    meta.Write(perm_hash);
    meta.Write(perm_idx);

    spec.Serialize(ar);
    info.Serialize(ar);

    Storage::DataBase::Instance().Save(Storage::BlobType::ShaderMeta,
                                       fmt::format("{:#018x}", perm_hash), ar.TakeOff());
}

void RegisterShaderBinary(std::vector<u32>&& spv, u64 pgm_hash, size_t perm_idx) {
    if (!Storage::DataBase::Instance().IsOpened()) {
        return;
    }

    const auto name = fmt::format("{:#018x}_{}", pgm_hash, perm_idx);
    // sotc: only a name already on disk needs the comparison (an in-memory index answers that;
    // the disk lookup of every new shader cost ~1 ms each, 220 ms in some 5 s windows).
    if (!EmulatorSettings.IsPipelineCacheArchived() &&
        Storage::DataBase::Instance().Exists(Storage::BlobType::ShaderBinary, name)) {
        std::vector<u32> stored;
        Storage::DataBase::Instance().Load(Storage::BlobType::ShaderBinary, name, stored);
        if (!stored.empty() && stored != spv) {
            static std::atomic<u32> reported{0};
            if (reported.fetch_add(1, std::memory_order_relaxed) < 8) {
                LOG_WARNING(Render_Vulkan,
                            "Shader permutation {} is another one on disk: not cached", name);
            }
            std::scoped_lock lock{collided_mutex};
            collided_bins.insert(BinaryId(pgm_hash, perm_idx));
            return;
        }
    }
    Storage::DataBase::Instance().Save(Storage::BlobType::ShaderBinary, name, std::move(spv));
}

bool LoadShaderMeta(Serialization::Archive& ar, Shader::Info& info,
                    Shader::StageSpecialization& spec, size_t& perm_idx) {
    Serialization::Reader meta{ar};

    u32 meta_version{};
    meta.Read(meta_version);
    if (meta_version != Serialization::ShaderMetaVersion) {
        return false;
    }

    u32 binary_version{};
    meta.Read(binary_version);
    if (binary_version != Serialization::ShaderBinaryVersion) {
        return false;
    }

    u64 perm_hash_ar{};
    meta.Read(perm_hash_ar);
    meta.Read(perm_idx);

    spec.Deserialize(ar);
    return info.Deserialize(ar);
}

void ComputePipelineKey::Serialize(Serialization::Archive& ar) const {
    Serialization::Writer key{ar};
    key.Write(value);
}

bool ComputePipelineKey::Deserialize(Serialization::Archive& ar) {
    Serialization::Reader key{ar};
    key.Read(value);
    return true;
}

void ComputePipeline::SerializationSupport::Serialize(Serialization::Archive& ar) const {
    // Nothing here yet
    return;
}

bool ComputePipeline::SerializationSupport::Deserialize(Serialization::Archive& ar) {
    // Nothing here yet
    return true;
}

bool PipelineCache::LoadComputePipeline(Serialization::Archive& ar) {
    // Stages left over from a pipeline whose load failed half-way must not leak into this one:
    // a stale entry for a stage this key does not use would be bound without user data.
    infos.fill(nullptr);
    modules.fill(nullptr);
    fetch_shader = nullptr;
    compute_key.Deserialize(ar);

    ComputePipeline::SerializationSupport sdata{};
    sdata.Deserialize(ar);

    std::vector<u8> meta_blob;
    Storage::DataBase::Instance().Load(Storage::BlobType::ShaderMeta,
                                       fmt::format("{:#018x}", compute_key.value), meta_blob);
    if (meta_blob.empty()) {
        return false;
    }

    Serialization::Archive meta_ar{std::move(meta_blob)};

    if (!LoadPipelineStage(meta_ar, 0)) {
        return false;
    }

    const auto [it, is_new] = compute_pipelines.try_emplace(compute_key);
    ASSERT(is_new);

    if (preload_jobs) {
        // sotc: built on a warm-up worker (see WarmUp).
        struct State {
            ComputePipelineKey key;
            const Shader::Info* info;
            vk::ShaderModule module;
            ComputePipeline::SerializationSupport sdata;
            std::unique_ptr<ComputePipeline> pipeline;
        };
        auto state = std::make_shared<State>(State{compute_key, infos[0], modules[0], sdata, {}});
        preload_jobs->push_back({
            [this, state] {
                state->pipeline = std::make_unique<ComputePipeline>(
                    instance, scheduler, desc_heap, profile, *pipeline_cache, state->key,
                    *state->info, state->module, state->sdata, true);
            },
            [this, state] {
                if (!state->pipeline || !state->pipeline->Handle()) {
                    compute_pipelines.erase(state->key);
                    return false;
                }
                compute_pipelines[state->key] = std::move(state->pipeline);
                return true;
            },
        });
        infos.fill(nullptr);
        modules.fill(nullptr);
        return true;
    }

    {
        WarmUpTimer timer{warmup_times.pipelines};
        it.value() = std::make_unique<ComputePipeline>(instance, scheduler, desc_heap, profile,
                                                       *pipeline_cache, compute_key, *infos[0],
                                                       modules[0], sdata, true);
    }

    infos.fill(nullptr);
    modules.fill(nullptr);

    if (!it.value()->Handle()) {
        compute_pipelines.erase(compute_key);
        return false;
    }
    return true;
}

void GraphicsPipelineKey::Serialize(Serialization::Archive& ar) const {
    Serialization::Writer key{ar};

    key.Write(this, sizeof(*this));
}

bool GraphicsPipelineKey::Deserialize(Serialization::Archive& ar) {
    Serialization::Reader key{ar};

    key.Read(this, sizeof(*this));
    return true;
}

void GraphicsPipeline::SerializationSupport::Serialize(Serialization::Archive& ar) const {
    Serialization::Writer sdata{ar};

    sdata.Write(&vertex_attributes, sizeof(vertex_attributes));
    sdata.Write(&vertex_bindings, sizeof(vertex_bindings));
    sdata.Write(&divisors, sizeof(divisors));
    sdata.Write(multisampling);
    sdata.Write(tcs);
    sdata.Write(tes);
}

bool GraphicsPipeline::SerializationSupport::Deserialize(Serialization::Archive& ar) {
    Serialization::Reader sdata{ar};

    sdata.Read(&vertex_attributes, sizeof(vertex_attributes));
    sdata.Read(&vertex_bindings, sizeof(vertex_bindings));
    sdata.Read(&divisors, sizeof(divisors));
    sdata.Read(multisampling);
    sdata.Read(tcs);
    sdata.Read(tes);
    return true;
}

bool PipelineCache::LoadGraphicsPipeline(Serialization::Archive& ar) {
    // Stages left over from a pipeline whose load failed half-way must not leak into this one:
    // a stale entry for a stage this key does not use would be bound without user data.
    infos.fill(nullptr);
    modules.fill(nullptr);
    fetch_shader = nullptr;
    graphics_key.Deserialize(ar);

    GraphicsPipeline::SerializationSupport sdata{};
    sdata.Deserialize(ar);

    for (int stage_idx = 0; stage_idx < MaxShaderStages; ++stage_idx) {
        const auto& hash = graphics_key.stage_hashes[stage_idx];
        if (!hash) {
            continue;
        }

        std::vector<u8> meta_blob;
        Storage::DataBase::Instance().Load(Storage::BlobType::ShaderMeta,
                                           fmt::format("{:#018x}", hash), meta_blob);
        if (meta_blob.empty()) {
            return false;
        }

        Serialization::Archive meta_ar{std::move(meta_blob)};

        if (!LoadPipelineStage(meta_ar, stage_idx)) {
            return false;
        }
    }

    const auto [it, is_new] = graphics_pipelines.try_emplace(graphics_key);
    ASSERT(is_new);

    if (preload_jobs) {
        // sotc: built on a warm-up worker (see WarmUp). Copies: the fetch shader data lives in a
        // module list that later loads can grow.
        struct State {
            GraphicsPipelineKey key;
            std::array<const Shader::Info*, MaxShaderStages> infos;
            std::array<Shader::RuntimeInfo, MaxShaderStages> runtime_infos;
            std::optional<Shader::Gcn::FetchShaderData> fetch;
            std::array<vk::ShaderModule, MaxShaderStages> modules;
            GraphicsPipeline::SerializationSupport sdata;
            std::unique_ptr<GraphicsPipeline> pipeline;
            std::array<const Shader::Backend::SPIRV::ShaderIo*, MaxShaderStages> ios;
        };
        std::optional<Shader::Gcn::FetchShaderData> fetch;
        if (fetch_shader) {
            fetch = *fetch_shader;
        }
        auto state = std::make_shared<State>(State{graphics_key, infos, runtime_infos,
                                                   std::move(fetch), modules, sdata, {},
                                                   ModuleIos(modules)});
        preload_jobs->push_back({
            [this, state] {
                state->pipeline = std::make_unique<GraphicsPipeline>(
                    instance, scheduler, desc_heap, profile, state->key, *pipeline_cache,
                    state->infos, state->runtime_infos, state->fetch ? &*state->fetch : nullptr,
                    state->modules, state->sdata, true, false, &state->ios);
            },
            [this, state] {
                if (!state->pipeline || !state->pipeline->Handle()) {
                    graphics_pipelines.erase(state->key);
                    return false;
                }
                graphics_pipelines[state->key] = std::move(state->pipeline);
                return true;
            },
        });
        infos.fill(nullptr);
        modules.fill(nullptr);
        fetch_shader = nullptr;
        return true;
    }

    {
        WarmUpTimer timer{warmup_times.pipelines};
        const auto ios = ModuleIos(modules);
        it.value() = std::make_unique<GraphicsPipeline>(
            instance, scheduler, desc_heap, profile, graphics_key, *pipeline_cache, infos,
            runtime_infos, fetch_shader, modules, sdata, true, false, &ios);
    }

    infos.fill(nullptr);
    modules.fill(nullptr);
    fetch_shader = nullptr;

    if (!it.value()->Handle()) {
        graphics_pipelines.erase(graphics_key);
        return false;
    }
    return true;
}

bool PipelineCache::LoadPipelineStage(Serialization::Archive& ar, size_t stage) {
    auto program = std::make_unique<Program>();
    Shader::StageSpecialization spec{};
    spec.info = &program->info;
    size_t perm_idx{};
    if (!LoadShaderMeta(ar, program->info, spec, perm_idx)) {
        return false;
    }

    std::vector<u32> spv{};
    {
        WarmUpTimer timer{warmup_times.load};
        Storage::DataBase::Instance().Load(
            Storage::BlobType::ShaderBinary,
            fmt::format("{:#018x}_{}", program->info.pgm_hash, perm_idx), spv);
    }
    if (spv.empty()) {
        return false;
    }

    // Permutation hash depends on shader variation index. To prevent collisions, we need insert it
    // at the exact position rather than append

    vk::ShaderModule module{};

    // Shader patches (user/shader/patch) must also apply to pipelines restored from the cache.
    if (EmulatorSettings.IsPatchShaders()) {
        if (auto patch = GetShaderPatch(program->info.pgm_hash, program->info.hw_stage, perm_idx,
                                        "spv", spv)) {
            LOG_INFO(Loader, "Loaded patch for cached {} shader {:#x}", program->info.hw_stage,
                     program->info.pgm_hash);
            spv = std::move(*patch);
        }
    }

    auto [it_pgm, new_program] = program_cache.try_emplace(program->info.pgm_hash);
    if (new_program) {
        {
            WarmUpTimer timer{warmup_times.modules};
            module = CompileSPV(spv, instance.GetDevice());
        }
        NoteModuleIo(module, spv);
        it_pgm.value() = std::move(program);
    } else {
        const auto& it = std::ranges::find(it_pgm.value()->modules, spec, &Program::Module::spec);
        if (it != it_pgm.value()->modules.end()) {
            // A matching permutation is valid only at its original index. A different index means
            // the store holds entries from more than one cache generation, so this pipeline is
            // left to compile at runtime.
            const auto idx = std::distance(it_pgm.value()->modules.begin(), it);
            if (perm_idx != idx) {
                LOG_WARNING(Render_Vulkan,
                            "Cached permutation {} of {}_{:x} conflicts with index {}, skipping "
                            "preload",
                            perm_idx, program->info.hw_stage, program->info.pgm_hash, idx);
                return false;
            }
            module = it->module;
        } else {
            WarmUpTimer timer{warmup_times.modules};
            module = CompileSPV(spv, instance.GetDevice());
            NoteModuleIo(module, spv);
        }
    }
    // The specialization must reference the cached program's info, not the temporary one.
    spec.info = &it_pgm.value()->info;
    it_pgm.value()->InsertPermut(module, std::move(spec), perm_idx);

    infos[stage] = &it_pgm.value()->info;
    modules[stage] = module;
    if (auto& fetch = it_pgm.value()->modules[perm_idx].spec.fetch_shader_data; !fetch.Empty()) {
        fetch_shader = &fetch;
    }

    return true;
}

void PipelineCache::WarmUp() {
    if (!EmulatorSettings.IsPipelineCacheEnabled()) {
        return;
    }

    Storage::DataBase::Instance().Open();

    // Check if cache is compatible
    std::vector<u8> profile_data{};
    Storage::DataBase::Instance().Load(Storage::BlobType::ShaderProfile, "profile", profile_data);
    if (profile_data.empty()) {
        Storage::DataBase::Instance().FinishPreload();

        profile_data.resize(sizeof(profile));
        std::memcpy(profile_data.data(), &profile, sizeof(profile));
        Storage::DataBase::Instance().Save(Storage::BlobType::ShaderProfile, "profile",
                                           std::move(profile_data));
        return;
    }
    Shader::Profile cached_profile{};
    if (profile_data.size() == sizeof(Shader::Profile)) {
        std::memcpy(&cached_profile, profile_data.data(), sizeof(cached_profile));
    }
    if (profile_data.size() != sizeof(Shader::Profile) || cached_profile != profile) {
        // sotc: upstream closed the cache for the session here, so it was never rewritten and
        // every later session compiled every shader again (stutters in each new area; bbport
        // had fixed this). Start a fresh cache for this build and GPU instead.
        LOG_WARNING(Render, "Pipeline cache isn't compatible with current system: rebuilding it");
        Storage::DataBase::Instance().Clear();
        Storage::DataBase::Instance().FinishPreload();
        profile_data.resize(sizeof(profile));
        std::memcpy(profile_data.data(), &profile, sizeof(profile));
        Storage::DataBase::Instance().Save(Storage::BlobType::ShaderProfile, "profile",
                                           std::move(profile_data));
        return;
    }

    LoadDriverCache();

    // sotc: SOTC_PROGRESS_DEMO=<seconds> shows the loading screen with a simulated build (to
    // check how it looks when the real one is too fast to see).
    if (const char* demo = std::getenv("SOTC_PROGRESS_DEMO")) {
        const int seconds = std::max(1, std::atoi(demo));
        SotcProgress::Begin(100, SotcProgress::Stage::Reading);
        for (u32 i = 0; i <= 100; ++i) {
            SotcProgress::Update(i);
            std::this_thread::sleep_for(std::chrono::milliseconds(seconds * 2));
        }
        SotcProgress::Begin(3000, SotcProgress::Stage::Building);
        for (u32 i = 0; i <= 3000; i += 10) {
            SotcProgress::Update(i);
            std::this_thread::sleep_for(std::chrono::milliseconds(seconds * 1000 / 300));
        }
        SotcProgress::End();
    }

    u32 num_pipelines{};
    u32 num_total_pipelines{};
    const auto warmup_start = std::chrono::steady_clock::now();
    const u32 num_keys =
        static_cast<u32>(Storage::DataBase::Instance().CountBlobs(Storage::BlobType::PipelineKey));
    // sotc: a loading screen with a progress bar while the cached pipelines are built: first the
    // cache entries are read (fast), then the driver builds every pipeline on all CPU cores.
    SotcProgress::Begin(num_keys, SotcProgress::Stage::Reading);

    std::vector<PreloadJob> jobs;
    jobs.reserve(num_keys);
    preload_jobs = &jobs;
    Storage::DataBase::Instance().ForEachBlob(
        Storage::BlobType::PipelineKey, [&](std::vector<u8>&& data) {
            ++num_total_pipelines;
            try {
                Serialization::Archive ar{std::move(data)};
                Serialization::Reader pldata{ar};

                u32 version{};
                pldata.Read(version);
                if (version != Serialization::PipelineKeyVersion) {
                    return;
                }

                u32 is_compute{};
                pldata.Read(is_compute);
                if (is_compute) {
                    LoadComputePipeline(ar);
                } else {
                    LoadGraphicsPipeline(ar);
                }
            } catch (const Serialization::CorruptedData&) {
                // A truncated entry (e.g. from a killed session) is just skipped.
                infos.fill(nullptr);
                modules.fill(nullptr);
                fetch_shader = nullptr;
            }
            SotcProgress::Update(num_total_pipelines);
        });
    preload_jobs = nullptr;
    const auto read_end = std::chrono::steady_clock::now();

    // The driver compiles one pipeline at a time per thread: every core builds its share.
    SotcProgress::Begin(static_cast<u32>(jobs.size()), SotcProgress::Stage::Building);
    {
        const u32 hw = std::max(1u, std::thread::hardware_concurrency());
        u32 num_workers = std::clamp<u32>(hw, 1u, 32u);
        if (const char* v = std::getenv("SOTC_PRELOAD_THREADS")) {
            num_workers = std::clamp<u32>(static_cast<u32>(std::atoi(v)), 1u, 32u);
        }
        std::atomic<size_t> next{0};
        std::atomic<u32> done{0};
        std::vector<std::jthread> workers;
        for (u32 w = 0; w < num_workers; ++w) {
            workers.emplace_back([&] {
                Common::SetCurrentThreadName("shadPS4:PipelinePreload");
                for (size_t i; (i = next.fetch_add(1)) < jobs.size();) {
                    jobs[i].build();
                    SotcProgress::Update(done.fetch_add(1) + 1);
                }
            });
        }
    }
    for (auto& job : jobs) {
        num_pipelines += job.install() ? 1 : 0;
    }
    SotcProgress::End();
    last_graphics_pipeline = nullptr;

    LOG_WARNING(Render,
                "Preloaded {} pipelines in {} ms (cache read {} ms, SPIR-V reads {} ms, shader "
                "modules {} ms, pipeline builds {} ms on {} threads)",
                num_pipelines, Ms(std::chrono::steady_clock::now() - warmup_start),
                Ms(read_end - warmup_start), Ms(warmup_times.load), Ms(warmup_times.modules),
                Ms(std::chrono::steady_clock::now() - read_end),
                std::max(1u, std::thread::hardware_concurrency()));
    if (num_total_pipelines > num_pipelines) {
        LOG_WARNING(Render, "{} stale pipelines were found. Consider re-generating the cache",
                    num_total_pipelines - num_pipelines);
    }

    Storage::DataBase::Instance().FinishPreload();
    // sotc: what the driver compiled is kept (vkGetPipelineCacheData), so the next start builds
    // the same pipelines from it in a fraction of the time.
    if (!jobs.empty()) {
        SaveDriverCache();
    }
}

// sotc: the driver's own pipeline cache (VkPipelineCache data) is stored with the shader cache.
// NVIDIA's disk cache is limited in size and shared with every other game; this one is not.
void PipelineCache::LoadDriverCache() {
    if (pipeline_cache) {
        return;
    }
    std::vector<u8> data;
    const char* use = std::getenv("SOTC_DRIVER_CACHE");
    if (Storage::DataBase::Instance().IsOpened() && !(use && use[0] == '0')) {
        Storage::DataBase::Instance().Load(Storage::BlobType::DriverCache, "driver", data);
    }
    // The header (vendor, device, cache UUID) must match this GPU and driver, else start empty.
    const auto& props = instance.GetPhysicalDevice().getProperties();
    if (data.size() >= 16 + VK_UUID_SIZE) {
        u32 vendor{}, device{};
        std::memcpy(&vendor, data.data() + 8, 4);
        std::memcpy(&device, data.data() + 12, 4);
        if (vendor != props.vendorID || device != props.deviceID ||
            std::memcmp(data.data() + 16, props.pipelineCacheUUID.data(), VK_UUID_SIZE) != 0) {
            LOG_INFO(Render_Vulkan, "sotc: driver pipeline cache is from another GPU or driver");
            data.clear();
        }
    } else {
        data.clear();
    }
    const vk::PipelineCacheCreateInfo cache_info = {
        .initialDataSize = data.size(),
        .pInitialData = data.empty() ? nullptr : data.data(),
    };
    auto created = instance.GetDevice().createPipelineCacheUnique(cache_info);
    if (created.result != vk::Result::eSuccess && !data.empty()) {
        data.clear();
        created = instance.GetDevice().createPipelineCacheUnique({});
    }
    ASSERT_MSG(created.result == vk::Result::eSuccess, "Failed to create pipeline cache: {}",
               vk::to_string(created.result));
    pipeline_cache = std::move(created.value);
    driver_cache_saved_size = data.size();
    LOG_INFO(Render_Vulkan, "sotc: driver pipeline cache {} MB", data.size() >> 20);
}

void PipelineCache::SaveDriverCache() {
    if (!pipeline_cache || !Storage::DataBase::Instance().IsOpened()) {
        return;
    }
    auto [result, data] = instance.GetDevice().getPipelineCacheData(*pipeline_cache);
    if (result != vk::Result::eSuccess || data.size() <= driver_cache_saved_size) {
        return;
    }
    driver_cache_saved_size = data.size();
    std::vector<u8> bytes(data.begin(), data.end());
    Storage::DataBase::Instance().Save(Storage::BlobType::DriverCache, "driver", std::move(bytes));
}

void PipelineCache::Sync() {
    Storage::DataBase::Instance().Close();
}

} // namespace Vulkan

namespace Shader {

void Info::Serialize(Serialization::Archive& ar) const {
    Serialization::Writer info{ar};

    info.Write(this, sizeof(InfoPersistent));
    info.Write(FlatBuf());
    srt_info.Serialize(ar);
}

bool Info::Deserialize(Serialization::Archive& ar) {
    Serialization::Reader info{ar};

    info.Read(this, sizeof(Shader::InfoPersistent));
    info.Read(FlatBuf());

    return srt_info.Deserialize(ar);
}

void Gcn::FetchShaderData::Serialize(Serialization::Archive& ar) const {
    Serialization::Writer fetch{ar};
    ar.Grow(6 + attributes.size() * sizeof(VertexAttribute));

    fetch.Write(size);
    fetch.Write(vertex_offset_sgpr);
    fetch.Write(instance_offset_sgpr);
    fetch.Write(attributes);
    fetch.Write(one_vgprs);
}

bool Gcn::FetchShaderData::Deserialize(Serialization::Archive& ar) {
    Serialization::Reader fetch{ar};

    fetch.Read(size);
    fetch.Read(vertex_offset_sgpr);
    fetch.Read(instance_offset_sgpr);
    fetch.Read(attributes);
    fetch.Read(one_vgprs);

    return true;
}

void PersistentSrtInfo::Serialize(Serialization::Archive& ar) const {
    Serialization::Writer srt{ar};

    srt.Write(this, sizeof(*this));
    if (walker_func_size) {
        srt.Write(reinterpret_cast<void*>(walker_func), walker_func_size);
    }
}

bool PersistentSrtInfo::Deserialize(Serialization::Archive& ar) {
    Serialization::Reader srt{ar};

    srt.Read(this, sizeof(*this));

    if (walker_func_size) {
        walker_func = RegisterWalkerCode(ar.CurrPtr(), walker_func_size);
        ar.Advance(walker_func_size);
        if (!walker_func) {
            // No room for the walker: drop the cached program, it is compiled again.
            walker_func_size = 0;
            return false;
        }
    }

    return true;
}

void StageSpecialization::Serialize(Serialization::Archive& ar) const {
    Serialization::Writer spec{ar};

    spec.Write(start);
    spec.Write(runtime_info);

    spec.Write(bitset.to_string());

    if (!fetch_shader_data.Empty()) {
        spec.Write(sizeof(fetch_shader_data));
        fetch_shader_data.Serialize(ar);
    } else {
        spec.Write(size_t{0});
    }

    spec.Write(vs_attribs);
    spec.Write(buffers);
    spec.Write(images);
    spec.Write(fmasks);
    spec.Write(samplers);
}

bool StageSpecialization::Deserialize(Serialization::Archive& ar) {
    Serialization::Reader spec{ar};

    spec.Read(start);
    spec.Read(runtime_info);

    std::string bits{};
    spec.Read(bits);
    bitset = std::bitset<MaxStageResources>(bits);

    u64 fetch_data_size{};
    spec.Read(fetch_data_size);

    if (fetch_data_size) {
        Gcn::FetchShaderData fetch_data;
        fetch_data.Deserialize(ar);
        fetch_shader_data = fetch_data;
    }

    spec.Read(vs_attribs);
    spec.Read(buffers);
    spec.Read(images);
    spec.Read(fmasks);
    spec.Read(samplers);

    return true;
}

} // namespace Shader
