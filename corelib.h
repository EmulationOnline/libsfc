#pragma once

#include <stdint.h>
#include <stddef.h>

// SNES video dimensions (512 width for hi-res, 478 height for PAL support)
#define VIDEO_WIDTH 512
#define VIDEO_HEIGHT 478

enum Keys {
    BTN_A = 0,
    BTN_B,
    BTN_Sel,
    BTN_Start,
    BTN_Up,
    BTN_Down,
    BTN_Left,
    BTN_Right,
    BTN_L,  // Shoulder buttons // 8
    BTN_R, // 9
    BTN_L2, // 10
    BTN_R2, // 11
    BTN_X, // 12
    BTN_Y, // 13
    NUM_KEYS
};

extern "C" void corelib_set_puts(void(*cb)(const char*));

extern "C" void set_key(size_t key, char val);
extern "C" void init(const uint8_t* data, size_t len);

// Note: Framebuffer creates a rgba32 formatted buffer.
extern "C" const uint8_t *framebuffer();

#ifdef __wasm32__
extern "C" size_t framebuffer_bytes();
extern "C" uint8_t *alloc_rom(size_t bytes);
#endif

extern "C" void frame();
// dynamic video
extern "C" int framerate();
extern "C" int width();
extern "C" int height();

extern "C" void dump_state(const char* save_path);
extern "C" void load_state(const char* save_path);

// APU
// const int SAMPLE_RATE = 35112 * 59.727 / 64;
const int SAMPLE_RATE = 44100; // Original
const int SAMPLES_PER_FRAME = SAMPLE_RATE / 60;
extern "C" void apu_tick_60hz();
extern "C" void apu_sample_60hz(int16_t *output);
extern "C" long apu_sample_variable(int16_t *output, int32_t frames);
