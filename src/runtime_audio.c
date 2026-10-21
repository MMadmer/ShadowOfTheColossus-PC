/* libSceAudioOut on SDL3 audio streams. sceAudioOutOutput returns once per
 * buffer period on a steady clock, like PS4 hardware: FMOD's output thread
 * reads 256-frame slots out of its mixer's 512-frame ring, and returning in
 * bursts (whenever the device pulls a quantum) lets it overtake the mixer and
 * play stale halves - a click every 10.7 ms. The lowest SDL queue level of
 * each window is steered to two buffers (silence prefill on underrun, ±3%
 * cadence corrections), which also absorbs device clock drift. Without an audio device (headless runs, BB_AUDIO=none)
 * ports only follow the clock. Every port type plays on the PC's default output device: the
 * pad speaker, headset (voice/personal) and aux ports as well, so sounds the PS4 sends to the
 * DualShock's speaker are heard without one. */
#define _GNU_SOURCE
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <math.h>
#include <SDL3/SDL.h>

#define PORTS 25
#define ERR_NOT_OPENED ((int32_t)0x80260001)
#define ERR_INVALID_PORT ((int32_t)0x80260003)
#define ERR_INVALID_POINTER ((int32_t)0x80260004)
#define ERR_PORT_FULL ((int32_t)0x80260005)
#define ERR_INVALID_SIZE ((int32_t)0x80260006)
#define ERR_INVALID_FORMAT ((int32_t)0x80260007)
#define ERR_INVALID_FREQ ((int32_t)0x80260008)
#define ERR_INVALID_VOLUME ((int32_t)0x80260009)
#define ERR_INVALID_TYPE ((int32_t)0x8026000A)
#define ERR_ALREADY_INIT ((int32_t)0x8026000E)
#define ERR_NOT_INIT ((int32_t)0x8026000F)
#define VOLUME_0DB 32768

typedef struct {
    int used, type, channels, is_float, frames, sample_bytes, std_layout;
    int32_t volume[8];
    SDL_AudioStream *stream;
    uint64_t open_tried_ns; /* sotc: last attempt to open the device stream (open_stream) */
    uint64_t next_deadline_ns; /* next return of sceAudioOutOutput */
    int64_t adjust_ns; int window_min, window_count; /* queue level control */
    uint64_t last_output_us;
    pthread_mutex_t lock;
    FILE *dump;                     /* BB_AUDIO_DUMP: raw converted PCM per port */
    int stats;
    uint64_t stat_start_ns, stat_last_ns, stat_max_gap_ns; /* BB_AUDIO_STATS */
    unsigned stat_starved, stat_buffers; int stat_min_queued;
} Port;
typedef struct { uint16_t output; uint8_t channel, reserved; int16_t volume; uint16_t reroute; uint64_t flag, reserved64[2]; } PortState;
_Static_assert(sizeof(PortState)==32,"AudioOut port state layout");

static pthread_mutex_t table_lock=PTHREAD_MUTEX_INITIALIZER;
static Port ports[PORTS];
static int initialized, sdl_ready=-1;
static size_t buffers_out, ports_opened;

static uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec; }
static void sleep_until(uint64_t deadline) {
#ifdef _WIN32
    /* winpthreads' clock_nanosleep only takes CLOCK_REALTIME (EINVAL otherwise). */
    for (uint64_t now=now_ns(); now<deadline; now=now_ns()) compat_sleep_ns(deadline-now);
#else
    struct timespec t={(time_t)(deadline/1000000000u),(long)(deadline%1000000000u)};
    while (clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&t,NULL)) {}
#endif
}
static int sdl_audio(void) {
    if (sdl_ready<0) {
        const char *mode=getenv("BB_AUDIO");
        sdl_ready = (!mode || strcmp(mode,"none")) && SDL_InitSubSystem(SDL_INIT_AUDIO);
        printf("Runtime: audio backend %s\n", sdl_ready ? SDL_GetCurrentAudioDriver() : "timer (silent)");
    }
    return sdl_ready;
}
/* sotc: the device stream of a port. With no device at the time (a Bluetooth headset not ready
 * yet, a device change) the port used the timer sink for the whole session: it now tries again
 * every second from the output path (#214). */
static void open_stream(Port *p) {
    SDL_AudioSpec spec={p->is_float ? SDL_AUDIO_F32 : SDL_AUDIO_S16, p->channels, 48000};
    p->open_tried_ns=now_ns();
    p->stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,NULL,NULL);
    if (p->stream) SDL_ResumeAudioStreamDevice(p->stream);
}
static int port_range(int type, int *first, int *last) {
    switch (type) {
    case 0: *first=0; *last=7; return 1;      /* main */
    case 1: *first=8; *last=8; return 1;      /* bgm */
    case 2: *first=9; *last=12; return 1;     /* voice */
    case 3: *first=13; *last=16; return 1;    /* personal */
    case 4: *first=17; *last=20; return 1;    /* pad speaker */
    case 126: *first=21; *last=22; return 1;  /* audio3d */
    case 127: *first=23; *last=24; return 1;  /* aux */
    default: return 0;
    }
}
static Port *port_of(int32_t handle, int32_t *error) {
    int id=handle & 0xff;
    if ((handle & 0x3f000000)!=0x20000000 || id>=PORTS) { *error=ERR_INVALID_PORT; return NULL; }
    if (!ports[id].used) { *error=ERR_NOT_OPENED; return NULL; }
    return &ports[id];
}

static ABI int32_t audio_init(void) {
    pthread_mutex_lock(&table_lock);
    int32_t r=initialized ? ERR_ALREADY_INIT : 0;
    initialized=1;
    pthread_mutex_unlock(&table_lock);
    return r;
}
static ABI int32_t audio_open(int32_t user, int32_t type, int32_t index, uint32_t length, uint32_t freq, uint32_t param) {
    (void)user; (void)index;
    if (!initialized) return ERR_NOT_INIT;
    if (!length || length>2048 || (length & 0xff)) return ERR_INVALID_SIZE;
    if (freq!=48000) return ERR_INVALID_FREQ;
    uint32_t format=param & 0xff;
    if (format>7) return ERR_INVALID_FORMAT;
    int first, last;
    if (!port_range(type,&first,&last)) return ERR_INVALID_TYPE;
    static const int channels[8]={1,2,8,1,2,8,8,8};
    pthread_mutex_lock(&table_lock);
    int id=-1;
    for (int i=first;i<=last;++i) if (!ports[i].used) { id=i; break; }
    if (id<0) { pthread_mutex_unlock(&table_lock); return ERR_PORT_FULL; }
    Port *p=&ports[id];
    memset(p,0,sizeof(*p));
    pthread_mutex_init(&p->lock,NULL);
    p->used=1; p->type=type; p->channels=channels[format]; p->is_float=format>=3 && format!=6;
    p->sample_bytes=p->is_float ? 4 : 2; p->frames=(int)length; p->std_layout=format>=6;
    for (int c=0;c<8;++c) p->volume[c]=VOLUME_0DB;
    if (sdl_audio()) {
        open_stream(p);
        if (!p->stream) fprintf(stderr,"Runtime: SDL audio stream failed (%s); port %d uses the timer sink and retries\n",SDL_GetError(),id);
    }
    p->stats=getenv("BB_AUDIO_STATS")!=NULL;
    const char *dump=getenv("BB_AUDIO_DUMP");
    if (dump) {
        char path[4096];
        snprintf(path,sizeof(path),"%s.port%d.%dch.%s",dump,id,p->channels,p->is_float ? "f32" : "s16");
        p->dump=fopen(path,"wb");
    }
    ++ports_opened;
    pthread_mutex_unlock(&table_lock);
    printf("Runtime: audio port %d opened (type %d, %d ch, %s, %u frames)\n",id,type,p->channels,p->is_float ? "float" : "s16",length);
    return (type<<16) | id | 0x20000000;
}
static ABI int32_t audio_close(int32_t handle) {
    pthread_mutex_lock(&table_lock);
    int32_t error=0;
    Port *p=port_of(handle,&error);
    if (p) {
        pthread_mutex_lock(&p->lock);
        if (p->stream) SDL_DestroyAudioStream(p->stream);
        if (p->dump) fclose(p->dump);
        p->dump=NULL;
        p->stream=NULL; p->used=0;
        pthread_mutex_unlock(&p->lock);
    }
    pthread_mutex_unlock(&table_lock);
    return p ? 0 : error;
}
/* SOTC_VOLUME: master volume in percent (default 100, 25-400; #95, the game is quiet). Above
 * 100 the peaks go through a soft limiter instead of clipping hard. */
static volatile float master_volume_value=-1.0f;
static float master_volume(void) {
    float v=master_volume_value;
    if (v<0.0f) {
        const char *e=getenv("SOTC_VOLUME");
        double pct=e && e[0] ? atof(e) : 100.0;
        if (pct<25.0) pct=25.0;
        if (pct>400.0) pct=400.0;
        v=(float)(pct/100.0);
        master_volume_value=v;
    }
    return v;
}
/* The in-game menu's volume (bbport_overlay.cpp): percent, applied from the next buffer. */
void runtime_audio_set_volume_percent(int pct);
int runtime_audio_volume_percent(void);
void runtime_audio_set_volume_percent(int pct) {
    if (pct<25) pct=25;
    if (pct>400) pct=400;
    master_volume_value=(float)pct/100.0f;
}
int runtime_audio_volume_percent(void) { return (int)(master_volume()*100.0f+0.5f); }

/* Linear up to 0.8 of full scale, then a tanh knee that never reaches 1. */
static inline float soft_limit(float v) {
    float a=fabsf(v);
    if (a<=0.8f) return v;
    float o=0.8f+0.2f*tanhf((a-0.8f)/0.2f);
    return v<0.0f ? -o : o;
}

/* `pace`: wait for this port's next period. sceAudioOutOutputs waits once for all its ports. */
static int32_t output_port(int32_t handle, const void *data, int pace) {
    int32_t error=0;
    pthread_mutex_lock(&table_lock);
    Port *p=port_of(handle,&error);
    pthread_mutex_unlock(&table_lock);
    if (!p) return error;
    pthread_mutex_lock(&p->lock);
    size_t samples=(size_t)p->frames*(size_t)p->channels, bytes=samples*(size_t)p->sample_bytes;
    uint64_t period=(uint64_t)p->frames*1000000000u/48000u;
    if (data) {
        /* Apply per-channel volume and map PS4 8ch (L R C LFE SL SR BL BR) to SDL 7.1 order. */
        unsigned char converted[2048*8*4];
        static const int remap[8]={0,1,2,3,6,7,4,5};
        const float master=master_volume();
        const int limit=master>1.0f;
        for (size_t f=0;f<(size_t)p->frames;++f) for (int c=0;c<p->channels;++c) {
            int target=p->channels==8 && !p->std_layout ? remap[c] : c;
            size_t from=f*(size_t)p->channels+(size_t)c, to=f*(size_t)p->channels+(size_t)target;
            float gain=(float)p->volume[c]/VOLUME_0DB*master;
            if (p->is_float) {
                float v; memcpy(&v,(const char *)data+from*4,4); v*=gain;
                if (limit) v=soft_limit(v);
                memcpy(converted+to*4,&v,4);
            } else {
                int16_t s16; memcpy(&s16,(const char *)data+from*2,2);
                float v=(float)s16*gain/32768.0f;
                if (limit) v=soft_limit(v);
                s16=(int16_t)(v*32767.0f); memcpy(converted+to*2,&s16,2);
            }
        }
        if (p->dump) fwrite(converted,1,bytes,p->dump);
        if (p->stats) {
            uint64_t now=now_ns();
            if (!p->stat_start_ns) { p->stat_start_ns=now; p->stat_min_queued=1<<30; }
            else if (now-p->stat_last_ns>p->stat_max_gap_ns) p->stat_max_gap_ns=now-p->stat_last_ns;
            p->stat_last_ns=now; ++p->stat_buffers;
            if (p->stream) {
                int queued=SDL_GetAudioStreamQueued(p->stream);
                if (queued<p->stat_min_queued) p->stat_min_queued=queued;
                if (queued==0) ++p->stat_starved;
            }
            if (now-p->stat_start_ns>=5000000000u) {
                printf("Audio stats port %d: %u buffers/5s (expected %u), max gap %.2f ms, min queued %d bytes, starved %u\n",
                       (int)(p-ports),p->stat_buffers,(unsigned)(5u*48000u/(unsigned)p->frames),p->stat_max_gap_ns/1e6,
                       p->stat_min_queued,p->stat_starved);
                p->stat_start_ns=now; p->stat_max_gap_ns=0; p->stat_starved=0; p->stat_buffers=0; p->stat_min_queued=1<<30;
            }
        }
        if (pace) {
            uint64_t now=now_ns();
            if (!p->next_deadline_ns || now>p->next_deadline_ns+8*period) p->next_deadline_ns=now; /* start or stall: restart cadence */
            else sleep_until(p->next_deadline_ns);
            p->next_deadline_ns+=period;
        }
        if (!p->stream && sdl_ready>0 && now_ns()-p->open_tried_ns>=1000000000u) {
            open_stream(p);
            if (p->stream) printf("Runtime: audio port %d: device stream opened\n",(int)(p-ports));
        }
        if (p->stream) {
            /* The device drains the queue in quanta (21 ms on PipeWire), so the level
               is a sawtooth: its minimum over ~32 buffers is what gets controlled. */
            int low=2*(int)bytes, queued=SDL_GetAudioStreamQueued(p->stream);
            if (queued<(int)bytes) {
                static const unsigned char silence[2048*8*4];
                SDL_PutAudioStreamData(p->stream,silence,low-queued<(int)bytes ? low-queued : (int)bytes);
                queued=low;
            }
            if (!p->window_count || queued<p->window_min) p->window_min=queued;
            if (++p->window_count==32) {
                p->adjust_ns=p->window_min>low+2*(int)bytes ? (int64_t)(period/32) : p->window_min<low ? -(int64_t)(period/32) : 0;
                p->window_count=0;
            }
            p->next_deadline_ns+=(uint64_t)p->adjust_ns;
            SDL_PutAudioStreamData(p->stream,converted,(int)bytes);
        }
        ++buffers_out;
    }
    p->last_output_us=now_ns()/1000;
    pthread_mutex_unlock(&p->lock);
    return data ? (int32_t)samples : 0;
}
static ABI int32_t audio_output(int32_t handle, const void *data) { return output_port(handle,data,1); }
typedef struct { int32_t handle; const void *data; } OutputParam;
static ABI int32_t audio_outputs(const OutputParam *params, uint32_t count) {
    if (!params) return ERR_INVALID_POINTER;
    if (!count || count>PORTS) return ERR_PORT_FULL;
    int32_t result=0;
    /* One period per call for all ports (FMOD feeds main and BGM together): pacing every
       port would halve the rate and starve both. */
    for (uint32_t i=0;i<count;++i) { int32_t r=output_port(params[i].handle,params[i].data,i==0); if (r<0) return r; result=r; }
    return result;
}
static ABI int32_t audio_volume(int32_t handle, int32_t flags, const int32_t *volume) {
    int32_t error=0;
    pthread_mutex_lock(&table_lock);
    Port *p=port_of(handle,&error);
    pthread_mutex_unlock(&table_lock);
    if (!p) return error;
    if (!volume) return ERR_INVALID_POINTER;
    pthread_mutex_lock(&p->lock);
    for (int c=0;c<8;++c) if (flags & (1<<c)) {
        if (volume[c]<0 || volume[c]>VOLUME_0DB) { pthread_mutex_unlock(&p->lock); return ERR_INVALID_VOLUME; }
        p->volume[c]=volume[c];
    }
    pthread_mutex_unlock(&p->lock);
    return 0;
}
static ABI int32_t audio_state(int32_t handle, PortState *state) {
    int32_t error=0;
    pthread_mutex_lock(&table_lock);
    Port *p=port_of(handle,&error);
    pthread_mutex_unlock(&table_lock);
    if (!p) return error;
    if (!state) return ERR_INVALID_POINTER;
    memset(state,0,sizeof(*state));
    switch (p->type) {
    case 2: case 3: state->output=0x40; state->channel=1; break;          /* headphone */
    case 4: state->output=0x04; state->channel=1; state->volume=127; break; /* pad speaker */
    default: state->output=0x01; state->channel=(uint8_t)(p->channels>2 ? 2 : p->channels); break;
    }
    return 0;
}
static ABI int32_t audio_last_time(int32_t handle, uint64_t *time) {
    int32_t error=0;
    pthread_mutex_lock(&table_lock);
    Port *p=port_of(handle,&error);
    pthread_mutex_unlock(&table_lock);
    if (!p) return error;
    if (!time) return ERR_INVALID_POINTER;
    *time=p->last_output_us; return 0;
}

/* --- libSceAudio3d. Shadow of the Colossus' sound engine (sndx) plays its positional voices as
 * Audio3d objects: during each NGS2 render their UserFx sets the grain's mono PCM with the
 * position, gain and spread (sceAudio3dObjectSetAttributes), then PortAdvance closes the grain and
 * a thread of the game pushes it. An advance pans the objects to stereo here (by the azimuth of
 * their position, +x right of the listener, narrowed by their spread; the game's gain already
 * holds the distance) into a small queue; a push plays it on the port's own SDL stream, next to
 * the AudioOut ports on the same device. --- */
#define A3D_OBJECTS 1024
#define A3D_MAX_GRAIN 1024
#define A3D_QUEUE 8
#define A3D_ERR_OUT_OF_RESOURCES ((int32_t)0x80EA0004)
#define A3D_ERR_INVALID_PORT ((int32_t)0x80EA0005)
#define A3D_ERR_INVALID_OBJECT ((int32_t)0x80EA0006)
#define A3D_ERR_INVALID_PARAMETER ((int32_t)0x80EA0007)
enum { A3D_PCM=1, A3D_PRIORITY=2, A3D_POSITION=3, A3D_SPREAD=4, A3D_GAIN=5, A3D_RESET_STATE=7, A3D_AMBISONICS=9 };
typedef struct { uint32_t id, pad; const void *value; uint64_t size; } A3dAttribute;
typedef struct { uint32_t format, pad; const void *buffer; uint32_t samples; } A3dPcm; /* format 0 s16, 1 float */
_Static_assert(sizeof(A3dAttribute)==24 && sizeof(A3dPcm)==24,"Audio3d attribute layout");
typedef struct {
    int used, has_pcm, has_position, ambisonics;
    float gain, spread, position[3];
    uint32_t frames;
    float pcm[A3D_MAX_GRAIN];
} A3dObject;
static struct {
    pthread_mutex_t lock;
    int open;
    uint32_t grain, depth, head, count;
    SDL_AudioStream *stream;
    A3dObject objects[A3D_OBJECTS];
    float queue[A3D_QUEUE][A3D_MAX_GRAIN*2];
    size_t grains, objects_heard;
} a3d={.lock=PTHREAD_MUTEX_INITIALIZER};

static void a3d_reset(A3dObject *o) {
    o->has_pcm=o->has_position=0; o->ambisonics=-1;
    o->gain=1.0f; o->spread=0.0f; o->position[0]=o->position[1]=o->position[2]=0.0f;
}
static A3dObject *a3d_object(uint32_t port, uint32_t object) {
    return a3d.open && port==0 && object<A3D_OBJECTS && a3d.objects[object].used ? &a3d.objects[object] : NULL;
}
static ABI int32_t a3d_initialize(int64_t reserved) { (void)reserved; return 0; }
static ABI int32_t a3d_terminate(void) { return 0; }
/* Open parameters: u64 size, u32 granularity, rate, max objects, queue depth, buffer mode. */
static ABI int32_t a3d_port_open(int32_t user, const uint32_t *params, uint32_t *port) {
    (void)user;
    if (!params || !port) return A3D_ERR_INVALID_PARAMETER;
    pthread_mutex_lock(&a3d.lock);
    const uint32_t grain=params[2], depth=params[0]>=24 ? params[5] : 0;
    a3d.grain=grain>=256 && grain<=A3D_MAX_GRAIN ? grain : 256;
    a3d.depth=depth>=1 && depth<=A3D_QUEUE ? depth : 2;
    a3d.head=a3d.count=0;
    if (!a3d.stream && sdl_audio()) {
        SDL_AudioSpec spec={SDL_AUDIO_F32,2,48000};
        a3d.stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,NULL,NULL);
        if (a3d.stream) SDL_ResumeAudioStreamDevice(a3d.stream);
        else fprintf(stderr,"Runtime: SDL audio stream failed (%s); Audio3d objects are silent\n",SDL_GetError());
    }
    a3d.open=1;
    *port=0;
    pthread_mutex_unlock(&a3d.lock);
    printf("Runtime: Audio3d port opened (%u-sample grains, queue %u)\n",a3d.grain,a3d.depth);
    return 0;
}
static ABI int32_t a3d_port_close(uint32_t port) {
    if (port!=0) return A3D_ERR_INVALID_PORT;
    pthread_mutex_lock(&a3d.lock);
    if (a3d.stream) SDL_DestroyAudioStream(a3d.stream);
    a3d.stream=NULL; a3d.open=0;
    for (int i=0;i<A3D_OBJECTS;++i) a3d.objects[i].used=0;
    pthread_mutex_unlock(&a3d.lock);
    return 0;
}
static ABI int32_t a3d_object_reserve(uint32_t port, uint32_t *object) {
    if (!object) return A3D_ERR_INVALID_PARAMETER;
    pthread_mutex_lock(&a3d.lock);
    int32_t r=a3d.open && port==0 ? A3D_ERR_OUT_OF_RESOURCES : A3D_ERR_INVALID_PORT;
    for (uint32_t i=0;r==A3D_ERR_OUT_OF_RESOURCES && i<A3D_OBJECTS;++i) if (!a3d.objects[i].used) {
        a3d.objects[i].used=1; a3d_reset(&a3d.objects[i]); *object=i; r=0;
    }
    pthread_mutex_unlock(&a3d.lock);
    return r;
}
static ABI int32_t a3d_object_unreserve(uint32_t port, uint32_t object) {
    pthread_mutex_lock(&a3d.lock);
    A3dObject *o=a3d_object(port,object);
    if (o) o->used=0;
    pthread_mutex_unlock(&a3d.lock);
    return o ? 0 : A3D_ERR_INVALID_OBJECT;
}
static ABI int32_t a3d_object_set_attributes(uint32_t port, uint32_t object, uint64_t count, const A3dAttribute *attributes) {
    if (count && !attributes) return A3D_ERR_INVALID_PARAMETER;
    pthread_mutex_lock(&a3d.lock);
    A3dObject *o=a3d_object(port,object);
    for (uint64_t i=0;o && i<count;++i) {
        const A3dAttribute *a=&attributes[i];
        if (!a->value) continue;
        switch (a->id) {
        case A3D_PCM: {
            if (a->size<sizeof(A3dPcm)) break;
            const A3dPcm *pcm=a->value;
            if (!pcm->buffer) break;
            const uint32_t n=pcm->samples<a3d.grain ? pcm->samples : a3d.grain;
            if (pcm->format==1) memcpy(o->pcm,pcm->buffer,n*sizeof(float));
            else for (uint32_t s=0;s<n;++s) o->pcm[s]=((const int16_t *)pcm->buffer)[s]/32768.0f;
            o->frames=n; o->has_pcm=1;
            break;
        }
        case A3D_POSITION:
            if (a->size>=12) { memcpy(o->position,a->value,12); o->has_position=1; }
            break;
        case A3D_SPREAD: if (a->size>=4) memcpy(&o->spread,a->value,4); break;
        case A3D_GAIN: if (a->size>=4) memcpy(&o->gain,a->value,4); break;
        case A3D_AMBISONICS: if (a->size>=4) memcpy(&o->ambisonics,a->value,4); break;
        case A3D_RESET_STATE: a3d_reset(o); break;
        default: break;
        }
    }
    pthread_mutex_unlock(&a3d.lock);
    return o ? 0 : A3D_ERR_INVALID_OBJECT;
}
static ABI int32_t a3d_port_set_attribute(uint32_t port, uint32_t id, const void *value, uint64_t size) {
    (void)id; (void)value; (void)size;
    return port==0 ? 0 : A3D_ERR_INVALID_PORT;
}
/* Closes the grain: every object given PCM since the last advance is panned into it. */
static ABI int32_t a3d_port_advance(uint32_t port) {
    if (port!=0) return A3D_ERR_INVALID_PORT;
    pthread_mutex_lock(&a3d.lock);
    if (a3d.count==A3D_QUEUE) { a3d.head=(a3d.head+1)%A3D_QUEUE; --a3d.count; } /* nobody pushes: oldest dropped */
    float *mix=a3d.queue[(a3d.head+a3d.count)%A3D_QUEUE];
    memset(mix,0,sizeof(float)*2*a3d.grain);
    for (int i=0;i<A3D_OBJECTS;++i) {
        A3dObject *o=&a3d.objects[i];
        if (!o->used || !o->has_pcm) continue;
        /* Constant-power pan by the azimuth; a wide spread pulls the source to the centre, an
           ambisonic or unplaced object stays there. */
        float pan=0.0f;
        const float x=o->position[0], z=o->position[2], d=sqrtf(x*x+z*z);
        if (o->has_position && o->ambisonics<0 && d>1e-4f) {
            const float width=1.0f-fminf(fmaxf(o->spread,0.0f)/3.14159265f,1.0f);
            pan=fmaxf(-1.0f,fminf(1.0f,x/d))*width;
        }
        const float angle=(pan+1.0f)*0.78539816f, left=cosf(angle)*o->gain, right=sinf(angle)*o->gain;
        for (uint32_t s=0;s<o->frames;++s) { mix[2*s]+=o->pcm[s]*left; mix[2*s+1]+=o->pcm[s]*right; }
        o->has_pcm=0;
        ++a3d.objects_heard;
    }
    ++a3d.count; ++a3d.grains;
    static int told;
    const int first=!told && a3d.objects_heard;
    told|=first;
    pthread_mutex_unlock(&a3d.lock);
    if (first) puts("Runtime: Audio3d objects are playing");
    return 0;
}
/* Plays the queued grains. The stream is kept 2 grains ahead like an AudioOut port: silence on an
   underrun, a grain dropped when it runs far ahead. */
static ABI int32_t a3d_port_push(uint32_t port, int32_t blocking) {
    (void)blocking;
    if (port!=0) return A3D_ERR_INVALID_PORT;
    pthread_mutex_lock(&a3d.lock);
    const int bytes=(int)(sizeof(float)*2*a3d.grain);
    for (;a3d.count;a3d.head=(a3d.head+1)%A3D_QUEUE,--a3d.count) {
        if (!a3d.stream) continue;
        const int queued=SDL_GetAudioStreamQueued(a3d.stream);
        if (queued<bytes) {
            static const float silence[A3D_MAX_GRAIN*2];
            SDL_PutAudioStreamData(a3d.stream,silence,bytes);
        }
        if (queued<8*bytes) SDL_PutAudioStreamData(a3d.stream,a3d.queue[a3d.head],bytes);
    }
    pthread_mutex_unlock(&a3d.lock);
    return 0;
}
static ABI int32_t a3d_port_queue_level(uint32_t port, uint32_t *level, uint32_t *available) {
    if (port!=0) return A3D_ERR_INVALID_PORT;
    pthread_mutex_lock(&a3d.lock);
    if (level) *level=a3d.count;
    if (available) *available=a3d.count<a3d.depth ? a3d.depth-a3d.count : 0;
    pthread_mutex_unlock(&a3d.lock);
    return 0;
}

static const RuntimeExport exports[]={
    {"sceAudioOutInit",audio_init}, {"sceAudioOutOpen",audio_open}, {"sceAudioOutClose",audio_close},
    {"sceAudioOutOutput",audio_output}, {"sceAudioOutOutputs",audio_outputs},
    {"sceAudioOutSetVolume",audio_volume}, {"sceAudioOutGetPortState",audio_state},
    {"sceAudioOutGetLastOutputTime",audio_last_time},
    {"sceAudio3dInitialize",a3d_initialize}, {"sceAudio3dTerminate",a3d_terminate},
    {"sceAudio3dPortOpen",a3d_port_open}, {"sceAudio3dPortClose",a3d_port_close},
    {"sceAudio3dObjectReserve",a3d_object_reserve}, {"sceAudio3dObjectUnreserve",a3d_object_unreserve},
    {"sceAudio3dObjectSetAttributes",a3d_object_set_attributes},
    {"sceAudio3dPortSetAttribute",a3d_port_set_attribute}, {"sceAudio3dPortAdvance",a3d_port_advance},
    {"sceAudio3dPortPush",a3d_port_push}, {"sceAudio3dPortGetQueueLevel",a3d_port_queue_level},
};
uintptr_t runtime_audio_resolve(const char *name) { return RUNTIME_LOOKUP(exports,name); }
void runtime_audio_report(void) {
    printf("Runtime: audio ports opened=%zu, buffers output=%zu; Audio3d grains=%zu, object grains=%zu\n",
           ports_opened,buffers_out,a3d.grains,a3d.objects_heard);
}
