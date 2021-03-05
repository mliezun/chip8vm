#include <SDL2/SDL.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

// Main memory
uint8_t mem[4096];

// Registers | V0 - VF
uint8_t registers[16];

// Program counter
uint16_t pc = 0x200;

// I register
uint16_t I = 0;

// Screen 32x64 pixels monochrome (1bit each pixel)
uint8_t screen[32][64];

// Values for bcd sprites
uint8_t bcd_sprites[16][5] = {
    {0xF0, 0x90, 0x90, 0x90, 0xF0}, {0x20, 0x60, 0x20, 0x20, 0x70},
    {0xF0, 0x10, 0xF0, 0x80, 0xF0}, {0xF0, 0x10, 0xF0, 0x10, 0xF0},
    {0x90, 0x90, 0xF0, 0x10, 0x10}, {0xF0, 0x80, 0xF0, 0x10, 0xF0},
    {0xF0, 0x80, 0xF0, 0x90, 0xF0}, {0xF0, 0x10, 0x20, 0x40, 0x40},
    {0xF0, 0x90, 0xF0, 0x90, 0xF0}, {0xF0, 0x90, 0xF0, 0x10, 0xF0},
    {0xF0, 0x90, 0xF0, 0x90, 0x90}, {0xE0, 0x90, 0xE0, 0x90, 0xE0},
    {0xF0, 0x80, 0x80, 0x80, 0xF0}, {0xE0, 0x90, 0x90, 0x90, 0xE0},
    {0xF0, 0x80, 0xF0, 0x80, 0xF0}, {0xF0, 0x80, 0xF0, 0x80, 0x80}};

// Stack 16 16-bit values
uint16_t stack[16];
uint8_t sp = 0;

// Opcode of current instruction
uint16_t opcode;

#define fetch() ((mem[pc] << 8) | mem[pc + 1])
#define xval ((opcode >> 8) & 0x0F)
#define vx registers[xval]
#define vy registers[((opcode >> 4) & 0x0F)]
#define vf registers[0xF]
#define v0 registers[0xF]
#define nn (0x00FF & opcode)
#define nnn (0x0FFF & opcode)

/* SDL object definition */
typedef struct {
  SDL_Window *win;
  SDL_Renderer *ren;
} sdl_object;

typedef struct {
  char *buffer;
  int size;
} Rom;

typedef struct {
  SDL_AudioSpec wavSpec;
  uint32_t wavLength;
  uint8_t *wavBuffer;
  SDL_AudioDeviceID deviceId;
} Audio;

sdl_object sdl;

int init_sdl();
void event_loop_sdl();
void destroy_sdl();
void cls_sdl();

/* */
pthread_t emulator_thread, timers_thread;
int init_emulator();
int init_timers();
void *emulator_loop(void *arg);
void *timers_loop(void *arg);
pthread_mutex_t emulator_mutex, keys_mutex;
pthread_cond_t keys_cond = PTHREAD_COND_INITIALIZER;
uint8_t waiting_keys = 0;
uint8_t delay_timer;
uint8_t sound_timer;

// Hex keyboard, 16 keys (0x0 to 0xF)
uint16_t keys_pressed;
/*
Layout
1	2	3	C
4	5	6	D
7	8	9	E
A	0	B	F
*/

static Rom *readFile(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(74);
  }

  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);

  char *buffer = (char *)malloc(fileSize);
  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    exit(74);
  }

  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  if (bytesRead < fileSize) {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(74);
  }

  fclose(file);

  Rom *rom = (Rom *)malloc(sizeof(Rom));
  rom->buffer = buffer;
  rom->size = fileSize;

  return rom;
}

int runVm(Rom *rom) {
  /* Error flag */
  int err;

  Audio audio;

  /* Init sdl objects */
  if ((err = init_sdl(&audio))) {
    return err;
  }

  /* Init emulator */
  pthread_mutex_init(&emulator_mutex, NULL);
  pthread_mutex_init(&keys_mutex, NULL);
  if ((err = init_emulator(rom, &audio))) {
    return err;
  }

  /* Handle sdl events */
  event_loop_sdl(&sdl);

  /* Destroy sdl objects */
  destroy_sdl(&audio);

  /* Destroy mutex */
  pthread_mutex_destroy(&emulator_mutex);
  pthread_mutex_destroy(&keys_mutex);

  return 0;
}

int runFile(const char *path) {
  Rom *rom = readFile(path);
  int result = runVm(rom);
  free(rom->buffer);
  free(rom);
  return result;
}

int main(int argc, const char *argv[]) {
  if (argc == 2) {
    return runFile(argv[1]);
  } else {
    fprintf(stderr, "Usage: chip8vm [path]\n");
    exit(64);
  }
}

int init_sdl(Audio *audio) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
    printf("SDL_Init Error: %s\n", SDL_GetError());
    return 1;
  }
#if defined linux && SDL_VERSION_ATLEAST(2, 0, 8)
  // Disable compositor bypass
  if (!SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0")) {
    printf("SDL can not disable compositor bypass!\n");
    return 1;
  }
#endif
  sdl.win =
      SDL_CreateWindow("chip8vm", SDL_WINDOWPOS_UNDEFINED,
                       SDL_WINDOWPOS_UNDEFINED, 660, 320, SDL_WINDOW_SHOWN);
  if (sdl.win == NULL) {
    printf("SDL_CreateWindow Error: %s", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  sdl.ren = SDL_CreateRenderer(sdl.win, -1, SDL_RENDERER_ACCELERATED);
  if (sdl.ren == NULL) {
    SDL_DestroyWindow(sdl.win);
    printf("SDL_CreateRenderer Error: %s", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_AudioSpec wavSpec;
  uint32_t wavLength;
  uint8_t *wavBuffer;
  SDL_LoadWAV("beep.wav", &wavSpec, &wavBuffer, &wavLength);
  SDL_AudioDeviceID deviceId = SDL_OpenAudioDevice(NULL, 0, &wavSpec, NULL, 0);
  if (deviceId <= 0) {
    SDL_DestroyWindow(sdl.win);
    printf("SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }
  audio->wavSpec = wavSpec;
  audio->wavLength = wavLength;
  audio->wavBuffer = wavBuffer;
  audio->deviceId = deviceId;

  cls_sdl(sdl);
  return 0;
}

static inline void redraw_screen() {
  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 64; x++) {
      // Creat a rect at pos ( x, y ) that's 10 pixels wide and 10 pixels high.
      SDL_Rect r;
      r.x = x * 10;
      r.y = y * 10;
      r.w = 10;
      r.h = 10;

      if (screen[y][x]) {
        // White
        SDL_SetRenderDrawColor(sdl.ren, 255, 255, 255, 255);
      } else {
        // Black
        SDL_SetRenderDrawColor(sdl.ren, 0, 0, 0, 255);
      }

      // Render rect
      SDL_RenderFillRect(sdl.ren, &r);
    }
  }

  // Render the rect to the screen
  SDL_RenderPresent(sdl.ren);
}

static inline void set_key(SDL_Keycode k) {
  pthread_mutex_lock(&emulator_mutex);
  switch (k) {
  case SDLK_0:
    keys_pressed |= 1;
    break;
  case SDLK_1:
    keys_pressed |= 1 << 1;
    break;
  case SDLK_2:
    keys_pressed |= 1 << 2;
    break;
  case SDLK_3:
    keys_pressed |= 1 << 3;
    break;
  case SDLK_4:
    keys_pressed |= 1 << 4;
    break;
  case SDLK_5:
    keys_pressed |= 1 << 5;
    break;
  case SDLK_6:
    keys_pressed |= 1 << 6;
    break;
  case SDLK_7:
    keys_pressed |= 1 << 7;
    break;
  case SDLK_8:
    keys_pressed |= 1 << 8;
    break;
  case SDLK_9:
    keys_pressed |= 1 << 9;
    break;
  case SDLK_a:
    keys_pressed |= 1 << 10;
    break;
  case SDLK_b:
    keys_pressed |= 1 << 11;
    break;
  case SDLK_c:
    keys_pressed |= 1 << 12;
    break;
  case SDLK_d:
    keys_pressed |= 1 << 13;
    break;
  case SDLK_e:
    keys_pressed |= 1 << 14;
    break;
  case SDLK_f:
    keys_pressed |= 1 << 15;
    break;
  }
  pthread_mutex_unlock(&emulator_mutex);
}

static inline void clear_key(SDL_Keycode k) {
  pthread_mutex_lock(&emulator_mutex);
  switch (k) {
  case SDLK_0:
    keys_pressed &= 0xFFFE;
    break;
  case SDLK_1:
    keys_pressed &= 0xFFFD;
    break;
  case SDLK_2:
    keys_pressed &= 0xFFFB;
    break;
  case SDLK_3:
    keys_pressed &= 0xFFF7;
    break;
  case SDLK_4:
    keys_pressed &= 0xFFEF;
    break;
  case SDLK_5:
    keys_pressed &= 0xFFDF;
    break;
  case SDLK_6:
    keys_pressed &= 0xFFBF;
    break;
  case SDLK_7:
    keys_pressed &= 0xFF7F;
    break;
  case SDLK_8:
    keys_pressed &= 0xFEFF;
    break;
  case SDLK_9:
    keys_pressed &= 0xFDFF;
    break;
  case SDLK_a:
    keys_pressed &= 0xFBFF;
    break;
  case SDLK_b:
    keys_pressed &= 0xF7FF;
    break;
  case SDLK_c:
    keys_pressed &= 0xEFFF;
    break;
  case SDLK_d:
    keys_pressed &= 0xDFFF;
    break;
  case SDLK_e:
    keys_pressed &= 0xBFFF;
    break;
  case SDLK_f:
    keys_pressed &= 0x7FFF;
    break;
  }
  pthread_mutex_unlock(&emulator_mutex);
}

void event_loop_sdl() {
  SDL_Event e;
  bool quit = false;
  bool locked = false;
  while (!quit) {
    while (SDL_PollEvent(&e)) {
      if (!locked) {
        pthread_mutex_lock(&keys_mutex);
        locked = true;
      }
      if (e.type == SDL_QUIT) {
        quit = true;
      }
      if (e.type == SDL_KEYUP) {
        clear_key(e.key.keysym.sym);
      }
      if (e.type == SDL_KEYDOWN) {
        if (waiting_keys) {
          pthread_mutex_unlock(&keys_mutex);
          pthread_cond_wait(&keys_cond, &keys_mutex);
          pthread_mutex_lock(&keys_mutex);
          waiting_keys = 0;
        }
        set_key(e.key.keysym.sym);
        locked = false;
        pthread_mutex_unlock(&keys_mutex);
      }
    }
  }
}

void destroy_sdl(Audio *audio) {
  SDL_CloseAudioDevice(audio->deviceId);
  SDL_FreeWAV(audio->wavBuffer);
  SDL_DestroyRenderer(sdl.ren);
  SDL_DestroyWindow(sdl.win);
  SDL_Quit();
}

void cls_sdl() {
  /* Select the color for drawing. It is set to red here. */
  SDL_SetRenderDrawColor(sdl.ren, 0, 0, 0, 255);
  /* Clear the entire screen to our selected color. */
  SDL_RenderClear(sdl.ren);
  /* Up until now everything was drawn behind the scenes.
  This will show the new, red contents of the window. */
  SDL_RenderPresent(sdl.ren);
  /* Give us time to see the window. */
}

int init_emulator(Rom *rom, Audio *audio) {
  for (int i = 0; i < 16; i++) {
    for (int j = 0; j < 5; j++) {
      mem[i * 5 + j] = bcd_sprites[i][j];
    }
  }
  /*creates a new thread with default attributes and NULL passed as the argument
   * to the start routine*/
  int err;
  /*check whether the thread creation was successful*/
  if ((err = pthread_create(&emulator_thread, NULL, emulator_loop, rom))) {
    printf("pthread_create error creating emulator thread\n");
    return err;
  }
  if ((err = init_timers(audio))) {
    return err;
  }
  return 0;
}

int init_timers(Audio *audio) {
  delay_timer = 0;
  sound_timer = 0;
  int err;
  if ((err = pthread_create(&timers_thread, NULL, timers_loop, audio))) {
    printf("pthread_create error creating emulator timers thread\n");
    return err;
  }
  return 0;
}

void *timers_loop(void *arg) {
  Audio *audio = arg;
  int queued = 0;
  while (1) {
    pthread_mutex_lock(&emulator_mutex);
    if (delay_timer > 0)
      delay_timer--;
    if (sound_timer == 0) {
      SDL_PauseAudioDevice(audio->deviceId, 1);
      if (!queued) {
        int err;
        if ((err = SDL_QueueAudio(audio->deviceId, audio->wavBuffer,
                                  audio->wavLength))) {
          printf("SDL_QueueAudio failed: %s\n", SDL_GetError());
        }
        queued = 1;
      }
    }
    if (sound_timer > 0) {
      if (queued) {
        SDL_PauseAudioDevice(audio->deviceId, 0);
        queued = 0;
      }
      sound_timer--;
    }
    pthread_mutex_unlock(&emulator_mutex);
    usleep(16667);
  }
}

static inline uint64_t shift_right(uint64_t val, uint8_t shift) {
  while (shift > 32) {
    val = val << 32;
    shift -= 32;
  }
  return val << shift;
}

void decode_execute() {
  // switch on first nibble
  switch (opcode >> 12) {
  case 0x0:
    if (opcode == 0x00E0) {
      cls_sdl();
    } else if (opcode == 0x00EE) {
      --sp;
      pc = stack[sp];
    } else {
      // not implemented
      exit(1);
    }
    pc += 2;
    break;
  case 0x1:
    // goto
    pc = nnn;
    break;
  case 0x2:
    stack[sp] = pc;
    ++sp;
    pc = nnn;
    break;
  case 0x3:
    // if (Vx==NN) skip the next instruction
    if (vx == nn) {
      pc += 2;
    }
    pc += 2;
    break;
  case 0x4:
    // if (Vx!=NN) skip the next instruction
    if (vx != nn) {
      pc += 2;
    }
    pc += 2;
    break;
  case 0x5:
    // if (Vx==Vy) skip the next instruction
    if (vx == vy) {
      pc += 2;
    }
    pc += 2;
    break;
  case 0x6:
    vx = nn;
    pc += 2;
    break;
  case 0x7:
    vx += nn;
    pc += 2;
    break;
  case 0x8:
    switch (opcode & 0x0F) {
    case 0x0:
      vx = vy;
      break;
    case 0x1:
      vx |= vy;
      break;
    case 0x2:
      vx &= vy;
      break;
    case 0x3:
      vx ^= vy;
      break;
    case 0x4: {
      int ow = (((int)vx + (int)vy) > 0xFF);
      vf = ow;
      vx += vy;
      break;
    }
    case 0x5:
      vf = vx >= vy;
      vx -= vy;
      break;
    case 0x6:
      vf = vy & 1;
      vx = vy >> 1;
      break;
    case 0x7:
      vf = vy >= vx;
      vx = vy - vx;
      break;
    case 0xE:
      vf = vy >> 7;
      vx = vy << 1;
      break;
    default:
      // TODO: handle undefined op
      break;
    }
    pc += 2;
    break;
  case 0x9:
    // if (Vx!=Vy) skip the next instruction
    if (vx != vy) {
      pc += 2;
    }
    pc += 2;
    break;
  case 0xA:
    I = nnn;
    pc += 2;
    break;
  case 0xB:
    pc = v0 + nnn;
    break;
  case 0xC: {
    time_t t;
    srand((unsigned)time(&t));
    vx = rand() & nn;
    pc += 2;
    break;
  }
  case 0xD: {
    int x = vx % 64;
    vf = 0;
    for (int i = 0; i < (opcode & 0xF); i++) {
      int y = (vy + i) % 32;
      uint8_t sprite = mem[I + i];
      for (int j = 0; j < 8; j++) {
        uint8_t sprite_bit = ((sprite >> (8 - (j + 1))) & 1);
        uint8_t screen_bit = screen[y][x + j] & 1;
        uint8_t screen_bit_result = (sprite_bit ^ screen_bit);
        if (vf == 0) {
          vf = screen_bit && !screen_bit_result;
        }
        screen[y][(x + j) % 64] = screen_bit_result;
      }
    }
    redraw_screen();
  }
    pc += 2;
    break;
  case 0xE:
    switch (nn) {
    // EX9E - Skips the next instruction if the key stored
    // in VX is pressed.
    case 0x9E:
      pthread_mutex_lock(&emulator_mutex);
      if (((1 << vx) & keys_pressed)) {
        pc += 2;
      }
      pthread_mutex_unlock(&emulator_mutex);
      break;
    // EXA1 - Skips the next instruction if the key stored
    // in VX isn't pressed.
    case 0xA1:
      pthread_mutex_lock(&emulator_mutex);
      if (!((1 << vx) & keys_pressed)) {
        pc += 2;
      }
      pthread_mutex_unlock(&emulator_mutex);
      break;
    default:
      break;
    }
    pc += 2;
    break;
  case 0xF:
    switch (opcode & 0xFF) {
    // FX07 - Sets VX to the value of the delay timer
    case 0x07:
      pthread_mutex_lock(&emulator_mutex);
      vx = delay_timer;
      pthread_mutex_unlock(&emulator_mutex);
      break;
    // FX0A - A key press is awaited, and then stored in VX
    case 0x0A:
      pthread_mutex_lock(&keys_mutex);
      waiting_keys = 1;
      uint16_t aux = keys_pressed;
      pthread_cond_signal(&keys_cond);
      pthread_mutex_unlock(&keys_mutex);

      int8_t pressed = -1;
      while (aux > 0) {
        aux >>= 1;
        pressed++;
      }
      vx = pressed;

      break;
    case 0x15:
      pthread_mutex_lock(&emulator_mutex);
      delay_timer = vx;
      pthread_mutex_unlock(&emulator_mutex);
      break;
    case 0x18:
      pthread_mutex_lock(&emulator_mutex);
      sound_timer = vx;
      pthread_mutex_unlock(&emulator_mutex);
      break;
    case 0x1E:
      I = I + vx;
      break;
    case 0x29:
      I = vx * 5;
      break;
    case 0x33: {
      uint8_t temp;
      temp = vx;
      for (int i = 2; i >= 0; i--) {
        mem[I + i] = temp % 10;
        temp /= 10;
      }
    } break;
    case 0x55:
      for (int i = 0; i <= xval; i++) {
        mem[I + i] = registers[i];
      }
      I = I + xval + 1;
      break;
    case 0x65:
      for (int i = 0; i <= xval; i++) {
        registers[i] = mem[I + i];
      }
      I = I + xval + 1;
      break;
    default:
      // TODO: handle error
      break;
    }
    pc += 2;
    break;
  }
}

void *emulator_loop(void *arg) {
  Rom *rom = arg;
  for (int i = 0; i < rom->size; i++) {
    mem[0x200 + i] = rom->buffer[i];
  }
  while (1) {
    opcode = fetch();
    decode_execute();
    usleep(1200);
  }
}
