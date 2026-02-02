#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "corelib.h"
#include "blastem/system.h"
#include "blastem/blastem.h"
#include "blastem/util.h"
#include "blastem/vdp.h"
#include "blastem/render.h"
#include "blastem/render_audio.h"
#include "blastem/io.h"
#include "blastem/genesis.h"
#include "blastem/sms.h"
#include "blastem/cdimage.h"
#include "ring.h"

#ifndef __wasm32__
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <assert.h>
#endif

#define REQUIRE_SYSTEM(val) if (!current_system) { printf("Skipping %s\n", __func__); return val; }

#define puts(arg) emu_puts_cb(arg)
static void (*emu_puts_cb)(const char *) = NULL;
void corelib_set_puts(void (*cb)(const char *)) {
    emu_puts_cb = cb;
}

// current_system is declared extern in blastem.h, defined in stubs.c
static system_media cart_;
static system_type stype;
static uint8_t started = 0;

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
    // 59.922751 ntsc,
    // 49.701459 pal
    return is_pal ? 50 : 60;
}



// Required by BlastEm internals
const system_media *current_media(void) {
    return &cart_;
}

__attribute__((visibility("default")))
uint32_t fbuffer_[VIDEO_WIDTH * VIDEO_HEIGHT];

__attribute__((visibility("default")))
void set_key(size_t key, char val) {
    REQUIRE_SYSTEM();
    int core_key;
    switch(key) {
        case BTN_A: core_key = BUTTON_A; break;
        case BTN_B: core_key = BUTTON_B; break;
        case BTN_X: core_key = BUTTON_C; break;
        case BTN_Start: core_key = BUTTON_START; break;
        case BTN_Up: core_key = DPAD_UP; break;
        case BTN_Down: core_key = DPAD_DOWN; break;
        case BTN_Left: core_key = DPAD_LEFT; break;
        case BTN_Right: core_key = DPAD_RIGHT; break;
        default: return;
    }

    const int PLAYER_NUM = 1;
    if (val) {
        current_system->gamepad_down(current_system, PLAYER_NUM, core_key);
    } else {
        current_system->gamepad_up(current_system, PLAYER_NUM, core_key);
    }
}

__attribute__((visibility("default")))
const uint8_t *framebuffer() {
    return (uint8_t*)fbuffer_;
}

// BlastEm's framebuffer from stubs.c
extern uint32_t genesis_fb[];

__attribute__((visibility("default")))
void frame() {
    REQUIRE_SYSTEM();
    if (started) {
        current_system->resume_context(current_system);
    } else {
        current_system->start_context(current_system, NULL);
        started = 1;
    }
    // Copy from BlastEm's framebuffer to our fbuffer_, skipping borders
    // Active area starts at BORDER_LEFT (13) horizontally
    for (int y = 0; y < VIDEO_HEIGHT; y++) {
        memcpy(&fbuffer_[y * VIDEO_WIDTH], &genesis_fb[y * LINEBUF_SIZE + BORDER_LEFT], VIDEO_WIDTH * sizeof(uint32_t));
    }
}

__attribute__((visibility("default")))
void init(const uint8_t* data, size_t len) {
    ring_init(&ring_);
    // Clean up previous system if any
    if (current_system) {
        puts("freeing old system");
        current_system->free_context(current_system);
        current_system = NULL;
    }
    if (cart_.buffer) {
        puts("freeing cart.");
        cart_.buffer = NULL;
    }

    // Reset state
    started = 0;
    stype = SYSTEM_UNKNOWN;
    memset(&cart_, 0, sizeof(cart_));

    // Initialize audio subsystem: 44100 Hz mono output
    render_audio_initialized(RENDER_AUDIO_S16, SAMPLE_RATE, 1, AUDIO_TMP_LEN, sizeof(int16_t));

    // Copy ROM data to our own buffer (rounded to power of 2 as BlastEm expects)
    size_t alloc_size = nearest_pow2(len);
    cart_.buffer = malloc(alloc_size);
    if (!cart_.buffer) {
        fprintf(stderr, "Failed to allocate %zu bytes for ROM\n", alloc_size);
        return;
    }
    memcpy(cart_.buffer, data, len);
    cart_.size = len;

    // Detect system type (Genesis, SMS, Game Gear, etc.)
    stype = detect_system_type(&cart_);
    printf("Detected system type: %d\n", stype);

    // Allocate and configure the emulator
    current_system = alloc_config_system(stype, &cart_, 0, 0);
    if (!current_system) {
        fprintf(stderr, "Failed to allocate system\n");
        free(cart_.buffer);
        cart_.buffer = NULL;
        return;
    }

    printf("System initialized successfully\n");
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
    size_t bytes;
    uint8_t* data = current_system->serialize(current_system, &bytes);
    printf("state size: %lu\n", bytes);
    if (dest != NULL) {
        assert(capacity >= bytes);
        memcpy(dest, data, bytes);
    } else {
        // add some padding for possible vdp state shifts.
        bytes += 256;
    }
    free(data);

    return bytes;
}

__attribute__((visibility("default")))
void load_str(int len, const uint8_t* src) {
    REQUIRE_SYSTEM();
    current_system->deserialize(current_system, src, len);
    started = 1;
}

#ifndef __wasm32__
// file interface unavail for wasm

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
    uint8_t *buffer = malloc(pos);
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

#endif // wasm32

