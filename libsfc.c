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

// SNES video dimensions (512 width for hi-res, 478 height for PAL support)
#define VIDEO_WIDTH 512
#define VIDEO_HEIGHT 478

// Stub emulator state - will be connected to bsnes later
static void* current_system = NULL;

#define REQUIRE_SYSTEM(val) if (!current_system) { printf("Skipping %s\n", __func__); return val; }

#define puts(arg) emu_puts_cb(arg)
static void (*emu_puts_cb)(const char *) = NULL;
void corelib_set_puts(void (*cb)(const char *)) {
    emu_puts_cb = cb;
}

// current_system is declared extern in blastem.h, defined in stubs.c
uint8_t is_pal = 0;

struct ring_i16 ring_;

__attribute__((visibility("default")))
int width() {
    return 320;
}

__attribute__((visibility("default")))
int height() {
    return is_pal ? 224 : 240;
}

__attribute__((visibility("default")))
int framerate() { 
    // FIXME
    // 59.922751 ntsc,
    // 49.701459 pal
    return is_pal ? 50 : 60;
}



__attribute__((visibility("default")))
uint32_t fbuffer_[VIDEO_WIDTH * VIDEO_HEIGHT];

__attribute__((visibility("default")))
void set_key(size_t key, char val) {
    REQUIRE_SYSTEM();
}

__attribute__((visibility("default")))
const uint8_t *framebuffer() {
    return (uint8_t*)fbuffer_;
}

__attribute__((visibility("default")))
void frame() {
    REQUIRE_SYSTEM();
}

__attribute__((visibility("default")))
void init(const uint8_t* data, size_t len) {
    ring_init(&ring_);
}

__attribute__((visibility("default")))
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
__attribute__((visibility("default")))
int save_str(uint8_t* dest, int capacity) {
    REQUIRE_SYSTEM(0);
    return 0; // FIXME
}

__attribute__((visibility("default")))
void load_str(int len, const uint8_t* src) {
    REQUIRE_SYSTEM();
    // FIXME
}

#ifndef __wasm32__
// file interface unavail for wasm

__attribute__((visibility("default")))
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

__attribute__((visibility("default")))
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

__attribute__((visibility("default")))
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

__attribute__((visibility("default")))
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

