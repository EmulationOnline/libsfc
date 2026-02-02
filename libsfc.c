#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "corelib.h"
#include "ring.h"

#ifndef __wasm32__
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <assert.h>
#endif

//=============================================================================
// bsnes integration
//=============================================================================

// Forward declaration
static void video_cb_impl(const void* data, unsigned width, unsigned height, size_t pitch);

// Callbacks that program.cpp expects (normally provided by libretro.cpp)
static void video_cb(const void* data, unsigned width, unsigned height, size_t pitch) {
    video_cb_impl(data, width, height, pitch);
}

static void audio_queue(int16_t left, int16_t right) {
    // TODO: push to ring buffer
}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
    // TODO: implement input
    return 0;
}

static bool environ_cb(unsigned cmd, void* data) {
    return false;
}

// Include libretro header for constants used by program.cpp
#include "bsnes/bsnes/target-libretro/libretro.h"

// Include bsnes program implementation
// (defines: emulator, program, Program class, and all bsnes headers)
#include "bsnes/bsnes/target-libretro/program.cpp"

#define REQUIRE_SYSTEM(val) if (!program) { printf("Skipping %s\n", __func__); return val; }

#define puts(arg) emu_puts_cb(arg)
static void (*emu_puts_cb)(const char *) = NULL;
extern "C" __attribute__((visibility("default")))
void corelib_set_puts(void (*cb)(const char *)) {
    emu_puts_cb = cb;
}

// current_system is declared extern in blastem.h, defined in stubs.c
uint8_t is_pal = 0;

struct ring_i16 ring_;

extern "C" __attribute__((visibility("default")))
int width() {
    return 512;
}

extern "C" __attribute__((visibility("default")))
int height() {
    return is_pal ? 239 : 224;
}

extern "C" __attribute__((visibility("default")))
int framerate() {
    // 59.922751 ntsc, 49.701459 pal
    return is_pal ? 50 : 60;
}



__attribute__((visibility("default")))
uint32_t fbuffer_[VIDEO_WIDTH * VIDEO_HEIGHT];

// Video callback implementation - copies bsnes output to our framebuffer
static void video_cb_impl(const void* data, unsigned width, unsigned height, size_t pitch) {
    const uint32_t* src = (const uint32_t*)data;
    size_t src_pitch = pitch / sizeof(uint32_t);

    for (unsigned y = 0; y < height && y < VIDEO_HEIGHT; y++) {
        for (unsigned x = 0; x < width && x < VIDEO_WIDTH; x++) {
            fbuffer_[y * VIDEO_WIDTH + x] = src[y * src_pitch + x];
        }
    }
}

extern "C" __attribute__((visibility("default")))
void set_key(size_t key, char val) {
    REQUIRE_SYSTEM();
    // TODO: map corelib keys to bsnes input
}

extern "C" __attribute__((visibility("default")))
const uint8_t *framebuffer() {
    return (uint8_t*)fbuffer_;
}

extern "C" __attribute__((visibility("default")))
void frame() {
    REQUIRE_SYSTEM();
    emulator->run();
}

static void cleanup() {
    if (program) {
        program->save();
        emulator->unload();
        delete program;
        program = nullptr;
    }
    if (emulator) {
        delete emulator;
        emulator = nullptr;
    }
}

extern "C" __attribute__((visibility("default")))
void init(const uint8_t* data, size_t len) {
    // Clean up any previous instance
    cleanup();

    ring_init(&ring_);

    // Create emulator and program
    emulator = new SuperFamicom::Interface;
    program = new Program;

    // Configure audio
    emulator->configure("Audio/Frequency", SAMPLE_RATE);

    // Set up video (no filtering)
    program->filterRender = &Filter::None::render;
    program->filterSize = &Filter::None::size;
    program->updateVideoPalette();

    // Load ROM from memory
    if (!program->loadSuperFamicomFromMemory(data, len)) {
        printf("Failed to load ROM\n");
        cleanup();
        return;
    }

    // Initialize emulator (calls emulator->load(), applies hacks, calls emulator->power())
    program->load();

    // Detect PAL/NTSC from ROM header
    is_pal = (program->superFamicom.region == "PAL") ? 1 : 0;
    printf("is_pal: %d\n", is_pal);

    // Connect controllers
    emulator->connect(SuperFamicom::ID::Port::Controller1, SuperFamicom::ID::Device::Gamepad);
    emulator->connect(SuperFamicom::ID::Port::Controller2, SuperFamicom::ID::Device::Gamepad);

    printf("bsnes initialized successfully\n");
}

extern "C" __attribute__((visibility("default")))
long apu_sample_variable(int16_t *output, int32_t frames) {
    REQUIRE_SYSTEM(0);
    size_t received = ring_pull(&ring_, output, frames);
    if (received < frames) {
        printf("underrun, filling %d - %ld frames\n", frames, received);
        // int16_t last = received > 0 ? output[received-1] : 0;
        for (int i = received; i < frames; i++) {
            output[i] = 0;
        }
    }
    return received;
}

// Returns bytes saved, and writes to dest. 
// Dest may be null to calculate size only. returns < 0 on error.
extern "C" __attribute__((visibility("default")))
int save_str(uint8_t* dest, int capacity) {
    REQUIRE_SYSTEM(0);
    return 0; // FIXME
}

extern "C" __attribute__((visibility("default")))
void load_str(int len, const uint8_t* src) {
    REQUIRE_SYSTEM();
    // FIXME
}

#ifndef __wasm32__
// file interface unavail for wasm

extern "C" __attribute__((visibility("default")))
void save(int fd) {
    REQUIRE_SYSTEM();
    int est = save_str(NULL, 0);
    printf("Estimated save size: %d\n", est);
    uint8_t *buffer = (uint8_t*)malloc(est);
    int size = save_str(buffer, est);
    assert(size <= est);
    const uint8_t* wr = buffer;
    while(size > 0) {
        ssize_t count = write(fd, wr, size);
        if (count < 0) {
            perror("write failed: ");
            exit(1);
            return;
        }
        size -= count;
        wr += count;
    }
    free(buffer);
}

extern "C" __attribute__((visibility("default")))
void load(int fd) {
    REQUIRE_SYSTEM();
    off_t pos = lseek(fd, 0, SEEK_END);
    if (pos < 0) {
        perror("lseek failed: ");
        exit(1);
        return;
    }
    lseek(fd, 0, SEEK_SET);
    uint8_t *buffer = (uint8_t*)malloc(pos);
    size_t count = pos;
    uint8_t *rd = buffer;
    while (count > 0) {
        ssize_t c = read(fd, rd, count);
        if (c < 0) {
            perror("Read failed: ");
            exit(1);
            return;
        }
        rd += c;
        count -= c;
    }
    load_str(pos, buffer);
    free(buffer);
}

extern "C" __attribute__((visibility("default")))
void dump_state(const char* filename) {
    REQUIRE_SYSTEM();
    int fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY , 0700);
    if (fd == -1) {
        perror("failed to open:");
        return;
    }
    printf("saving to %s\n", filename);
    save(fd);
    close(fd);
}

extern "C" __attribute__((visibility("default")))
void load_state(const char* filename) {
    REQUIRE_SYSTEM();
    int fd = open(filename,  O_RDONLY , 0700);
    if (fd == -1) {
        perror("Failed to open: ");
        return;
    }
    load(fd);
    close(fd);
}

#endif // wasm32

