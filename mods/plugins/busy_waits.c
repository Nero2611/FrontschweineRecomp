/*
 * Busy waits without busy polling for Frontschweine (SLES-02767).
 *
 * The game waits in two polling loops whose timeouts count loop iterations,
 * not time. The CPU overclock runs those iterations faster while devices keep
 * stock timing, so the timeouts expire early, and every extra iteration costs
 * host time, more the higher the overclock:
 *
 *  - PsyQ libetc v_wait(target, timeout) polls the VSync counter, which the
 *    VBlank handler advances, and gives up after `timeout << 15` iterations.
 *    At 200% about every third frame timed out ("VSync: timeout", pad and
 *    root-counter callbacks reset, no wait), which made every scene stutter.
 *  - The movie player retries StGetNext up to 0x1FFFF times until the CD
 *    callback has put the next frame into the ring buffer; at 800% the intro
 *    ran at 17 fps.
 *
 * Neither loop can see progress before an interrupt, so both hooks let device
 * time run straight to the next observable event instead of polling, and keep
 * each timeout in guest time at the stock iteration cost. The plugin belongs
 * to the CPU overclock feature, the only setting under which these loops
 * misbehave.
 */
#include "cpu_state.h"
#include "mod_plugins.h"

#define PLUGIN_ID "frontschweine.busy-waits"

/* Guest cycles of one stock-speed iteration, rounded up so a timeout never
 * expires sooner than at stock speed. */
#define FRAME_CYCLES 677376u /* 33.8688 MHz / 50 Hz */

/* libetc v_wait. At 100% the game never hit the iteration timeout while
 * waiting a full frame, so one iteration costs more than 677376 / 32768. */
#define FN_V_WAIT              0x8001DA54u /* void v_wait(int target, int timeout) */
#define G_VSYNC_COUNTER        0x8002B1F0u /* advanced by the VBlank handler */
#define V_WAIT_ITERATION       32u

/* Movie player (loaded at runtime): `for (n = 0x1FFFF; n; n--)` around
 * StGetNext; s0 holds n, decremented after each failed call. */
#define FN_ST_GET_NEXT         0x800238A0u /* int StGetNext(u_long**, u_long**) */
#define MOVIE_RETRY_RA         0x8005BFA0u
#define MOVIE_RETRY_START      0x1FFFFu
#define ST_GET_NEXT_ITERATION  64u

static struct {
    int active;
    uint32_t sp, ra, target;
    uint64_t deadline;
} s_vsync;

static struct {
    int waited;
    uint32_t sp, count;
} s_movie;

static void hook_v_wait(CPUState* cpu, uint32_t address) {
    (void)address;
    if (!psx_mod_function_can_return()) return;
    const uint32_t target = cpu->gpr[4];
    const uint32_t timeout = cpu->gpr[5];
    const uint32_t sp = cpu->gpr[29], ra = cpu->gpr[31];
    const int resumed = s_vsync.active && s_vsync.sp == sp && s_vsync.ra == ra &&
                        s_vsync.target == target;

    if ((int32_t)psx_mod_read_word(G_VSYNC_COUNTER) >= (int32_t)target) {
        /* Register results of the original exit path. */
        if (resumed) cpu->gpr[3] = 0xFFFFFFFFu;
        cpu->gpr[2] = 0;
        cpu->gpr[5] = timeout << 15;
        s_vsync.active = 0;
        psx_mod_function_return(cpu);
        return;
    }

    const uint64_t now = psx_mod_guest_cycles();
    if (!resumed) {
        s_vsync.active = 1;
        s_vsync.sp = sp;
        s_vsync.ra = ra;
        s_vsync.target = target;
        s_vsync.deadline = now + (uint64_t)(timeout << 15) * V_WAIT_ITERATION;
    }
    const uint64_t left = s_vsync.deadline > now ? s_vsync.deadline - now : 0;
    if (!left) {
        /* Timed out in guest time: the game's own loop runs its timeout path. */
        s_vsync.active = 0;
        return;
    }
    if (!psx_mod_function_wait(cpu, left > FRAME_CYCLES ? FRAME_CYCLES : (uint32_t)left))
        s_vsync.active = 0;
}

/* The retry loop's code, checked because other modules reuse that RAM. */
static int movie_retry_loop_loaded(void) {
    return psx_mod_read_word(0x8005BF98u) == 0x0C008E28u && /* jal StGetNext */
           psx_mod_read_word(0x8005BFA0u) == 0x10400005u && /* beq v0, zero */
           psx_mod_read_word(0x8005BFA4u) == 0x2610FFFFu && /* addiu s0, s0, -1 */
           psx_mod_read_word(0x8005BFA8u) == 0x1600FFFBu;   /* bne s0, zero */
}

static void hook_st_get_next(CPUState* cpu, uint32_t address) {
    (void)address;
    const uint32_t count = cpu->gpr[16], sp = cpu->gpr[29];
    /* Only retries: the first call of a wait must see the buffer as it is. */
    if (cpu->gpr[31] != MOVIE_RETRY_RA || count >= MOVIE_RETRY_START || count < 2 ||
        !psx_mod_function_can_return() || !movie_retry_loop_loaded())
        return;
    if (s_movie.waited && s_movie.sp == sp && s_movie.count == count) {
        /* Device time has reached the next event: let StGetNext look. */
        s_movie.waited = 0;
        return;
    }
    const uint32_t cycles = psx_mod_function_wait(cpu, FRAME_CYCLES);
    if (!cycles) return;
    /* Iterations the stock loop would have spent meanwhile; stop short of
     * zero so the game's own exit branch still runs. */
    uint32_t spent = cycles / ST_GET_NEXT_ITERATION;
    if (spent > count - 1u) spent = count - 1u;
    cpu->gpr[16] = count - spent;
    s_movie.waited = 1;
    s_movie.sp = sp;
    s_movie.count = cpu->gpr[16];
}

PSX_MOD_CONSTRUCTOR(frontschweine_register_busy_waits_plugin) {
    (void)psx_mod_register_function_entry_plugin(PLUGIN_ID, FN_V_WAIT, hook_v_wait);
    (void)psx_mod_register_function_entry_plugin(PLUGIN_ID, FN_ST_GET_NEXT,
                                                 hook_st_get_next);
}
