#include "testenv.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../platform/link.h"
#include "../../platform/psx/psx_io.h"

#ifndef REPO_DIR
#define REPO_DIR "."
#endif

static uint64_t fields;
static char saveDir[256];
int portPal; /* these fixtures use NTSC timing */

uint64_t PsxFieldCount(void)
{
    return fields;
}

int PortLinkRaceActive(void)
{
    return 0;
}

uint64_t test_fields(void)
{
    return fields;
}

/* The platform's video timing: one field passes. */
void PsxVBlank(void)
{
    fields++;
    PsxIoField(fields);
}

/* ... and the fields due by the clock while the game reads the disc: none (the tests' time is the fields'). */
void PsxPassFields(void)
{
}

/* The platform's two-player link and window, as without them: no link, the window in focus. */
int PortLinkRole(void)
{
    return LINK_NONE;
}

void PortLinkSetInput(const LinkInput *input)
{
    (void)input;
}

int PortLinkGetInput(LinkInput *input)
{
    (void)input;
    return 0;
}

int PortLinkTakeRumble(int kind, int *level)
{
    (void)kind;
    (void)level;
    return 0;
}

/* (platform/main.c) the second player's motors: none without the link */
void PortPadSecondRumble(int kind, int level)
{
    (void)kind;
    (void)level;
}

const char *PortUpdateAvailable(void) { return NULL; } /* no updater in I/O tests */
int PortAnalogSteeringEnabled(void) { return 0; } /* these tests exercise the console pad protocol */

int PortWindowFocused(void)
{
    return 1;
}

void PortLog(const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
}

static int exists(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0;
}

const char *test_disc_path(void)
{
    static char path[1024];
    const char *env = getenv("WIPEOUT_XL_DISC");

    if (env) {
        return exists(env) ? env : NULL;
    }
    snprintf(path, sizeof(path), "%s/../wipeout-xl/Wipeout XL (USA).cue", REPO_DIR);
    return exists(path) ? path : NULL;
}

int test_cdda_reference(const char *cue, int track, int sectorInTrack, uint8_t out[2352])
{
    FILE *sheet = fopen(cue, "r");
    if (!sheet) return 0;

    char line[2048], filename[1024] = "", type[32] = "";
    int number = 0, fileSector = -1;
    while (fgets(line, sizeof(line), sheet)) {
        char key[16];
        if (sscanf(line, " %15s", key) != 1) continue;
        if (!strcmp(key, "FILE")) {
            if (number == track) break;
            filename[0] = type[0] = 0;
            if (sscanf(line, " FILE \"%1023[^\"]\" %31s", filename, type) != 2)
                sscanf(line, " FILE %1023s %31s", filename, type);
        } else if (!strcmp(key, "TRACK")) {
            if (number == track) break;
            number = 0;
            sscanf(line, " TRACK %d", &number);
        } else if (number == track && !strcmp(key, "INDEX")) {
            int index, minutes, seconds, frames;
            if (sscanf(line, " INDEX %d %d:%d:%d", &index, &minutes, &seconds, &frames) == 4 &&
                (index == 0 || (index == 1 && fileSector < 0)))
                fileSector = (minutes * 60 + seconds) * 75 + frames;
        }
    }
    fclose(sheet);
    if (fileSector < 0 || sectorInTrack < 0 || !filename[0] || strcmp(type, "BINARY")) return 0;

    char path[2048];
    const char *slash = strrchr(cue, '/');
    int directory = filename[0] != '/' && slash ? (int)(slash - cue + 1) : 0;
    int length = snprintf(path, sizeof(path), "%.*s%s", directory, cue, filename);
    if (length < 0 || (size_t)length >= sizeof(path)) return 0;
    FILE *raw = fopen(path, "rb");
    if (!raw) return 0;
    int ok = fseek(raw, ((long)fileSector + sectorInTrack) * 2352, SEEK_SET) == 0 &&
             fread(out, 1, 2352, raw) == 2352;
    fclose(raw);
    return ok;
}

const char *test_root_dir(void)
{
    static char path[1024];
    const char *env = getenv("WIPEOUT_XL_ROOT");

    if (env) {
        return exists(env) ? env : NULL;
    }
    snprintf(path, sizeof(path), "%s/../wipeout-xl/disc/root", REPO_DIR);
    return exists(path) ? path : NULL;
}

const char *test_save_dir(void)
{
    return saveDir;
}

static double lastSpeed = 1.0;
static const char *lastScript;
static int started;

int test_io_restart(void)
{
    PsxIoConfig config;
    char error[256] = "";

    PsxIoShutdown();
    memset(&config, 0, sizeof(config));
    config.discPath = test_disc_path();
    config.savePath = saveDir;
    config.fixedStep = 1;
    config.cdSpeed = lastSpeed;
    config.padScript = lastScript;
    fields = 0;
    return PsxIoInit(&config, error, sizeof(error));
}

int test_io_start(double cdSpeed, const char *padScript)
{
    PsxIoConfig config;
    char error[256] = "";
    const char *disc = test_disc_path();

    if (!disc) {
        return 0;
    }
    snprintf(saveDir, sizeof(saveDir), "/tmp/psxio-test-XXXXXX");
    if (!mkdtemp(saveDir)) {
        perror("mkdtemp");
        exit(1);
    }
    memset(&config, 0, sizeof(config));
    config.discPath = disc;
    config.savePath = saveDir;
    config.fixedStep = 1;
    config.cdSpeed = cdSpeed;
    config.padScript = padScript;
    lastSpeed = cdSpeed;
    lastScript = padScript;
    started = 1;
    fields = 0;
    if (!PsxIoInit(&config, error, sizeof(error))) {
        fprintf(stderr, "PsxIoInit: %s\n", error);
        exit(1);
    }
    return 1;
}

void test_io_stop(void)
{
    char command[300];

    (void)started;
    PsxIoShutdown();
    snprintf(command, sizeof(command), "rm -rf '%s'", saveDir);
    if (saveDir[0]) {
        if (system(command) != 0) {
            fprintf(stderr, "cleanup failed\n");
        }
    }
}
