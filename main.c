#include<stdio.h>
#include<stdint.h>
#include<stdbool.h>
#include<stdlib.h>
#include<time.h>
#include<pthread.h>
#include<SDL2/SDL.h>

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
    {0xF0, 0x90, 0x90, 0x90, 0xF0},
    {0x20, 0x60, 0x20, 0x20, 0x70},
    {0xF0, 0x10, 0xF0, 0x80, 0xF0},
    {0xF0, 0x10, 0xF0, 0x10, 0xF0},
    {0x90, 0x90, 0xF0, 0x10, 0x10},
    {0xF0, 0x80, 0xF0, 0x10, 0xF0},
    {0xF0, 0x80, 0xF0, 0x90, 0xF0},
    {0xF0, 0x10, 0x20, 0x40, 0x40},
    {0xF0, 0x90, 0xF0, 0x90, 0xF0},
    {0xF0, 0x90, 0xF0, 0x10, 0xF0},
    {0xF0, 0x90, 0xF0, 0x90, 0x90},
    {0xE0, 0x90, 0xE0, 0x90, 0xE0},
    {0xF0, 0x80, 0x80, 0x80, 0xF0},
    {0xE0, 0x90, 0x90, 0x90, 0xE0},
    {0xF0, 0x80, 0xF0, 0x80, 0xF0},
    {0xF0, 0x80, 0xF0, 0x80, 0x80}
};

// Stack 16 16-bit values
uint16_t stack[16];
uint8_t sp = 0;

// Opcode of current instruction
uint16_t opcode;

#define fetch() ((mem[pc] << 8) | mem[pc+1])
#define vx registers[((opcode >> 8) & 0x0F)]
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

static Rom *readFile(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
    {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char *buffer = (char *)malloc(fileSize);
    if (buffer == NULL)
    {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize)
    {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }

    fclose(file);

    Rom *rom = (Rom *)malloc(sizeof(Rom));
    rom->buffer = buffer;
    rom->size = fileSize;

    return rom;
}

int runVm(Rom *rom)
{
    /* Error flag */
    int err;

    /* Init emulator */
    pthread_mutex_init(&emulator_mutex, NULL);
    pthread_mutex_init(&keys_mutex, NULL);
    if ((err = init_emulator(rom))) {
        return err;
    }
    
    /* Init sdl objects */
    if ((err = init_sdl(&sdl))) {
        return err;
    }
    
    /* Handle sdl events */
    event_loop_sdl(&sdl);

    /* Destroy sdl objects */
    destroy_sdl(&sdl);

    /* Destroy mutex */
    pthread_mutex_destroy(&emulator_mutex);
    pthread_mutex_destroy(&keys_mutex);

    return 0;
}

int runFile(const char *path)
{
    Rom *rom = readFile(path);
    int result = runVm(rom);
    free(rom->buffer);
    free(rom);
    return result;
}

int main(int argc, const char *argv[])
{
    if (argc == 2)
    {
        return runFile(argv[1]);
    }
    else
    {
        fprintf(stderr, "Usage: chip8vm [path]\n");
        exit(64);
    }
}

int init_sdl()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0){
		//logdisabled:printf("SDL_Init Error: %s\n", SDL_GetError());
		return 1;
	}
    #if defined linux && SDL_VERSION_ATLEAST(2, 0, 8)
    // Disable compositor bypass
    if (!SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0"))
    {
        //logdisabled:printf("SDL can not disable compositor bypass!\n");
        return 1;
    }
    #endif
    sdl.win = SDL_CreateWindow("chip8vm", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 660, 320, SDL_WINDOW_SHOWN);
    if (sdl.win == NULL){
        //logdisabled:printf("SDL_CreateWindow Error: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    sdl.ren = SDL_CreateRenderer(sdl.win, -1, SDL_RENDERER_ACCELERATED);
    if (sdl.ren == NULL){
        SDL_DestroyWindow(sdl.win);
        //logdisabled:printf("SDL_CreateRenderer Error: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    cls_sdl(sdl);
    return 0;
}

static inline void redraw_screen()
{
    //logdisabled:printf("debug: redrawing screen\n");
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 64; x++) {            
            // Creat a rect at pos ( x, y ) that's 10 pixels wide and 10 pixels high.
            SDL_Rect r;
            r.x = x*10;
            r.y = y*10;
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

static inline void set_key(SDL_Keycode k)
{
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

static inline void clear_key(SDL_Keycode k)
{
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

void event_loop_sdl()
{
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

void destroy_sdl()
{
    SDL_DestroyRenderer(sdl.ren);
    SDL_DestroyWindow(sdl.win);
	SDL_Quit();
}

void cls_sdl()
{
    /* Select the color for drawing. It is set to red here. */
    SDL_SetRenderDrawColor(sdl.ren, 0, 0, 0, 255);
    /* Clear the entire screen to our selected color. */
    SDL_RenderClear(sdl.ren);
    /* Up until now everything was drawn behind the scenes.
    This will show the new, red contents of the window. */
    SDL_RenderPresent(sdl.ren);
    /* Give us time to see the window. */
}

int init_emulator(Rom *rom)
{
    /*creates a new thread with default attributes and NULL passed as the argument to the start routine*/
    int err;
    /*check whether the thread creation was successful*/
    if ((err = pthread_create(&emulator_thread, NULL, emulator_loop, rom))) {
        //logdisabled:printf("pthread_create error creating emulator thread\n");
        return err;
    }
    if ((err = init_timers())) {
        return err;
    }
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 5; j++) {
            mem[i*5+j] = bcd_sprites[i][j];
        }
    }
    return 0;
}

int init_timers()
{
    int err;
    if ((err = pthread_create(&timers_thread, NULL, timers_loop, NULL))) {
        //logdisabled:printf("pthread_create error creating emulator timers thread\n");
        return err;
    }
    delay_timer = 0;
    sound_timer = 0;
    return 0;
}

void *timers_loop(void *arg)
{
    usleep(16667);
    while (1) {
        pthread_mutex_lock(&emulator_mutex);
        if (delay_timer > 0) delay_timer--;
        if (sound_timer > 0) {
            if (sound_timer == 1) SDL_PauseAudio(1);
            sound_timer--;
        }
        pthread_mutex_unlock(&emulator_mutex);
        usleep(16667);
    }
}

static inline uint64_t shift_right(uint64_t val, uint8_t shift)
{
    while (shift > 32) {
        val = val << 32;
        shift -= 32;
    }
    return val << shift;
}

void decode_execute()
{
    // switch on first nibble
    switch (opcode >> 12) {
        case 0x0:
            if (((opcode >> 4) & 0xF) == 0xE) {
                switch (opcode & 0xF) {
                    case 0x0:
                        //logdisabled:printf("debug: clear screen\n");
                        cls_sdl();
                        break;
                    case 0xE:
                        --sp;
                        pc = stack[sp];
                        break;
                    
                    default:
                        // TODO: handle error
                        break;
                }
            } else {
                // TODO: handle error
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
                case 0x4:
                    vf = vx + vy > 0xFF;
                    vx += vy;
                    break;
                case 0x5:
                    vf = vx > vy;
                    vx -= vy;
                    break;
                case 0x6:
                    vf = vx & 1;
                    vx >>= 1;
                    break;
                case 0x7:
                    vf = vy > vx;
                    vx = vy - vx;
                    break;
                case 0xE:
                    vf = vx >> 7;
                    vx <<= 1;
                    break;
                default:
                    //TODO: handle undefined op
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
            I = nnn & 0xFFF;
            pc += 2;
            break;
        case 0xB:
            pc = ((v0 + nnn) & 0xFFF);
            break;
        case 0xC: {
            /* random number generator */
            time_t t;
            srand((unsigned) time(&t));
            vx = rand() && nn;
            pc += 2;
            break;
        }
        case 0xD:
            {
                //logdisabled:printf("debug: writing sprite rows=%d\n", opcode & 0xF);
                int x = vx % 64;
                vf = 0;
                for (int i = 0; i < (opcode & 0xF); i++) {
                    int y = (vy + i) % 32;
                    uint8_t sprite = mem[I+i];
                    // write each bit of sprite from screen[y][x] to screen[y][x+8-1]
                    for (int j = 0; j < 8; j++) {
                        uint8_t sprite_bit = ((sprite >> (8-(j+1))) & 1);
                        uint8_t screen_bit = screen[y][x+j] & 1;
                        uint8_t screen_bit_result = (sprite_bit ^ screen_bit);
                        if (vf == 0) {
                            vf = screen_bit && !screen_bit_result;
                        }
                        //logdisabled:printf("debug: writing to screen line=%d, col=%d, colWrapped=%d, bit=%d\n", y, x+j, (x+j)%64, screen_bit_result);
                        screen[y][(x+j)%64] = screen_bit_result;
                        //usleep(1000000);
                    }
                    /*
                    uint64_t screen_line = screen[y];
                    uint64_t screen_line_result = 0;
                    for (int j = 0; j < 64; j++) {
                        if (j >= x && j < x+8) {
                            int sprite_bit = ((sprite >> (8+x-j)) & 1);
                            int screen_bit = ((screen_line >> j) & 1);
                            int screen_bit_result = (sprite_bit ^ screen_bit);
                            // vf = bit erased?
                            vf = screen_bit && !screen_bit_result;
                            screen_line_result |= shift_right(screen_bit_result, j);
                        } else {
                            screen_line_result |= (screen_line & shift_right(1, j));
                        }
                    }
                    */
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
            switch (opcode & 0xF) {
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
                    // TODO: implement audio
                    sound_timer = vx;
                    pthread_mutex_unlock(&emulator_mutex);
                    break;
                case 0x1E:
                    I = I + vx;
                    break;
                case 0x29:
                    I = vx*5;
                    break;
                case 0x33:
                    {
                        uint8_t temp;
                        temp = vx;
                        for (int i = 2; i >= 0; i--) {
                            mem[I+i] = temp % 10;
                            temp /= 10;
                        }
                    }
                    break;
                case 0x55:
                    for (int i = 0; i < ((opcode >> 8) & 0x0F); i++) {
                        mem[I++] = registers[i];
                    }
                    break;
                case 0x65:
                    for (int i = 0; i < ((opcode >> 8) & 0x0F); i++) {
                        registers[i] = mem[I++];
                    }
                    break;
                default:
                    // TODO: handle error
                    break;
            }
            pc += 2;
            break;
    }
}

void *emulator_loop(void *arg)
{
    Rom *rom = arg;
    for (int i = 0; i < rom->size; i++) {
        mem[0x200+i] = rom->buffer[i];
    }
    while (1) {
        opcode = fetch();
        //logdisabled:printf("opcode: 0x%x - pc: 0x%x\n", opcode, pc);
        decode_execute();
        usleep(1200);
    }
}
