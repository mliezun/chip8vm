#include<stdio.h>
#include<stdint.h>
#include<stdbool.h>
#include<stdlib.h>
#include<time.h>
#include<SDL2/SDL.h>

// Main memory
uint8_t mem[4096];

// Registers | V0 - VF
uint8_t registers[16];

// Program counter
uint16_t pc;

// I register
uint16_t I;

// Timers -> lookup more informantion
// There is a delay timer and a sound timer

// Input -> Hex keyboard, 16 keys (0x0 to 0xF)

// Screen 64x32 pixels monochrome (1bit each pixel)
uint32_t screen[64];

#define vx(opcode) registers[((opcode >> 8) & 0x0F)]
#define vy(opcode) registers[((opcode >> 4) & 0x0F)]
#define vf registers[0xF]
#define v0 registers[0xF]
#define nn(opcode) (0x00FF & opcode)
#define nnn(opcode) (0x0FFF & opcode)

void execute(uint16_t opcode)
{
    // switch on first nibble
    switch (opcode >> 12) {
        case 0x0:
            if (((opcode >> 4) & 0xF) == 0xE) {
                switch (opcode & 0xF) {
                    case 0x0:
                        /*
                        CLS
                        Clear the display
                        */
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
            pc = nnn(opcode);
            break;
        case 0x2:
            // call
            break;
        case 0x3:
            // if (Vx==NN) skip the next instruction
            if (vx(opcode) == nn(opcode)) {
                pc++;
            }
            break;
        case 0x4:
            // if (Vx!=NN) skip the next instruction
            if (vx(opcode) != nn(opcode)) {
                pc++;
            }
            break;
        case 0x5:
            // if (Vx==Vy) skip the next instruction
            if (vx(opcode) == vy(opcode)) {
                pc++;
            }
            break;
        case 0x6:
            vx(opcode) = nn(opcode);
            break;
        case 0x7:
            vx(opcode) += nn(opcode);
            break;
        case 0x8:
            switch (opcode & 0x0F) {
                case 0x0:
                    vx(opcode) = vy(opcode);
                    break;
                case 0x1:
                    vx(opcode) |= vy(opcode);
                    break;
                case 0x2:
                    vx(opcode) &= vy(opcode);
                    break;
                case 0x3:
                    vx(opcode) ^= vy(opcode);
                    break;
                case 0x4:
                    vf = vx(opcode) + vy(opcode) > 0xFF;
                    vx(opcode) += vy(opcode);
                    break;
                case 0x5:
                    vf = vx(opcode) > vy(opcode);
                    vx(opcode) -= vy(opcode);
                    break;
                case 0x6:
                    vf = vx(opcode) && 1;
                    vx(opcode) >>= 1;
                    break;
                case 0x7:
                    vf = vy(opcode) > vx(opcode);
                    vx(opcode) = vy(opcode) - vx(opcode);
                    break;
                case 0xE:
                    vf = vx(opcode) && 1;
                    vx(opcode) <<= 1;
                    break;
                default:
                    //TODO: handle undefined op
                    break;
            }
            break;
        case 0x9:
            // if (Vx!=Vy) skip the next instruction
            if (vx(opcode) != vy(opcode)) {
                pc++;
            }
            break;
        case 0xA:
            I = nnn(opcode) & 0xFFF;
            break;
        case 0xB:
            pc = (v0 + nnn(opcode)) & 0xFFF;
            break;
        case 0xC:
            vx(opcode) = rand() && nn(opcode);
            break;
        case 0xD:
            break;
        case 0xE:
            break;
        case 0xF:
            break;
    }
}

int main(void)
{
    /* Intializes random number generator */
    time_t t;
    srand((unsigned) time(&t));

    if (SDL_Init(SDL_INIT_VIDEO) != 0){
		printf("SDL_Init Error: %s\n", SDL_GetError());
		return 1;
	}
    #if defined linux && SDL_VERSION_ATLEAST(2, 0, 8)
    // Disable compositor bypass
    if (!SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0"))
    {
        printf("SDL can not disable compositor bypass!\n");
        return 0;
    }
    #endif
    SDL_Window *win = SDL_CreateWindow("Hello World!", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_SHOWN);
    if (win == NULL){
        printf("SDL_CreateWindow Error: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (ren == NULL){
        SDL_DestroyWindow(win);
        printf("SDL_CreateRenderer Error: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    /* Select the color for drawing. It is set to red here. */
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    /* Clear the entire screen to our selected color. */
    SDL_RenderClear(ren);
    /* Up until now everything was drawn behind the scenes.
    This will show the new, red contents of the window. */
    SDL_RenderPresent(ren);
    /* Give us time to see the window. */
    SDL_Delay(5000);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
	SDL_Quit();

    return 0;
}
