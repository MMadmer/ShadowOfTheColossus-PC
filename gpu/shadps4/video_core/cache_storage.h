// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/path_util.h"
#include "common/singleton.h"
#include "common/types.h"

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace Storage {

enum class BlobType : u32 {
    ShaderMeta,
    ShaderBinary,
    PipelineKey,
    ShaderProfile,
    DriverCache, ///< sotc: VkPipelineCache data of this GPU and driver
};

class DataBase {
public:
    static DataBase& Instance() {
        return *Common::Singleton<DataBase>::Instance();
    }

    void Open();
    void Close();
    void Clear();
    [[nodiscard]] bool IsOpened() const {
        return opened;
    }
    void FinishPreload();

    bool Save(BlobType type, const std::string& name, std::vector<u8>&& data);
    bool Save(BlobType type, const std::string& name, std::vector<u32>&& data);

    void Load(BlobType type, const std::string& name, std::vector<u8>& data);
    void Load(BlobType type, const std::string& name, std::vector<u32>& data);

    /// sotc: whether a blob of this name is on disk (or queued for writing), from an in-memory
    /// index: no file system access, so new shaders need no disk lookups.
    [[nodiscard]] bool Exists(BlobType type, const std::string& name);

    void ForEachBlob(BlobType type, const std::function<void(std::vector<u8>&& data)>& func);
    /// sotc: how many blobs of this type ForEachBlob would visit (for the warm-up progress).
    [[nodiscard]] size_t CountBlobs(BlobType type);

private:
    void Prefetch();
    /// sotc: fills an empty or stale cache from the shader pack shipped with the port.
    void SeedFromPack();
    void IndexAdd(std::string&& file_name);
    bool TakePrefetched(const std::filesystem::path& path, std::vector<u8>& data);

    std::jthread io_worker{};
    std::filesystem::path cache_path{};
    bool opened{};
    /// sotc: every loose cache file read up front (in parallel) for the boot preload, by file
    /// name; ordered like an NTFS directory listing. Emptied by FinishPreload.
    std::map<std::string, std::vector<u8>> prefetched{};
    bool prefetch_active{};
    std::mutex index_mutex{};
    std::unordered_set<std::string> on_disk{}; ///< sotc: file names of the loose cache blobs
};

} // namespace Storage
