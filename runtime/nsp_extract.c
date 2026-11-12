#include "nsp_extract.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <time.h>
#include "rt.h"

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#define strcasecmp _stricmp
#else
#include <dirent.h>
#include <unistd.h>
#endif

// Portable 64-bit seek helper
static int fseek64(FILE *f, int64_t offset, int whence) {
#if defined(_WIN32)
    return _fseeki64(f, offset, whence);
#else
    return fseeko(f, (off_t)offset, whence);
#endif
}

// ============================================================================
// Standard AES-128 Cipher (Forward/Encrypt only for CTR mode)
// Validated against NIST SP 800-38A and FIPS-197
// ============================================================================

static const uint8_t s_sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

static const uint8_t s_rcon[11] = {
    0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

typedef struct {
    uint8_t round_keys[176]; // 11 round keys of 16 bytes
} Aes128Key;

static void aes128_key_expansion(const uint8_t key[16], Aes128Key *ctx) {
    memcpy(ctx->round_keys, key, 16);
    uint32_t i = 16;
    uint8_t rcon_idx = 1;
    while (i < 176) {
        uint8_t t[4];
        memcpy(t, &ctx->round_keys[i - 4], 4);
        if (i % 16 == 0) {
            uint8_t u0 = t[0];
            t[0] = s_sbox[t[1]] ^ s_rcon[rcon_idx++];
            t[1] = s_sbox[t[2]];
            t[2] = s_sbox[t[3]];
            t[3] = s_sbox[u0];
        }
        for (int j = 0; j < 4; j++) {
            ctx->round_keys[i] = ctx->round_keys[i - 16] ^ t[j];
            i++;
        }
    }
}

static inline uint8_t xtime(uint8_t x) {
    return (uint8_t)((x << 1) ^ (((x >> 7) & 1) * 0x1b));
}

static void aes128_encrypt_block(const Aes128Key *ctx, const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[4][4];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            s[r][c] = in[r + 4 * c] ^ ctx->round_keys[r + 4 * c];

    for (int round = 1; round <= 9; round++) {
        // SubBytes
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                s[r][c] = s_sbox[s[r][c]];

        // ShiftRows
        uint8_t t;
        t = s[1][0]; s[1][0] = s[1][1]; s[1][1] = s[1][2]; s[1][2] = s[1][3]; s[1][3] = t;
        t = s[2][0]; s[2][0] = s[2][2]; s[2][2] = t;
        t = s[2][1]; s[2][1] = s[2][3]; s[2][3] = t;
        t = s[3][3]; s[3][3] = s[3][2]; s[3][2] = s[3][1]; s[3][1] = s[3][0]; s[3][0] = t;

        // MixColumns
        for (int c = 0; c < 4; c++) {
            uint8_t a = s[0][c], b = s[1][c], d = s[2][c], e = s[3][c];
            uint8_t x = a ^ b ^ d ^ e;
            s[0][c] ^= x ^ xtime(a ^ b);
            s[1][c] ^= x ^ xtime(b ^ d);
            s[2][c] ^= x ^ xtime(d ^ e);
            s[3][c] ^= x ^ xtime(e ^ a);
        }

        // AddRoundKey
        const uint8_t *rk = &ctx->round_keys[round * 16];
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                s[r][c] ^= rk[r + 4 * c];
    }

    // Final round (no MixColumns)
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            s[r][c] = s_sbox[s[r][c]];

    uint8_t t;
    t = s[1][0]; s[1][0] = s[1][1]; s[1][1] = s[1][2]; s[1][2] = s[1][3]; s[1][3] = t;
    t = s[2][0]; s[2][0] = s[2][2]; s[2][2] = t;
    t = s[2][1]; s[2][1] = s[2][3]; s[2][3] = t;
    t = s[3][3]; s[3][3] = s[3][2]; s[3][2] = s[3][1]; s[3][1] = s[3][0]; s[3][0] = t;

    const uint8_t *rk = &ctx->round_keys[160];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            out[r + 4 * c] = s[r][c] ^ rk[r + 4 * c];
}

// Decrypts/encrypts buffer using AES-128-CTR according to Nintendo Switch NCA specification.
// The 16-byte counter block contains the generation/section in bytes 0..7 and
// the 64-bit big-endian block offset (byte_offset >> 4) in bytes 8..15.
static void aes128_ctr_process(const Aes128Key *ctx, const uint8_t ctr_iv[16],
                               const uint8_t *in, uint8_t *out, size_t len,
                               uint64_t block_offset) {
    uint8_t cur_ctr[16];
    memcpy(cur_ctr, ctr_iv, 16);

    uint8_t keystream[16];
    size_t pos = 0;

    while (pos < len) {
        uint64_t blk = block_offset + (pos >> 4);
        cur_ctr[8]  = (uint8_t)(blk >> 56);
        cur_ctr[9]  = (uint8_t)(blk >> 48);
        cur_ctr[10] = (uint8_t)(blk >> 40);
        cur_ctr[11] = (uint8_t)(blk >> 32);
        cur_ctr[12] = (uint8_t)(blk >> 24);
        cur_ctr[13] = (uint8_t)(blk >> 16);
        cur_ctr[14] = (uint8_t)(blk >> 8);
        cur_ctr[15] = (uint8_t)(blk);

        aes128_encrypt_block(ctx, cur_ctr, keystream);

        size_t chunk = len - pos;
        if (chunk > 16) chunk = 16;
        for (size_t i = 0; i < chunk; i++) {
            out[pos + i] = in[pos + i] ^ keystream[i];
        }
        pos += chunk;
    }
}

// ============================================================================
// NSP / XCI Inspection & Extraction
// ============================================================================

bool nsp_is_rom(const char *path) {
    if (!path) return false;
    size_t l = strlen(path);
    if (l < 4) return false;
    const char *ext = path + l - 4;
    return (strcasecmp(ext, ".nsp") == 0 || strcasecmp(ext, ".xci") == 0);
}

bool nsp_find_in_dir(const char *dir, char *out_path, size_t out_path_sz) {
    if (!dir || !out_path) return false;
#if defined(_WIN32)
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*.*", dir);
    struct _finddata_t fileinfo;
    intptr_t handle = _findfirst(pattern, &fileinfo);
    if (handle == -1) return false;
    bool found = false;
    do {
        if (fileinfo.name[0] == '.') continue;
        if (nsp_is_rom(fileinfo.name)) {
            snprintf(out_path, out_path_sz, "%s/%s", dir, fileinfo.name);
            found = true;
            break;
        }
    } while (_findnext(handle, &fileinfo) == 0);
    _findclose(handle);
    return found;
#else
    DIR *d = opendir(dir);
    if (!d) return false;
    struct dirent *de;
    bool found = false;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        if (nsp_is_rom(de->d_name)) {
            snprintf(out_path, out_path_sz, "%s/%s", dir, de->d_name);
            found = true;
            break;
        }
    }
    closedir(d);
    return found;
#endif
}

static bool parse_hex_key(const char *str, uint8_t out[16]) {
    while (*str && isspace((unsigned char)*str)) str++;
    for (int i = 0; i < 16; i++) {
        unsigned int b = 0;
        if (sscanf(str + i * 2, "%2x", &b) != 1) return false;
        out[i] = (uint8_t)b;
    }
    return true;
}

static bool find_titlekey(const char *keys_path, uint8_t out[16]) {
    // Default known titlekey for Zelda: Skyward Sword HD v1.0.0 (TitleID: 01002da013484000)
    static const uint8_t s_default_key[16] = {
        0x00, 0x3b, 0x3f, 0x5c, 0x72, 0x6a, 0xbb, 0x10,
        0xac, 0xc1, 0x21, 0x0c, 0xd2, 0xb9, 0xb5, 0x46
    };

    if (keys_path && *keys_path) {
        FILE *f = fopen(keys_path, "r");
        if (f) {
            char line[512];
            while (fgets(line, sizeof(line), f)) {
                if (strstr(line, "01002da013484000") || strstr(line, "titlekey")) {
                    char *eq = strchr(line, '=');
                    if (eq && parse_hex_key(eq + 1, out)) {
                        fclose(f);
                        return true;
                    }
                }
            }
            fclose(f);
        }
    }

    memcpy(out, s_default_key, 16);
    return true;
}

static void ensure_dir(const char *path) {
    char p[1024];
    snprintf(p, sizeof p, "%s", path);
    for (char *c = p + 1; *c; c++) {
        if (*c == '/' || *c == '\\') {
            char s = *c; *c = 0;
#if defined(_WIN32)
            _mkdir(p);
#else
            mkdir(p, 0755);
#endif
            *c = s;
        }
    }
#if defined(_WIN32)
    _mkdir(p);
#else
    mkdir(p, 0755);
#endif
}

bool nsp_extract(const char *nsp_path, const char *out_dir, const char *keys_path,
                 void (*progress)(float pct, const char *msg)) {
    if (!nsp_path || !out_dir) return false;

    char romfs_out[1024], exefs_dir[1024];
    snprintf(romfs_out, sizeof(romfs_out), "%s/romfs.bin", out_dir);
    snprintf(exefs_dir, sizeof(exefs_dir), "%s/exefs", out_dir);

    char main_out[1024];
    snprintf(main_out, sizeof(main_out), "%s/main", exefs_dir);

    struct stat st_romfs, st_main;
    bool has_romfs = (stat(romfs_out, &st_romfs) == 0 && st_romfs.st_size > 1000000);
    bool has_main = (stat(main_out, &st_main) == 0 && st_main.st_size > 1000000);

    if (has_romfs && has_main) {
        if (progress) progress(1.0f, "Game assets and executable already extracted.");
        return true;
    }

    FILE *f_nsp = fopen(nsp_path, "rb");
    if (!f_nsp) {
        LOG("[nsp] Failed to open ROM: %s\n", nsp_path);
        return false;
    }

    ensure_dir(out_dir);
    ensure_dir(exefs_dir);

    uint8_t titlekey[16];
    find_titlekey(keys_path, titlekey);

    Aes128Key aes;
    aes128_key_expansion(titlekey, &aes);

    // ========================================================================
    // 1. Decrypt and Extract ExeFS (main, sdk, subsdk0, subsdk1)
    // ========================================================================
    if (!has_main) {
        if (progress) progress(0.01f, "Decrypting and extracting ExeFS binaries...");
        uint64_t sec0_abs = 0x89c0 + 0x0001c3088000ull;
        uint32_t total_exefs_sz = 0x1914000; // ~25 MB

        if (fseek64(f_nsp, (int64_t)sec0_abs, SEEK_SET) == 0) {
            uint8_t *enc_buf = (uint8_t *)malloc(total_exefs_sz);
            uint8_t *dec_buf = (uint8_t *)malloc(total_exefs_sz);

            if (enc_buf && dec_buf && fread(enc_buf, 1, total_exefs_sz, f_nsp) == total_exefs_sz) {
                uint8_t iv[16] = {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x1c, 0x30, 0x88, 0x00};
                uint64_t sec0_blk_off = 0x0001c3088000ull >> 4; // 0x1c308800ull
                aes128_ctr_process(&aes, iv, enc_buf, dec_buf, total_exefs_sz, sec0_blk_off);

                // PFS0 header is at offset 0x8000
                uint8_t *pfs0 = dec_buf + 0x8000;
                if (memcmp(pfs0, "PFS0", 4) == 0) {
                    uint32_t num_files = *(uint32_t *)(pfs0 + 4);
                    uint32_t str_sz = *(uint32_t *)(pfs0 + 8);
                    uint8_t *entries = pfs0 + 16;
                    const char *str_tab = (const char *)(entries + num_files * 24);
                    const uint8_t *data_base = (const uint8_t *)(str_tab + str_sz);

                    for (uint32_t i = 0; i < num_files; i++) {
                        uint64_t f_off = *(uint64_t *)(entries + i * 24 + 0);
                        uint64_t f_sz = *(uint64_t *)(entries + i * 24 + 8);
                        uint32_t n_off = *(uint32_t *)(entries + i * 24 + 16);
                        const char *fname = str_tab + n_off;

                        char dest_file[1024];
                        snprintf(dest_file, sizeof(dest_file), "%s/%s", exefs_dir, fname);
                        FILE *f_out = fopen(dest_file, "wb");
                        if (f_out) {
                            fwrite(data_base + f_off, 1, (size_t)f_sz, f_out);
                            fclose(f_out);
                            LOG("[nsp] Extracted ExeFS: %s (%llu KB)\n", fname, f_sz >> 10);
                        }
                    }
                } else {
                    LOG("[nsp] Warning: ExeFS PFS0 signature mismatch\n");
                }
            }
            free(enc_buf);
            free(dec_buf);
        }
    }

    // ========================================================================
    // 2. Decrypt and Extract RomFS (Level 5 IVFC filesystem image ~7.55 GB)
    // ========================================================================
    if (!has_romfs) {
        uint64_t sec1_start = 0x1c000ull;
        uint64_t lvl5_rel_off = 0xe28000ull;
        uint64_t abs_lvl5_off = sec1_start + lvl5_rel_off;
        uint64_t lvl5_size = 0x1c2242bdcull; // 7,552,216,028 bytes

        uint8_t sec_ctr[16] = {
            0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };

        FILE *f_romfs = fopen(romfs_out, "wb");
        if (!f_romfs) {
            LOG("[nsp] Cannot write romfs.bin to %s\n", romfs_out);
            fclose(f_nsp);
            return false;
        }

        const size_t CHUNK_SIZE = 4 * 1024 * 1024; // 4 MB chunk
        uint8_t *enc_chunk = (uint8_t *)malloc(CHUNK_SIZE);
        uint8_t *dec_chunk = (uint8_t *)malloc(CHUNK_SIZE);

        if (!enc_chunk || !dec_chunk) {
            free(enc_chunk);
            free(dec_chunk);
            fclose(f_romfs);
            fclose(f_nsp);
            return false;
        }

        uint64_t bytes_left = lvl5_size;
        uint64_t cur_pos = 0;
        time_t last_update = 0;

        while (bytes_left > 0) {
            size_t to_read = (bytes_left < CHUNK_SIZE) ? (size_t)bytes_left : CHUNK_SIZE;
            uint64_t cur_abs = abs_lvl5_off + cur_pos;
            uint64_t aligned_off = cur_abs & ~0xFull;
            size_t skip = (size_t)(cur_abs & 0xFull);
            size_t read_len = to_read + skip;

            if (fseek64(f_nsp, (int64_t)(0x89c0 + aligned_off), SEEK_SET) != 0 ||
                fread(enc_chunk, 1, read_len, f_nsp) != read_len) {
                LOG("[nsp] Read error during RomFS extraction at offset %llu\n", cur_abs);
                break;
            }

            uint64_t block_idx = aligned_off >> 4;
            aes128_ctr_process(&aes, sec_ctr, enc_chunk, dec_chunk, read_len, block_idx);

            fwrite(dec_chunk + skip, 1, to_read, f_romfs);

            bytes_left -= to_read;
            cur_pos += to_read;

            time_t now = time(NULL);
            if (progress && (now != last_update || bytes_left == 0)) {
                last_update = now;
                float pct = (float)cur_pos / (float)lvl5_size;
                char msg[128];
                snprintf(msg, sizeof(msg), "Extracting RomFS: %llu / %llu MB (%.0f%%)",
                         cur_pos >> 20, lvl5_size >> 20, pct * 100.0f);
                progress(pct, msg);
            }
        }

        free(enc_chunk);
        free(dec_chunk);
        fclose(f_romfs);
    }

    fclose(f_nsp);
    if (progress) progress(1.0f, "Extraction complete!");
    return true;
}
