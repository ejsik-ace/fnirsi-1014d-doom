#include "ff.h"
#include "w_file.h"
#include "z_zone.h"
#include "display_lib.h"
#include "font_structs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct
{
    wad_file_t wad;
    FIL fil;
} fatfs_wad_file_t;

extern wad_file_class_t stdc_wad_file;
extern void fnirsi_show_error(const char *msg);

static wad_file_t *W_FatFS_OpenFile(char *path)
{
    FIL fil;
    FRESULT res = f_open(&fil, path, FA_READ);
    if (res != FR_OK)
    {
        res = f_open(&fil, "DOOM1.WAD", FA_READ);
        if (res != FR_OK)
        {
            res = f_open(&fil, "0:/DOOM1.WAD", FA_READ);
            if (res != FR_OK)
            {
                res = f_open(&fil, "\\DOOM1.WAD", FA_READ);
                if (res != FR_OK)
                {
                    res = f_open(&fil, "/DOOM1.WAD", FA_READ);
                    if (res != FR_OK)
                    {
                        fnirsi_show_error("DOOM1.WAD not found on SD card!");
                        return NULL;
                    }
                }
            }
        }
    }

    unsigned int len = (unsigned int)f_size(&fil);
    if (len == 0)
    {
        fnirsi_show_error("DOOM1.WAD file is 0 bytes!");
        return NULL;
    }

    byte *data = (byte *)malloc(len);
    if (!data)
    {
        fnirsi_show_error("Malloc failed: Out of RAM for DOOM1.WAD!");
        return NULL;
    }

    display_set_fg_color(0x00FFFFFF);
    display_text(20, 210, "Loading DOOM1.WAD into RAM...");

    const UINT chunk_size = 32768; // 32 KB chunks
    UINT total_read = 0;
    char prog_str[64];

    while (total_read < len)
    {
        UINT to_read = len - total_read;
        if (to_read > chunk_size)
        {
            to_read = chunk_size;
        }

        UINT br = 0;
        res = f_read(&fil, data + total_read, to_read, &br);
        if (res != FR_OK || br != to_read)
        {
            fnirsi_show_error("SD card read error (f_read)!");
            free(data);
            return NULL;
        }

        total_read += br;

        // Update progress on screen every ~256 KB or at end
        if ((total_read % (chunk_size * 8)) == 0 || total_read == len)
        {
            int pct = (int)((unsigned long long)total_read * 100 / len);
            snprintf(prog_str, sizeof(prog_str), "Progress: %d%% (%u / %u KB) ", pct, total_read / 1024, len / 1024);
            display_set_fg_color(0x0000FF00); // Green
            display_text(20, 240, prog_str);
        }
    }

    f_close(&fil);

    display_set_fg_color(0x0000FF00);
    display_text(20, 270, "OK: DOOM1.WAD loaded into RAM. Starting game!");

    wad_file_t *result = (wad_file_t *)malloc(sizeof(wad_file_t));
    if (!result)
    {
        free(data);
        return NULL;
    }
    result->file_class = &stdc_wad_file;
    result->mapped = data;
    result->length = len;

    return result;
}

static void W_FatFS_CloseFile(wad_file_t *wad)
{
    if (wad->mapped)
    {
        free(wad->mapped);
    }
    free(wad);
}

static size_t W_FatFS_Read(wad_file_t *wad, unsigned int offset, void *buffer, size_t buffer_len)
{
    if (wad->mapped != NULL)
    {
        memcpy(buffer, wad->mapped + offset, buffer_len);
        return buffer_len;
    }
    return 0;
}

wad_file_class_t stdc_wad_file = 
{
    W_FatFS_OpenFile,
    W_FatFS_CloseFile,
    W_FatFS_Read,
};
