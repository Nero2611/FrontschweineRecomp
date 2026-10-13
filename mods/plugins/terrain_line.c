/*
 * Native terrain line walk for Frontschweine (SLES-02767).
 *
 * The battle module (LOAD/L640V2.MGL, loaded at 0x8005B3A0) rates a straight
 * line between two map positions with a leaf routine at 0x8006AD70. It steps
 * along the major axis one map unit (1/512 tile) at a time, Bresenham style,
 * and looks at every tile the line enters in the 50-tile-wide terrain table at
 * 0x80078668:
 *
 *  - a tile whose low byte is set and whose bit 15 is clear blocks the line,
 *  - every entered tile adds 4 to the cost if bit 14 is set, else 1,
 *  - a line longer than two tiles that is almost axis-aligned (minor extent
 *    below 1/8 of the major one) also counts as blocked.
 *
 * It returns the cost, negated when the line is blocked.
 *
 * The enemy AI calls it for every candidate move and target while it thinks.
 * A line across the map costs some ten thousand loop iterations of guest code,
 * so a single decision runs for hundreds of milliseconds of emulated CPU time,
 * during which the game draws no new frame. This hook computes the same walk
 * natively, register for register, so the AI decides in the same frame.
 * Results and every register and memory effect of the original are kept; only
 * the emulated CPU time the loop took disappears.
 */
#include "cpu_state.h"
#include "mod_plugins.h"

#define PLUGIN_ID "frontschweine.terrain-line"

#define FN_TERRAIN_LINE  0x8006AD70u
#define TERRAIN_LINE_WORDS 278u        /* 1112 bytes, up to the jr ra delay slot */
#define TERRAIN_LINE_FNV 0xFCD3223Du   /* FNV-1a over those words */
#define TERRAIN_TABLE    0x80078668u   /* lui 0x8008 + addiu -31128 */

/* Other modules reuse this RAM: only take over the exact routine. */
static int terrain_line_loaded(void) {
    uint32_t hash = 0x811C9DC5u;
    for (uint32_t i = 0; i < TERRAIN_LINE_WORDS; i++) {
        hash ^= psx_mod_read_word(FN_TERRAIN_LINE + 4u * i);
        hash *= 0x01000193u;
    }
    return hash == TERRAIN_LINE_FNV;
}

static uint32_t sra(uint32_t value, unsigned shift) {
    return (uint32_t)((int32_t)value >> shift);
}

static int slt(uint32_t a, uint32_t b) {
    return (int32_t)a < (int32_t)b;
}

/* Address of the table entry for a tile row (already biased by -7, as the
 * guest computes it) and column: 50 entries per row, minus 7. */
static uint32_t tile_address(uint32_t row_minus_7, uint32_t column) {
    return ((row_minus_7 * 50u - 7u + column) << 1) + TERRAIN_TABLE;
}

static void hook_terrain_line(CPUState* cpu, uint32_t address) {
    (void)address;
    if (!psx_mod_function_can_return() || !terrain_line_loaded()) return;

    uint32_t* r = cpu->gpr;
    uint32_t v0, v1, a0, a1 = r[5], a2 = r[6], a3 = r[7];
    uint32_t t0, t1, t2, t3, t4, t5, t6, t7, t8, t9;
    uint32_t s0 = r[16];
    const uint32_t sp = r[29] - 8u;
    uint16_t tile;

    t6 = a1;
    t0 = a3 - t6;
    a0 = t0;
    t5 = psx_mod_read_word(sp + 24u);
    t7 = 0;
    psx_mod_write_word(sp, s0);
    v1 = t5 - a2;
    a1 = v1;
    if (slt(a0, 0)) a0 = 0u - a0;
    v0 = slt(a0, 1025);
    if (slt(a1, 0)) a1 = 0u - a1;
    if (!v0) {
        v0 = slt(a0, 0) ? a0 + 7u : a0;
        if (slt(a1, sra(v0, 3))) t7 = 1;
    }
    v0 = slt(a1, 1025);
    if (!v0) {
        v0 = slt(a1, 0) ? a1 + 7u : a1;
        if (slt(a0, sra(v0, 3))) t7 = 1;
    }
    v0 = slt(a1, a0);

    if (v0) {
        /* X is the major axis. */
        v0 = slt(a2, t5);
        if (slt(t0, 0)) {
            v0 = t6; t6 = a3; a3 = v0;
            v1 = a2; a2 = t5; t5 = v1;
            v0 = slt(a2, t5);
        }
        t9 = v0 ? 1u : 0xFFFFFFFFu;
        t8 = a1 << 1;
        t4 = t8 - a0;
        t3 = t6;
        t2 = a2;
        t5 = sra(t2, 9);
        a1 = a1 - a0;
        t0 = 0x80080000u;
        a0 = TERRAIN_TABLE;
        v1 = t5 - 7u;
        v0 = tile_address(v1, sra(t3, 9));
        v1 = sra(t3, 9);
        tile = psx_mod_read_half(v0);
        v0 = tile;
        v1 = v0 & 0xFFu;
        s0 = a1 << 1;
        if (v1) {
            v0 &= 0x8000u;
            if (!v0) t7 = 1;
        }
        a2 = sra(a2, 9);
        v0 = a2 - 7u;
        t0 = sra(t6, 9);
        v1 = tile_address(v0, t0);
        v0 = (uint32_t)psx_mod_read_half(v1) & 0x4000u;
        t1 = v0 ? 4u : 1u;
        a1 = t0;
        v0 = slt(t6, a3);
        v1 = a2;
        while (v0) {
            if (!slt(t4, 0)) {
                t4 += s0;
                t3 += 1u;
                t2 += t9;
                t5 = sra(t2, 9);
            } else {
                t4 += t8;
                t3 += 1u;
            }
            t0 = sra(t3, 9);
            a2 = t0;
            if (t0 == a1) {
                v0 = slt(t3, a3);
                if (t5 == v1) continue;
            }
            v1 = tile_address(t5 - 7u, t0);
            v1 = psx_mod_read_half(v1);
            v0 = v1 & 0xFFu;
            if (v0) {
                v0 = v1 & 0x8000u;
                if (!v0) t7 = 1;
            } else {
                v0 = v1 & 0x8000u;
            }
            v0 = t5 - 7u;
            v1 = tile_address(v0, a2);
            v0 = (uint32_t)psx_mod_read_half(v1) & 0x4000u;
            t1 += v0 ? 4u : 1u;
            a1 = a2;
            v1 = t5;
            v0 = slt(t3, a3);
        }
    } else {
        /* Z is the major axis (also for equal extents). */
        v0 = slt(t6, a3);
        if (slt(v1, 0)) {
            v0 = t6; t6 = a3; a3 = v0;
            v1 = a2; a2 = t5; t5 = v1;
            v0 = slt(t6, a3);
        }
        t9 = v0 ? 1u : 0xFFFFFFFFu;
        t8 = a0 << 1;
        t4 = t8 - a1;
        t3 = t6;
        t2 = a2;
        t0 = sra(t3, 9);
        a1 = a0 - a1;
        a3 = 0x80080000u;
        a0 = TERRAIN_TABLE;
        v1 = sra(t2, 9) - 7u;
        v0 = tile_address(v1, t0);
        tile = psx_mod_read_half(v0);
        v0 = tile;
        v1 = v0 & 0xFFu;
        s0 = a1 << 1;
        if (v1) {
            v0 &= 0x8000u;
            if (!v0) t7 = 1;
        }
        a3 = sra(a2, 9);
        v0 = a3 - 7u;
        a1 = sra(t6, 9);
        v1 = tile_address(v0, a1);
        v0 = (uint32_t)psx_mod_read_half(v1) & 0x4000u;
        t1 = v0 ? 4u : 1u;
        v0 = slt(a2, t5);
        v1 = a3;
        while (v0) {
            if (slt(0, t4)) {
                t4 += s0;
                t2 += 1u;
                t3 += t9;
                t0 = sra(t3, 9);
            } else {
                t4 += t8;
                t2 += 1u;
            }
            a2 = sra(t2, 9);
            if (t0 == a1) {
                v0 = sra(t2, 9);
                if (v0 == v1) {
                    v0 = slt(t2, t5);
                    continue;
                }
                a2 = sra(t2, 9);
            }
            v1 = tile_address(a2 - 7u, t0);
            v1 = psx_mod_read_half(v1);
            v0 = v1 & 0xFFu;
            if (v0) {
                v0 = v1 & 0x8000u;
                if (!v0) t7 = 1;
            } else {
                v0 = v1 & 0x8000u;
            }
            v0 = a2 - 7u;
            v1 = tile_address(v0, t0);
            v0 = (uint32_t)psx_mod_read_half(v1) & 0x4000u;
            t1 += v0 ? 4u : 1u;
            a1 = t0;
            v1 = a2;
            v0 = slt(t2, t5);
        }
    }

    v0 = t7 ? 0u - t1 : t1;
    r[2] = v0; r[3] = v1;
    r[4] = a0; r[5] = a1; r[6] = a2; r[7] = a3;
    r[8] = t0; r[9] = t1; r[10] = t2; r[11] = t3;
    r[12] = t4; r[13] = t5; r[14] = t6; r[15] = t7;
    r[24] = t8; r[25] = t9;
    /* s0 and sp come back unchanged: lw s0,0(sp); addiu sp,sp,8. */
    psx_mod_function_return(cpu);
}

PSX_MOD_CONSTRUCTOR(frontschweine_register_terrain_line_plugin) {
    (void)psx_mod_register_function_entry_plugin(PLUGIN_ID, FN_TERRAIN_LINE,
                                                 hook_terrain_line);
}
