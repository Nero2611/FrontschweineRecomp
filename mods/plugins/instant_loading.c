/*
 * Instant loading for Frontschweine (SLES-02767).
 *
 * The game reads every data file through one small CD library in its boot
 * EXE: Load (whole file), Open (CdSearchFile) and ReadAt (part of the open
 * file). Each call blocks until the emulated drive has delivered the sectors,
 * which is what the loading screens wait for. This plugin takes those three
 * calls over: it copies the file from extracted/ next to the executable
 * (filled from the mounted disc on first use) straight into guest RAM and
 * leaves the library's globals exactly as a completed CD read does.
 *
 * Speech, music and cutscenes (.IXA/.SXA XA streams) only Open their file for
 * its disc position and are then streamed by the drive itself, so they keep
 * the emulated CD and its timing. Any call this plugin does not handle
 * (unknown file, read already in flight, SPU-bound read) runs the game's own
 * code.
 */
#include "cpu_state.h"
#include "mod_plugins.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#define instant_mkdir(path) _mkdir(path)
#else
#include <sys/stat.h>
#define instant_mkdir(path) mkdir(path, 0755)
#endif

#define PLUGIN_ID "frontschweine.instant-loading"

/* Library entry points (boot EXE). */
#define FN_OPEN    0x80016A78u /* int Open(const char* name) -> size */
#define FN_READ_AT 0x80016BD0u /* int ReadAt(void* dst, int offset, int len) -> size */
#define FN_LOAD    0x80016D9Cu /* int Load(const char* name, void* dst) -> size */

/* Library state. */
#define G_BUSY      0x800291C0u /* -1 while a read is in flight, 0 when done */
#define G_SPU_ADDR  0x800291C4u /* >0: sectors are forwarded to SPU RAM */
#define G_SIZE      0x800291C8u /* size of the last found file */
#define G_STATUS    0x800291CCu /* progress code, -1 when idle */
#define G_REMAIN    0x800291D0u /* bytes the current read still owes */
#define G_FILE      0x800291D4u /* CdlFILE of the open file (pos, size, name) */
#define G_READY_CB  0x800291ECu /* CdReadyCallback installed by a read */
#define G_SYNC_CB   0x800291F0u /* CdSyncCallback installed by a read */
#define G_OFFSET0   0x80029200u /* offset the current read started at */
#define G_LENGTH    0x80029204u /* length of the current read */
#define G_RETRIES   0x80029208u /* cleared when Load/Open start */
#define G_DST       0x80037B5Cu /* next destination byte */
#define G_OFFSET    0x80037B60u /* next file offset */
#define G_NAME      0x80037B74u /* copy of the requested name */
#define G_DST0      0x80037BF8u /* destination the current read started at */

#define CDLFILE_SIZE 24u
#define CD_LEAD_IN   150u /* 00:02:00 */

typedef struct {
    char key[64];   /* "DIR/SUB/FILE.EXT", upper case */
    char name[16];  /* ISO identifier as CdSearchFile stores it */
    uint32_t lba;   /* disc-relative, 0 = 00:02:00 */
    uint32_t size;
    int found;
    uint8_t* data;  /* loaded on first read */
} DiscFile;

static DiscFile* s_files;
static uint32_t s_file_count;
static uint32_t s_file_cap;

static uint32_t rd32(uint32_t address) { return psx_mod_read_word(address); }
static void wr32(uint32_t address, uint32_t value) { psx_mod_write_word(address, value); }

/* Read the guest name and turn it into a disc path the way the library's
 * Search does ("\" prefix, upper case, ";1" suffix), minus prefix/suffix. */
static int guest_name_key(uint32_t address, char* raw, uint32_t raw_size,
                          char* key, uint32_t key_size) {
    uint32_t n = 0;
    for (;; n++) {
        if (n + 1u >= raw_size || n + 1u >= key_size) return 0;
        const char c = (char)psx_mod_read_byte(address + n);
        raw[n] = c;
        if (!c) break;
    }
    if (!n || strchr(raw, ';')) return 0;
    const char* src = raw[0] == '\\' ? raw + 1 : raw;
    uint32_t k = 0;
    for (; *src; src++) {
        char c = *src;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c == '\\') c = '/';
        key[k++] = c;
    }
    key[k] = '\0';
    return k > 0 && key[k - 1] != '/';
}

static DiscFile* lookup_key(const char* key) {
    for (uint32_t i = 0; i < s_file_count; i++)
        if (strcmp(s_files[i].key, key) == 0) return s_files[i].found ? &s_files[i] : NULL;
    if (s_file_count == s_file_cap) {
        const uint32_t cap = s_file_cap ? s_file_cap * 2u : 256u;
        DiscFile* grown = (DiscFile*)realloc(s_files, cap * sizeof *grown);
        if (!grown) return NULL;
        s_files = grown;
        s_file_cap = cap;
    }
    DiscFile* f = &s_files[s_file_count++];
    memset(f, 0, sizeof *f);
    snprintf(f->key, sizeof f->key, "%s", key);
    f->found = psx_mod_find_disc_file(key, &f->lba, &f->size) && f->size > 0;
    if (f->found) {
        const char* base = strrchr(key, '/');
        snprintf(f->name, sizeof f->name, "%s;1", base ? base + 1 : key);
    }
    return f->found ? f : NULL;
}

static DiscFile* lookup_lba(uint32_t lba, uint32_t size) {
    for (uint32_t i = 0; i < s_file_count; i++)
        if (s_files[i].found && s_files[i].lba == lba && s_files[i].size == size)
            return &s_files[i];
    return NULL;
}

static void extracted_path(const DiscFile* f, char* out, size_t out_size) {
    snprintf(out, out_size, "%sextracted/%s", psx_mod_exe_dir(), f->key);
}

static void make_parent_dirs(char* path) {
    for (char* p = path; *p; p++) {
        if (*p != '/' && *p != '\\') continue;
        if (p == path || p[-1] == ':') continue;
        const char saved = *p;
        *p = '\0';
        (void)instant_mkdir(path);
        *p = saved;
    }
}

/* The file's bytes: extracted/ copy when present and complete, otherwise
 * read from the mounted disc and extracted for the next run. */
static const uint8_t* file_data(DiscFile* f) {
    if (f->data) return f->data;
    uint8_t* data = (uint8_t*)malloc(f->size);
    if (!data) return NULL;
    char path[512];
    extracted_path(f, path, sizeof path);
    FILE* in = fopen(path, "rb");
    if (in) {
        const int complete = fread(data, 1, f->size, in) == f->size && fgetc(in) == EOF;
        fclose(in);
        if (complete) return f->data = data;
    }
    uint32_t got = 0;
    if (!psx_mod_read_disc_file(f->key, data, f->size, &got) || got != f->size) {
        free(data);
        return NULL;
    }
    make_parent_dirs(path);
    FILE* out = fopen(path, "wb");
    if (out) {
        const int written = fwrite(data, 1, f->size, out) == f->size;
        if (fclose(out) != 0 || !written) remove(path);
    }
    return f->data = data;
}

static uint8_t to_bcd(uint32_t value) { return (uint8_t)(((value / 10u) << 4) | (value % 10u)); }
static uint32_t from_bcd(uint8_t value) { return (value >> 4) * 10u + (value & 0x0Fu); }

/* The CdlFILE CdSearchFile copies out of libcd's directory cache. */
static void write_cdlfile(const DiscFile* f) {
    uint8_t file[CDLFILE_SIZE] = {0};
    const uint32_t sector = f->lba + CD_LEAD_IN;
    file[0] = to_bcd(sector / (60u * 75u));
    file[1] = to_bcd((sector / 75u) % 60u);
    file[2] = to_bcd(sector % 75u);
    memcpy(file + 4, &f->size, 4);
    memcpy(file + 8, f->name, strlen(f->name));
    (void)psx_mod_write_guest_bytes(G_FILE, file, sizeof file);
}

/* Effects of the library's Search step for a found file. */
static void complete_search(const DiscFile* f, const char* raw_name) {
    wr32(G_RETRIES, 0);
    (void)psx_mod_write_guest_bytes(G_NAME, raw_name, (uint32_t)strlen(raw_name) + 1u);
    write_cdlfile(f);
    wr32(G_SIZE, f->size);
}

/* Effects of a completed read of [offset, offset+length) into dst. */
static int complete_read(DiscFile* f, uint32_t dst, uint32_t offset, uint32_t length) {
    const uint8_t* data = file_data(f);
    if (!data || !psx_mod_write_guest_bytes(dst, data + offset, length)) return 0;
    wr32(G_DST0, dst);
    wr32(G_DST, dst + length);
    wr32(G_OFFSET0, offset);
    wr32(G_OFFSET, offset + length);
    wr32(G_LENGTH, length);
    wr32(G_REMAIN, 0);
    return 1;
}

/* The library refuses (or would run a different path) in these states; let
 * its own code produce that outcome. */
static int library_idle(int reads) {
    if (rd32(G_BUSY) == 0xFFFFFFFFu) return 0;
    if (rd32(G_READY_CB) || rd32(G_SYNC_CB)) return 0;
    if (reads && rd32(G_SPU_ADDR)) return 0;
    return 1;
}

static void finish_call(CPUState* cpu, uint32_t result) {
    wr32(G_BUSY, 0);
    wr32(G_STATUS, 0xFFFFFFFFu);
    cpu->gpr[2] = result;
    psx_mod_function_return(cpu);
}

static void hook_open(CPUState* cpu, uint32_t address) {
    (void)address;
    char raw[64], key[64];
    if (!psx_mod_function_can_return() || !library_idle(0)) return;
    if (!guest_name_key(cpu->gpr[4], raw, sizeof raw, key, sizeof key)) return;
    DiscFile* f = lookup_key(key);
    if (!f) return;
    complete_search(f, raw);
    finish_call(cpu, f->size);
}

static void hook_load(CPUState* cpu, uint32_t address) {
    (void)address;
    char raw[64], key[64];
    if (!psx_mod_function_can_return() || !library_idle(1)) return;
    if (!guest_name_key(cpu->gpr[4], raw, sizeof raw, key, sizeof key)) return;
    DiscFile* f = lookup_key(key);
    if (!f || !complete_read(f, cpu->gpr[5], 0, f->size)) return;
    complete_search(f, raw);
    finish_call(cpu, f->size);
}

static void hook_read_at(CPUState* cpu, uint32_t address) {
    (void)address;
    if (!psx_mod_function_can_return() || !library_idle(1)) return;
    const uint8_t minute = psx_mod_read_byte(G_FILE);
    const uint32_t size = rd32(G_FILE + 4u);
    if (minute == 0xFFu || !size) return; /* closed: the library reports -7 */
    const uint32_t sector = from_bcd(minute) * 60u * 75u +
                            from_bcd(psx_mod_read_byte(G_FILE + 1u)) * 75u +
                            from_bcd(psx_mod_read_byte(G_FILE + 2u));
    if (sector < CD_LEAD_IN) return;
    DiscFile* f = lookup_lba(sector - CD_LEAD_IN, size);
    if (!f) return;
    /* The library's own clamping of the requested window. */
    uint32_t offset = cpu->gpr[5];
    uint32_t length = cpu->gpr[6];
    if (size - 1u < offset) offset = size - 1u;
    if (!length) length = size;
    if (size < offset + length) length = size - offset;
    if (!complete_read(f, cpu->gpr[4], offset, length)) return;
    finish_call(cpu, rd32(G_SIZE));
}

PSX_MOD_CONSTRUCTOR(frontschweine_register_instant_loading_plugin) {
    (void)psx_mod_register_function_entry_plugin(PLUGIN_ID, FN_OPEN, hook_open);
    (void)psx_mod_register_function_entry_plugin(PLUGIN_ID, FN_READ_AT, hook_read_at);
    (void)psx_mod_register_function_entry_plugin(PLUGIN_ID, FN_LOAD, hook_load);
}
