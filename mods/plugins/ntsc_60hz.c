/*
 * 60 Hz mode for Frontschweine (SLES-02767), the classic "P/N selector".
 *
 * Back in 2000 the PAL release was played through a selector patch whose
 * NTSC choice made libgpu's GetVideoMode() report MODE_NTSC. The graphics
 * library then programs the GPU for 60 Hz, while the game, which never asks
 * for the video mode itself, keeps its PAL tick rate of 2 ticks per VBlank:
 * 120 instead of 100 ticks per second. Battles step every 4 ticks, so they
 * run at 30 instead of 25 fps, evenly paced, and everything is 20% faster --
 * the speed players of that patch know. The runtime follows the GPU's video
 * standard, so VBlank timing and host pacing switch to 60 Hz on their own.
 *
 * Unlike the patch, only the display-mode word (GP1(08h)) gets NTSC; the
 * display range keeps libgpu's PAL programming and the presentation keeps
 * the PAL active region, so all 256 lines the game draws stay visible
 * instead of an NTSC television's 240.
 */
#include "cpu_state.h"
#include "mod_plugins.h"

#define PLUGIN_ID "frontschweine.ntsc-60hz"

#define FN_GET_VIDEO_MODE 0x8001E30Cu /* long GetVideoMode(void) */
/* Return address of the call in PutDispEnv that builds GP1(08h). */
#define RA_DISPLAY_MODE_WORD 0x8001B334u
#define MODE_NTSC 0u

static void hook_get_video_mode(CPUState* cpu, uint32_t address) {
    (void)address;
    if (cpu->gpr[31] != RA_DISPLAY_MODE_WORD || !psx_mod_function_can_return())
        return;
    cpu->gpr[2] = MODE_NTSC;
    psx_mod_function_return(cpu);
}

static void ntsc_60hz_activate(void) {
    psx_mod_set_display_layout_pal(1);
}

PSX_MOD_CONSTRUCTOR(frontschweine_register_ntsc_60hz_plugin) {
    (void)psx_mod_register_activation_plugin(PLUGIN_ID, ntsc_60hz_activate);
    (void)psx_mod_register_function_entry_plugin(PLUGIN_ID, FN_GET_VIDEO_MODE,
                                                 hook_get_video_mode);
}
