#define DEBUG 1
#include "corelib.h"
#include <SDL2/SDL.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>

void emu_puts(const char* msg) {
    printf("EMU LOG: %s\n", msg);
}

void sdl_update_keys(const uint8_t *sdlkeys) {
    set_key(BTN_A, sdlkeys[SDL_SCANCODE_C]);      // x
    set_key(BTN_B, sdlkeys[SDL_SCANCODE_V]);      // circle
    set_key(BTN_X, sdlkeys[SDL_SCANCODE_Z]);      // x
    set_key(BTN_Y, sdlkeys[SDL_SCANCODE_X]);      // x

    set_key(BTN_Sel, sdlkeys[SDL_SCANCODE_TAB]);    // Select
    set_key(BTN_Start, sdlkeys[SDL_SCANCODE_RETURN]); // Start
    set_key(BTN_Up, sdlkeys[SDL_SCANCODE_UP]);     // Dpad Up
    set_key(BTN_Down, sdlkeys[SDL_SCANCODE_DOWN]);   // Dpad Down
    set_key(BTN_Left, sdlkeys[SDL_SCANCODE_LEFT]);   // Dpad Left
    set_key(BTN_Right, sdlkeys[SDL_SCANCODE_RIGHT]);   // Dpad Right
    set_key(BTN_L, sdlkeys[SDL_SCANCODE_A]);  // left shoulder
    set_key(BTN_R, sdlkeys[SDL_SCANCODE_S]);  // right shoulder
    set_key(BTN_L2, sdlkeys[SDL_SCANCODE_Q]);
    set_key(BTN_R2, sdlkeys[SDL_SCANCODE_W]);
}

char run = 1;
const uint8_t *sdlkeys;
void input() {
    for (SDL_Event event; SDL_PollEvent(&event);) {
        if (event.type == SDL_QUIT) {
            run = 0;
        }
    }
  sdl_update_keys(sdlkeys);
}

size_t slurp(const char*filename, uint8_t* out, size_t capacity) {
    size_t pos = 0;
    int fd = open(filename, 0);
    if (fd == -1) {
        perror("open fail: ");
        exit(1);
    }
    off_t result = lseek(fd, 0, SEEK_END); 
    printf("lseek returned: %ld\n", result);
    printf("capacity is: %ld\n", capacity);
    if (result < 0) {
        perror("seek failed: ");
        exit(1);
    }
    assert(result <= capacity);
    lseek(fd, 0, SEEK_SET);
    while (1) {
        ssize_t count = read(fd, out + pos, capacity - pos);
        if (count == -1) {
            perror("read fail: ");
            exit(1);
        }
        if (count == 0) break;
        pos += count;
    }
    return pos;
}

typedef struct {
    SDL_AudioDeviceID device;
} SoundCard;

void audio_callback(void *userdata, uint8_t* data, int bytes) {
    // printf("attempt to dequeue %d bytes\n", bytes);
    assert(bytes % sizeof(int16_t) == 0);
    apu_sample_variable((int16_t*)data, bytes / sizeof(int16_t));
}

void soundcard_init(SoundCard *snd) {
    SDL_AudioSpec desired;
    desired.freq = SAMPLE_RATE;
    desired.format = AUDIO_S16LSB;   // intel and aarch64 are both LE
    desired.channels = 1;
    desired.samples = 0;
    desired.callback = audio_callback;  // async audio
    SDL_AudioSpec actual;
    int res = SDL_OpenAudio(&desired, &actual);
    assert(res == 0);
    assert(desired.freq == actual.freq);
    // assert(desired.format == actual.format);  // some bits may change
    assert(desired.channels == actual.channels);
    // assert(desired.samples == actual.samples);

    snd->device = 1;
    SDL_PauseAudioDevice(snd->device, 0);
}


void soundcard_queue(SoundCard *snd, uint8_t* data, size_t bytes) {
    if(SDL_QueueAudio(snd->device, (void*)data, bytes) != 0) {
        printf("audio queue fail: '%s'\n", SDL_GetError());
    }
    SDL_PauseAudioDevice(snd->device, 0);
}

const int ROM_BUFFER_BYTES = 500*1024*1024;
const int BYTES_PER_PIXEL=4;
const int SCALE = 2;
int main(int argc, char **argv) {
  corelib_set_puts(emu_puts);
  set_cachedir(".");
  sdlkeys = (uint8_t*)SDL_GetKeyboardState(0);

  char skipSave = 0;

  if (argc == 0) {
      puts("usage: main [rom]");
      return 0;
  } else if (argc == 1) {
      printf("usage: %s [rom]\n", argv[0]);
      return 0;
  } else if (argc == 2) {
      // load rom only.
  } else if (argc == 3) {
      if (strcmp(argv[2], "runc") == 0) {
          // skipsave
          skipSave = 1;
      } else {
        // take arg as bios path
        const char *filename = argv[2];
        printf("Taking  %s as bios path\n", filename);
        const int CAP = 0x80000;
        char* buffer = malloc(CAP);
        size_t len = slurp(filename, buffer, CAP);
        if (len == 0 || len != CAP) {
            puts("Failed to load bios file.");
            return 1;
        }
        copy_bios(buffer, len);
        free(buffer);
      }
  }

  uint8_t *buffer = (uint8_t*)malloc(ROM_BUFFER_BYTES);
  size_t bytes_read = slurp(argv[1], buffer, ROM_BUFFER_BYTES);
  printf("read bytes: %lu\n", bytes_read);
  init(buffer, bytes_read);

  char save_file[1024];
  snprintf(save_file, 1023, "%s.sav", argv[1]);
  if (skipSave) {
      printf("Skipping ubersave load.\n");
  } else {
      printf("Loading %s\n", save_file);
      load_state(save_file);
  }

  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window *window = 
      SDL_CreateWindow("pico station", 0, 0, VIDEO_WIDTH*SCALE, VIDEO_HEIGHT*SCALE, SDL_WINDOW_SHOWN);
  SDL_Renderer *renderer = SDL_CreateRenderer(
      window, -1,
      SDL_RENDERER_ACCELERATED);
  SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32,
  // SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                    SDL_TEXTUREACCESS_STREAMING, VIDEO_WIDTH, VIDEO_HEIGHT);
  SoundCard soundcard;
  soundcard_init(&soundcard);
  // soundcard_test();

  clock_t last = clock();
  clock_t start = clock();
  static_assert(CLOCKS_PER_SEC == 1000*1000, "Bad clock assumptions");
  char title[50];
  int w=0,h=0;

  SDL_Rect srcRect = {
      .x = 0,
      .y = 0,
      .w = 0,
      .h = 0,
  };
  while(run) {
      input();
      frame();
      int wp = width();
      int hp = height();
      if (wp != w || hp != h) {
          w = wp; 
          h = hp; 
          snprintf(title, sizeof(title), "picostation - %d x %d", w, h);
          SDL_SetWindowTitle(window, title);
          SDL_SetWindowSize(window, w*SCALE, h*SCALE);
          srcRect.w = w;
          srcRect.h = h;
      }
      SDL_UpdateTexture(texture, 0, framebuffer(), VIDEO_WIDTH*BYTES_PER_PIXEL);
      SDL_RenderCopy(renderer, texture, &srcRect, 0);
      SDL_RenderPresent(renderer);
      // soundcard_queue(&soundcard, audio_buffer, audio_bytes);
      // SDL_UpdateTexture(texture, 0, finished_frame + 2048, 512);
      // SDL_RenderCopy(renderer, texture, 0, 0);
      // SDL_RenderPresent(renderer);
      clock_t now = clock();
      // printf("now: %ld\n", now);
      // printf("frame speed: %f\n", 1000.0*1000 / (now - last));
      size_t delta = 1000*1000 / framerate();
      ssize_t delay = (last + delta) - now;
      if (delay > 0) {
          usleep(delay);
      } else {
          puts("lagging");
      }
      last = now;
  }
  printf("save file: %s\n", save_file);
  dump_state(save_file);
}
