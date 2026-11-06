CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy

CFLAGS = -mcpu=arm926ej-s -marm -O2 -DNORMALUNIX -D_DEFAULT_SOURCE -I. -Wall -Wno-unused-variable -Wno-unused-function -Wno-char-subscripts -Wno-stringop-truncation -Wno-format
LDFLAGS = -T./fnirsi_1014d.ld -nostartfiles -specs=nosys.specs -lm -lc -lgcc

HW_SRCS = ccu_control.c clock_synthesizer.c display_control.c fpga_control.c timer.c uart.c sd_card_interface.c diskio.c ff.c ffunicode.c variables.c spi_control.c interrupt.c 1014D_fonts.c display_lib.c sin_cos_math.c icons.c

DOOM_SRCS = dummy.c am_map.c doomdef.c doomstat.c dstrings.c d_event.c d_items.c d_iwad.c d_loop.c d_main.c d_mode.c d_net.c f_finale.c f_wipe.c g_game.c hu_lib.c hu_stuff.c info.c i_cdmus.c i_endoom.c i_joystick.c i_scale.c i_sound.c i_system.c i_timer.c memio.c m_argv.c m_bbox.c m_cheat.c m_config.c m_controls.c m_fixed.c m_menu.c m_misc.c m_random.c p_ceilng.c p_doors.c p_enemy.c p_floor.c p_inter.c p_lights.c p_map.c p_maputl.c p_mobj.c p_plats.c p_pspr.c p_saveg.c p_setup.c p_sight.c p_spec.c p_switch.c p_telept.c p_tick.c p_user.c r_bsp.c r_data.c r_draw.c r_main.c r_plane.c r_segs.c r_sky.c r_things.c sha1.c sounds.c statdump.c st_lib.c st_stuff.c s_sound.c tables.c v_video.c wi_stuff.c w_checksum.c w_file.c w_main.c w_wad.c z_zone.c i_input.c i_video.c doomgeneric.c w_file_fatfs.c doomgeneric_fnirsi.c

OBJS = start.o $(HW_SRCS:.c=.o) $(DOOM_SRCS:.c=.o)

all: fnirsi_doom.bin fnirsi_1014d.bin

start.o: start.s
	$(CC) -x assembler-with-cpp -c -O0 -mcpu=arm926ej-s -marm start.s -o start.o

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

fnirsi_doom.elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o fnirsi_doom.elf

fnirsi_doom.bin: fnirsi_doom.elf
	$(OBJCOPY) -O binary fnirsi_doom.elf fnirsi_doom.bin
	chmod +x ./mksunxi
	./mksunxi fnirsi_doom.bin

fnirsi_1014d.bin: fnirsi_doom.bin
	chmod +x ./flashfilepacker
	./flashfilepacker -o fnirsi_1014d.bin -i fnirsi_doom.bin -l 0x8000

clean:
	rm -f *.o fnirsi_doom.elf fnirsi_doom.bin fnirsi_1014d.bin
