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
uint16_t pc;

// I register
uint16_t I;

// Screen 64x32 pixels monochrome (1bit each pixel)
uint32_t screen[64];

// Stack 16 16-bit values
uint16_t stack[16];

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
pthread_mutex_t emulator_mutex;
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

int main(void)
{
    /* Intializes random number generator */
    time_t t;
    srand((unsigned) time(&t));

    /* Error flag */
    int err;

    /* Init emulator */
    if ((err = init_emulator())) {
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

    return 0;
}

int init_sdl()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0){
		printf("SDL_Init Error: %s\n", SDL_GetError());
		return 1;
	}
    #if defined linux && SDL_VERSION_ATLEAST(2, 0, 8)
    // Disable compositor bypass
    if (!SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0"))
    {
        printf("SDL can not disable compositor bypass!\n");
        return 1;
    }
    #endif
    sdl.win = SDL_CreateWindow("chip8vm", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 320, SDL_WINDOW_SHOWN);
    if (sdl.win == NULL){
        printf("SDL_CreateWindow Error: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    sdl.ren = SDL_CreateRenderer(sdl.win, -1, SDL_RENDERER_ACCELERATED);
    if (sdl.ren == NULL){
        SDL_DestroyWindow(sdl.win);
        printf("SDL_CreateRenderer Error: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    cls_sdl(sdl);
    return 0;
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
            keys_pressed |= 1 << 10;
            break;
        case SDLK_c:
            keys_pressed |= 1 << 10;
            break;
        case SDLK_d:
            keys_pressed |= 1 << 10;
            break;
        case SDLK_e:
            keys_pressed |= 1 << 10;
            break;
        case SDLK_f:
            keys_pressed |= 1 << 10;
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
    while (!quit){
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
            }
            if (e.type == SDL_KEYDOWN) {
                set_key(e.key.keysym.sym);
            }
            if (e.type == SDL_KEYUP) {
                clear_key(e.key.keysym.sym);
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

int init_emulator()
{
    /*creates a new thread with default attributes and NULL passed as the argument to the start routine*/
    int err;
    /*check whether the thread creation was successful*/
    if ((err = pthread_create(&emulator_thread, NULL, emulator_loop, NULL))) {
        printf("pthread_create error creating emulator thread\n");
        return err;
    }
    if ((err = init_timers())) {
        return err;
    }
    pthread_mutex_init(&emulator_mutex, NULL);
    return 0;
}

int init_timers()
{
    int err;
    if ((err = pthread_create(&timers_thread, NULL, timers_loop, NULL))) {
        printf("pthread_create error creating emulator timers thread\n");
        return err;
    }
    delay_timer = 0;
    sound_timer = 0;
    return 0;
}

void *timers_loop(void *arg)
{
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

void *emulator_loop(void *arg)
{
    while (1) {
        opcode = fetch();
        //decode_execute();
        printf("emulator_loop\n");
        sleep(1);
    }
}

void decode_execute()
{
    // switch on first nibble
    switch (opcode >> 12) {
        case 0x0:
            if (((opcode >> 4) & 0xF) == 0xE) {
                switch (opcode & 0xF) {
                    case 0x0:
                        cls_sdl();
                        break;
                    case 0xE:
                        /*
                        RET
                        Return from a subroutine.
                        The interpreter sets the program counter to the address at the top of the stack, then subtracts 1 from the stack pointer.
                        */
                        break;
                    
                    default:
                        // TODO: handle error
                        break;
                }
            } else {
                // TODO: handle error
            }
            break;
        case 0x1:
            // goto
            pc = nnn;
            break;
        case 0x2:
            // call
            break;
        case 0x3:
            // if (Vx==NN) skip the next instruction
            if (vx == nn) {
                pc += 2;
            }
            break;
        case 0x4:
            // if (Vx!=NN) skip the next instruction
            if (vx != nn) {
                pc += 2;
            }
            break;
        case 0x5:
            // if (Vx==Vy) skip the next instruction
            if (vx == vy) {
                pc += 2;
            }
            break;
        case 0x6:
            vx = nn;
            break;
        case 0x7:
            vx += nn;
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
                    vf = vx && 1;
                    vx >>= 1;
                    break;
                case 0x7:
                    vf = vy > vx;
                    vx = vy - vx;
                    break;
                case 0xE:
                    vf = vx && 1;
                    vx <<= 1;
                    break;
                default:
                    //TODO: handle undefined op
                    break;
            }
            break;
        case 0x9:
            // if (Vx!=Vy) skip the next instruction
            if (vx != vy) {
                pc += 2;
            }
            break;
        case 0xA:
            I = nnn & 0xFFF;
            break;
        case 0xB:
            pc = (v0 + nnn) & 0xFFF;
            break;
        case 0xC:
            vx = rand() && nn;
            break;
        case 0xD:
            break;
        case 0xE:
            pthread_mutex_lock(&emulator_mutex);
            if (((1 << vx) | keys_pressed)) {
                pc += 2;
            }
            pthread_mutex_unlock(&emulator_mutex);
            break;
        case 0xF:
            switch (opcode & 0xF) {
                case 0x07:
                    pthread_mutex_lock(&emulator_mutex);
                    vx = delay_timer;
                    pthread_mutex_unlock(&emulator_mutex);
                    break;
                case 0x0A:
                    while (1) {
                        pthread_mutex_lock(&emulator_mutex);
                        if (keys_pressed) {
                            int8_t pressed = -1;
                            uint16_t aux = keys_pressed;
                            while (aux > 0) {
                                aux >>= 1;
                                pressed++;
                            }
                            vx = pressed;
                            break;
                        }
                        pthread_mutex_unlock(&emulator_mutex);
                    }
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
                    break;
                case 0x33:
                    break;
                case 0x55:
                    int i;
                    for (i = 0; i < ((opcode >> 8) & 0x0F); i++) {
                        mem[I+i] = registers[i];
                    }
                    break;
                case 0x65:
                    int i;
                    for (i = 0; i < ((opcode >> 8) & 0x0F); i++) {
                        registers[i] = mem[I+i];
                    }
                    break;
                default:
                    // TODO: handle error
                    break;
            }
            break;
    }
}
