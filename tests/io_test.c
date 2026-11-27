/* Checks the sceIo file shim against the game's disc folder.
 *   chexa-io-test [--disc DIR]     (default $CHEXA_DISC, else /mnt/c/psp/chexa)
 * Exits 77 (CTest "skipped") when the files aren't there. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psp_imports.h"
#include "psp_shim.h"

static int failures;

#define CHECK(cond, ...)                         \
    do {                                         \
        if (cond) {                              \
            printf("ok    ");                    \
        } else {                                 \
            printf("FAIL  ");                    \
            failures++;                          \
        }                                        \
        printf(__VA_ARGS__);                     \
        printf("\n");                            \
    } while (0)

int main(int argc, char **argv)
{
    const char *disc = getenv("CHEXA_DISC");
    if (!disc)
        disc = "/mnt/c/psp/chexa";
    if (argc == 3 && !strcmp(argv[1], "--disc"))
        disc = argv[2];
    if (!psp_io_mount_disc(disc)) {
        printf("SKIP: game files not found at %s\n", disc);
        return 77;
    }

    /* Directory listing: the UMD lists "." and ".." first, then the entries. */
    SceUID d = sceIoDopen("disc0:/PSP_GAME/USRDIR");
    CHECK(d >= 0, "dopen disc0:/PSP_GAME/USRDIR -> %d", (int)d);
    int entries = 0, saw_idx = 0, saw_dot = 0, saw_dir = 0;
    SceIoDirent ent;
    memset(&ent, 0, sizeof ent);
    while (d >= 0 && sceIoDread(d, &ent) > 0) {
        entries++;
        if (!strcmp(ent.d_name, "."))
            saw_dot = 1;
        if (!strcmp(ent.d_name, "chexa.idx") && (ent.d_stat.st_mode & FIO_S_IFREG) &&
            ent.d_stat.st_size > 0)
            saw_idx = 1;
        if ((ent.d_stat.st_mode & FIO_S_IFDIR) && strcmp(ent.d_name, ".") && strcmp(ent.d_name, ".."))
            saw_dir = 1;
    }
    CHECK(d >= 0 && sceIoDclose(d) == 0, "dclose");
    CHECK(entries >= 5 && saw_dot && saw_idx && saw_dir,
          "dread: %d entries, \".\" %s, chexa.idx file %s, a subfolder %s", entries,
          saw_dot ? "yes" : "no", saw_idx ? "yes" : "no", saw_dir ? "yes" : "no");

    /* Stat, then the same file through a differently-cased path. */
    SceIoStat st;
    int r = sceIoGetstat("disc0:/PSP_GAME/USRDIR/chexa.idx", &st);
    CHECK(r == 0 && (st.st_mode & FIO_S_IFREG) && st.st_size > 40 && st.sce_mtime.year >= 2000,
          "getstat chexa.idx: size %lld, mtime year %u", (long long)st.st_size,
          (unsigned)st.sce_mtime.year);
    SceUID fd = sceIoOpen("disc0:/psp_game/usrdir/CHEXA.IDX", PSP_O_RDONLY, 0);
    CHECK(fd >= 0, "open with different case -> %d", (int)fd);
    char magic[4] = {0};
    CHECK(sceIoRead(fd, magic, 4) == 4 && !memcmp(magic, "PIDX", 4), "read: index magic");
    CHECK(sceIoLseek(fd, 0, PSP_SEEK_END) == st.st_size, "lseek end == file size");
    CHECK(sceIoLseek(fd, 2, PSP_SEEK_SET) == 2 && sceIoRead(fd, magic, 2) == 2 &&
          !memcmp(magic, "DX", 2), "lseek set + read");
    CHECK(sceIoRead(fd, magic, 0) == 0, "zero-length read");
    CHECK(sceIoClose(fd) == 0 && sceIoClose(fd) == SCE_KERNEL_ERROR_BADF, "close, double close is BADF");

    /* The async pattern the game uses: open/read async, then wait for each result. */
    SceInt64 res = -1;
    fd = sceIoOpenAsync("disc0:/PSP_GAME/USRDIR/chexa.idx", PSP_O_RDONLY, 0);
    CHECK(fd >= 0 && sceIoWaitAsyncCB(fd, &res) == 0 && res == fd, "open async + wait -> fd");
    CHECK(sceIoReadAsync(fd, magic, 4) == 0 && sceIoPollAsync(fd, &res) == 0 && res == 4,
          "read async + poll -> 4 bytes");
    CHECK(sceIoWaitAsyncCB(fd, &res) == SCE_KERNEL_ERROR_NOASYNC, "wait with nothing pending");
    CHECK(sceIoCloseAsync(fd) == 0 && sceIoWaitAsyncCB(fd, &res) == 0 && res == 0 &&
          sceIoClose(fd) == SCE_KERNEL_ERROR_BADF, "close async frees the fd after the wait");
    fd = sceIoOpenAsync("disc0:/no/such/file", PSP_O_RDONLY, 0);
    CHECK(fd >= 0 && sceIoWaitAsyncCB(fd, &res) == 0 && res == SCE_ERROR_ERRNO_ENOENT &&
          sceIoClose(fd) == 0, "failed async open reports ENOENT through the wait");

    /* Errors and sandboxing. */
    CHECK(sceIoOpen("disc0:/PSP_GAME/USRDIR/missing.bin", PSP_O_RDONLY, 0) == SCE_ERROR_ERRNO_ENOENT,
          "missing file -> ENOENT");
    CHECK(sceIoOpen("disc0:/PSP_GAME/USRDIR/chexa.idx", PSP_O_WRONLY, 0) == SCE_ERROR_ERRNO_EROFS,
          "disc0: is read-only");
    CHECK(sceIoGetstat("disc0:/../../../../PSP_GAME/USRDIR/chexa.idx", &st) == 0,
          "\"..\" stops at the device root");
    CHECK(sceIoOpen("nodev0:/x", PSP_O_RDONLY, 0) == SCE_ERROR_ERRNO_ENODEV, "unknown device -> ENODEV");
    CHECK(sceIoDopen("disc0:/PSP_GAME/USRDIR/chexa.idx") < 0, "dopen on a file fails");
    CHECK(sceUmdGetDriveStat() & PSP_UMD_READY, "UMD reports ready");

    printf("%s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
