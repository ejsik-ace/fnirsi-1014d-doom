//----------------------------------------------------------------------------------------------------------------------------------
// doomgeneric_fnirsi.c - DoomGeneric HAL port & Entry point for FNIRSI 1014D
//
// Based on:
// - FNIRSI 1014D Firmware & Hardware Drivers by pecostm32 (https://github.com/pecostm32/FNIRSI_1014D_Firmware)
// - doomgeneric by ozkl (https://github.com/ozkl/doomgeneric)
//----------------------------------------------------------------------------------------------------------------------------------

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "doomgeneric.h"
#include "doomkeys.h"
#include "types.h"
#include "variables.h"
#include "statemachine.h"
#include "timer.h"
#include "uart.h"
#include "ccu_control.h"
#include "clock_synthesizer.h"
#include "display_control.h"
#include "display_lib.h"
#include "font_structs.h"
#include "fpga_control.h"
#include "interrupt.h"
#include "arm32.h"
#include "ff.h"

// Screen dimensions
#define LCD_WIDTH   800
#define LCD_HEIGHT  480

static uint16 *lcd_buffer = (uint16 *)maindisplaybuffer;

extern IRQHANDLERFUNCION interrupthandlers[];
extern FONTDATA font_0;

void _fini(void)
{
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

int mkdir(const char *path, int mode)
{
    (void)path;
    (void)mode;
    return 0;
}

//----------------------------------------------------------------------------------------------------------------------------------
// MMU & D-Cache Initialization for ARM926EJ-S
// Enables full hardware D-Cache & I-Cache at 600 MHz (bypassing DDR bus latency)
//----------------------------------------------------------------------------------------------------------------------------------
static uint32_t mmu_page_table[4096] __attribute__((aligned(16384)));

void arm32_mmu_setup(void)
{
    // 1. By default, map entire 4GB (all 4096 1MB sections) as Strongly-Ordered / Device memory:
    // C=0, B=0, AP=0b11 (full read/write), Section descriptor (0x12)
    // Formula: (i << 20) | (3 << 10) | 0x12 = (i << 20) | 0xC12
    for (uint32_t i = 0; i < 4096; i++)
    {
        mmu_page_table[i] = (i << 20) | (3 << 10) | 0x12;
    }

    // 2. Map 32 MB of DDR SDRAM (0x80000000 - 0x82000000):
    // Sections 0x800 to 0x81F
    // Cacheable (C=1) and Bufferable (B=1): 0x12 | (1 << 3) | (1 << 2) = 0x1E
    // (i << 20) | (3 << 10) | 0x1E = (i << 20) | 0xC1E
    for (uint32_t i = 0x800; i < 0x820; i++)
    {
        mmu_page_table[i] = (i << 20) | (3 << 10) | 0x1E;
    }

    // 3. Invalidate caches & TLB, configure MMU registers, and enable MMU + D-Cache + I-Cache
    asm volatile (
        "mov r0, #0\n"
        "mcr p15, 0, r0, c7, c7, 0\n"   // Invalidate both I-cache and D-cache
        "mcr p15, 0, r0, c8, c7, 0\n"   // Invalidate Unified TLB
        "mvn r0, #0\n"
        "mcr p15, 0, r0, c3, c0, 0\n"   // DACR = 0xFFFFFFFF (All domains Manager: no permission faults)
        "mcr p15, 0, %0, c2, c0, 0\n"   // TTBR0 = mmu_page_table
        "mrc p15, 0, r0, c1, c0, 0\n"   // Read CP15 Control Register (c1)
        "orr r0, r0, #0x0001\n"         // Bit 0: Enable MMU
        "orr r0, r0, #0x0004\n"         // Bit 2: Enable D-Cache
        "orr r0, r0, #0x1000\n"         // Bit 12: Enable I-Cache
        "mcr p15, 0, r0, c1, c0, 0\n"   // Write CP15 Control Register
        "nop\n"
        "nop\n"
        "nop\n"
        :
        : "r" (mmu_page_table)
        : "r0", "memory"
    );
}

static inline void arm32_dcache_clean(void)
{
    asm volatile (
        "1:\n"
        "mrc p15, 0, r15, c7, c10, 3\n"
        "bne 1b\n"
        "mov r0, #0\n"
        "mcr p15, 0, r0, c7, c10, 4\n" // Drain write buffer
        :
        :
        : "r0", "memory"
    );
}

void fnirsi_show_error(const char *msg)
{
    fpga_set_backlight_brightness(0x78);
    display_set_screen_buffer((uint16 *)maindisplaybuffer);
    display_set_dimensions(SCREEN_WIDTH, SCREEN_HEIGHT);
    display_set_font(&font_0);
    display_set_fg_color(0x00FF0000); // Red
    display_text(20, 320, "DOOM ERROR:");
    display_text(20, 350, msg);
    arm32_dcache_clean();
    while (1)
    {
    }
}

void DG_Init()
{
    // Clear display buffer to black right before game starts rendering
    memset((void *)maindisplaybuffer, 0, LCD_WIDTH * LCD_HEIGHT * sizeof(uint16));
    arm32_dcache_clean();
}

static int polled_this_frame = 0;
static uint32_t last_poll_ms = 0;

void DG_DrawFrame()
{
    polled_this_frame = 0; // Ready to poll hardware for the next frame

    // Doom buffer DG_ScreenBuffer is 640x400 uint32_t (RGBA8888)
    // Center it on 800x480 RGB565 LCD
    const int offset_x = (LCD_WIDTH - DOOMGENERIC_RESX) / 2;   // (800 - 640) / 2 = 80
    const int offset_y = (LCD_HEIGHT - DOOMGENERIC_RESY) / 2;  // (480 - 400) / 2 = 40

    for (int y = 0; y < DOOMGENERIC_RESY; y++)
    {
        uint32_t *src_row = &DG_ScreenBuffer[y * DOOMGENERIC_RESX];
        uint16 *dst_row = &lcd_buffer[(offset_y + y) * LCD_WIDTH + offset_x];

        for (int x = 0; x < DOOMGENERIC_RESX; x++)
        {
            uint32_t c = src_row[x];

            // Convert ARGB8888 / RGBA8888 to RGB565:
            // R[4:0] = (c >> 19) & 0x1F  -> bits 15..11
            // G[5:0] = (c >> 10) & 0x3F  -> bits 10..5
            // B[4:0] = (c >> 3)  & 0x1F  -> bits 4..0
            dst_row[x] = (uint16)(((c >> 19) << 11) | 
                                  (((c >> 10) & 0x3F) << 5) | 
                                  ((c >> 3) & 0x1F));
        }
    }

    // Clean D-Cache so LCD controller DMA sees the rendered pixels in physical DRAM
    arm32_dcache_clean();
}

void DG_SleepMs(uint32_t ms)
{
    if (ms > 0)
    {
        timer0_delay((uint32)ms);
    }
}

uint32_t DG_GetTicksMs()
{
    return (uint32_t)timer0_get_ticks();
}

//----------------------------------------------------------------------------------------------------------------------------------
// Robust Key Queue & State Machine for FNIRSI 1014D Front Panel Input
//----------------------------------------------------------------------------------------------------------------------------------
#define KEYQUEUE_SIZE 32
static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex = 0;

static void queue_key(int pressed, unsigned char key)
{
    unsigned int next = (s_KeyQueueWriteIndex + 1) % KEYQUEUE_SIZE;
    if (next != s_KeyQueueReadIndex)
    {
        s_KeyQueue[s_KeyQueueWriteIndex] = (pressed ? 0x100 : 0) | key;
        s_KeyQueueWriteIndex = next;
    }
}

#define MAX_ACTIVE_KEYS 8
typedef struct {
    unsigned char key;
    uint8_t ttl;
} key_state_t;

static key_state_t held_keys[MAX_ACTIVE_KEYS];

static unsigned char map_cmd_to_key(uint8 cmd)
{
    switch (cmd)
    {
        // Directional Navigation
        case UIC_BUTTON_NAV_UP:
        case UIC_ROTARY_TIME_ADD:
            return KEY_UPARROW;

        case UIC_BUTTON_NAV_DOWN:
        case UIC_ROTARY_TIME_SUB:
            return KEY_DOWNARROW;

        case UIC_BUTTON_NAV_LEFT:
        case UIC_ROTARY_SEL_SUB:
            return KEY_LEFTARROW;

        case UIC_BUTTON_NAV_RIGHT:
        case UIC_ROTARY_SEL_ADD:
            return KEY_RIGHTARROW;

        case UIC_BUTTON_NAV_OK:
            return KEY_ENTER;

        // Action Keys
        case UIC_BUTTON_RUN_STOP:
            return KEY_FIRE;      // Primary Fire (Ctrl)

        case UIC_BUTTON_AUTO:
            return KEY_USE;       // Use / Open Doors (Space)

        case UIC_BUTTON_MENU:
            return KEY_ESCAPE;    // Menu / Pause

        // Weapon Selection (F1 - F6)
        case UIC_BUTTON_F1:
            return '1';           // Fist / Chainsaw
        case UIC_BUTTON_F2:
            return '2';           // Pistol
        case UIC_BUTTON_F3:
            return '3';           // Shotgun
        case UIC_BUTTON_F4:
            return '4';           // Chaingun
        case UIC_BUTTON_F5:
            return '5';           // Rocket Launcher
        case UIC_BUTTON_F6:
            return '6';           // Plasma Rifle

        // Strafe via Channel Buttons / Channel knobs
        case UIC_BUTTON_CH1_ENABLE:
        case UIC_ROTARY_CH1_POS_SUB:
            return KEY_STRAFE_L;

        case UIC_BUTTON_CH2_ENABLE:
        case UIC_ROTARY_CH1_POS_ADD:
            return KEY_STRAFE_R;

        default:
            return 0;
    }
}

static void poll_hardware_input(void)
{
    uint8 cmd = uart1_receive_data();
    unsigned char k = map_cmd_to_key(cmd);

    // If a key was pressed / reported by hardware:
    if (k != 0)
    {
        int found = 0;
        for (int i = 0; i < MAX_ACTIVE_KEYS; i++)
        {
            if (held_keys[i].key == k)
            {
                // Refresh TTL (3 frames = ~85 ms)
                held_keys[i].ttl = 3;
                found = 1;
                break;
            }
        }
        if (!found)
        {
            // Add new active key and queue keydown event
            for (int i = 0; i < MAX_ACTIVE_KEYS; i++)
            {
                if (held_keys[i].key == 0)
                {
                    held_keys[i].key = k;
                    held_keys[i].ttl = 3;
                    queue_key(1, k);
                    break;
                }
            }
        }
    }

    // Age all active keys; if not refreshed by hardware, release them
    for (int i = 0; i < MAX_ACTIVE_KEYS; i++)
    {
        if (held_keys[i].key != 0)
        {
            if (held_keys[i].key != k)
            {
                if (held_keys[i].ttl > 0)
                {
                    held_keys[i].ttl--;
                }
                if (held_keys[i].ttl == 0)
                {
                    // TTL expired: queue keyup event and free slot
                    queue_key(0, held_keys[i].key);
                    held_keys[i].key = 0;
                }
            }
        }
    }
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
    uint32_t now = (uint32_t)timer0_get_ticks();
    if (now != last_poll_ms)
    {
        polled_this_frame = 0;
        last_poll_ms = now;
    }

    if (!polled_this_frame)
    {
        polled_this_frame = 1;
        poll_hardware_input();
    }

    if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex)
    {
        return 0; // Queue is empty
    }

    unsigned short keyData = s_KeyQueue[s_KeyQueueReadIndex];
    s_KeyQueueReadIndex = (s_KeyQueueReadIndex + 1) % KEYQUEUE_SIZE;

    *pressed = (keyData >> 8) & 1;
    *doomKey = (unsigned char)(keyData & 0xFF);
    return 1;
}

//----------------------------------------------------------------------------------------------------------------------------------
// Main Entry Point
//----------------------------------------------------------------------------------------------------------------------------------
int main(void)
{
    // Clear BSS
    memset(&BSS_START, 0, (size_t)(&BSS_END - &BSS_START));

    // Initialize clock system (CPU @ 600 MHz)
    sys_clock_init();

    // Setup external clock synthesizer for FPGA
    clock_synthesizer_setup();

    // Initialize MMU & Enable L1 Data Cache + Instruction Cache (35 FPS performance)
    arm32_mmu_setup();

    // Clear interrupt handlers
    memset(interrupthandlers, 0, 256);

    // Setup system timer interrupt
    timer0_setup();

    // CRITICAL: Enable ARM CPU interrupts so timer0 ticks advance!
    arm32_interrupt_enable();

    // Initialize FPGA & turn on backlight
    fpga_init();
    fpga_set_backlight_brightness(0x78);

    // Initialize Display controller (800x480 RGB565)
    sys_init_display(SCREEN_WIDTH, SCREEN_HEIGHT, (uint16 *)maindisplaybuffer);

    // Clear display to black
    memset((void *)maindisplaybuffer, 0, 800 * 480 * 2);

    display_set_screen_buffer((uint16 *)maindisplaybuffer);
    display_set_dimensions(SCREEN_WIDTH, SCREEN_HEIGHT);
    display_set_font(&font_0);

    display_set_fg_color(0x0000FF00); // Green
    display_text(20, 20, "FNIRSI 1014D - DOOM LOADER");

    display_set_fg_color(0x00FFFFFF); // White
    display_text(20, 60, "[1/3] Mounting SD card...");

    // Mount SD Card FAT32 partition
    FRESULT mres = f_mount(&fs, "0", 1);
    if (mres != FR_OK)
    {
        mres = f_mount(&fs, "", 1);
    }

    if (mres != FR_OK)
    {
        display_set_fg_color(0x00FF0000); // Red
        display_text(20, 90, "ERROR: Failed to mount SD card!");
        arm32_dcache_clean();
        while (1);
    }

    display_set_fg_color(0x0000FF00);
    display_text(20, 90, "OK: SD card mounted.");

    display_set_fg_color(0x00FFFFFF);
    display_text(20, 130, "[2/3] Initializing controls...");
    uart1_init();

    display_set_fg_color(0x00FFFFFF);
    display_text(20, 170, "[3/3] Initializing DOOM engine...");
    arm32_dcache_clean();

    // Start Doom!
    char *argv[] = {
        "doom",
        "-iwad",
        "DOOM1.WAD"
    };

    doomgeneric_Create(3, argv);

    // Game loop: tick Doom continuously
    while (1)
    {
        doomgeneric_Tick();
    }

    return 0;
}
