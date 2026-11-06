/* Real SDL key mapping, PC cheats and the existing PlayStation sequence decoder. */
#include "../../platform/psx/pad_sdl.c"
#include "../../platform/controls.c"
#include "../../game/src/pccheats.c"
#include <assert.h>

PortConfig portConfig;
PortSettings portSettings;
static uint64_t field = 100;
static int focused = 1, updateAlert, saves, newWeapons;
int PortWindowFocused(void) { return focused; }
const char *PortUpdateAvailable(void) { return updateAlert ? "update" : NULL; }
int PortLinkRole(void) { return LINK_NONE; }
int PortLinkRaceActive(void) { return 0; }
void PortLinkSetInput(const LinkInput *input) {}
int PortLinkGetInput(LinkInput *input) { return 0; }
int PortLinkTakeRumble(int kind, int *level) { return 0; }
void PortPadSecondRumble(int kind, int level) {}
uint64_t PsxFieldCount(void) { return field; }
void PortLog(const char *format, ...) {}
int PortTracing(void) { return 0; }
int PortSettingsSave(void) { saves++; return 1; }
void PortScriptGamePad(unsigned char *packet) {}

unsigned char cheatWeapon, cheatDamage, cheatTime, cheatMinigun, sillyShips, gameType;
short cheatfade;
int ownShip, checkTimer, raceType;
int cheatHistory[16];
unsigned short keysPressed, keysDown;
ConfigData config, *gameData = &config, *tempGameData = &config;
static MenuEntry menus[3];
MenuEntry *menu = menus;
static ShipData ship;
ShipData *shipIndex = &ship;
void GetNewWeapon(ShipData *ships, int shipNo) { assert(ships == &ship && shipNo == 0); newWeapons++; }

static void Key(SDL_Scancode code, int down, int repeat, SDL_Keymod mod)
{
    SDL_Event event = {0};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = code;
    event.key.down = down;
    event.key.repeat = repeat;
    event.key.mod = mod;
    PsxInputEvent(&event);
}

static void Type(const char *word)
{
    for (; *word; word++) {
        char letter = *word >= 'a' && *word <= 'z' ? *word : (char)(*word - 'A' + 'a');
        SDL_Scancode code = SDL_GetScancodeFromKey((SDL_Keycode)letter, NULL);
        Key(code, 1, 0, SDL_KMOD_NONE);
        Key(code, 0, 0, SDL_KMOD_NONE);
    }
}

int main(void)
{
    static const unsigned short buttons[] = {PAD_TRIANGLE, PAD_CROSS, PAD_SQUARE, PAD_CIRCLE,
                                            PAD_TRIANGLE, PAD_CROSS, PAD_SQUARE, PAD_CIRCLE};
    size_t i;
    assert(SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMEPAD));
    PcCheatsInit();
    Type("rush"); assert(sillyShips);
    sillyShips = 0;
    PcCheatsSetContext(PC_CHEAT_IDLE);
    Type("RUSH"); assert(!sillyShips);
    field += 100;
    Type("FRAMERATE"); assert(portSettings.showFps && saves == 1);
    Type("FRAMERATE"); assert(!portSettings.showFps && saves == 2);

    UpdateCheatSequence(1);
    Type("noiseXTEAM"); assert(config.teamCheatActive);
    Type("XCLASS"); assert(config.phantomTrackCheatActive && menu[2].options[3].enabled);
    menu[2].options[3].enabled = 0;
    Type("XTRACK"); assert(config.trackCheatActive && menu[2].options[3].enabled);
    Type("PSYPROTECT"); assert(!cheatDamage);

    gameType = GAME_SINGLE_PLAYER;
    UpdateCheatSequence(0);
    Type("PSYMEGA"); assert(!cheatWeapon && !newWeapons); /* weapons disabled */
    config.weaponsOn = 1;
    Type("PSYMEGA"); assert(cheatWeapon && newWeapons == 1 && cheatfade == 255);
    Type("PSYMEGA"); assert(!cheatWeapon && newWeapons == 1);
    Type("PSYPROTECT"); assert(cheatDamage);
    Type("PSYPROTECT"); assert(!cheatDamage);
    Type("PSYTICKER"); assert(cheatTime && checkTimer == 999);
    Type("PSYTICKER"); assert(!cheatTime);
    Type("PSYRAPID"); assert(cheatMinigun);
    Type("PSYRAPID"); assert(!cheatMinigun);
    raceType = RACE_TIME_TRIAL;
    Type("PSYTICKER"); assert(cheatTime); /* the PC codes accepted single-player time trials */
    cheatTime = 0;
    gameType = GAME_LINK_MASTER;
    Type("PSYPROTECT"); assert(!cheatDamage);
    gameType = GAME_SINGLE_PLAYER;

    /* Repeats do not add letters; a second P within the code does not press Start again. */
    Type("PSY");
    Key(SDL_SCANCODE_P, 1, 0, SDL_KMOD_NONE);
    assert(!(keyboardState.buttons & PAD_START));
    Key(SDL_SCANCODE_P, 1, 1, SDL_KMOD_NONE);
    Key(SDL_SCANCODE_P, 0, 0, SDL_KMOD_NONE);
    Type("ROTECT"); assert(cheatDamage);
    cheatDamage = 0;
    Type("PSY");
    PsxPadResetPcCheats(); /* the debug console consumes its own keys */
    Type("PROTECT"); assert(!cheatDamage);
    Type("PSY");
    focused = 0;
    Type("PROTECT"); assert(!cheatDamage);
    focused = 1;
    Type("PROTECT"); assert(!cheatDamage);
    updateAlert = 1;
    Type("PSYPROTECT"); assert(!cheatDamage);
    updateAlert = 0;
    PsxPadNameEntry(NAME_LETTERS);
    Type("PSYPROTECT"); assert(!cheatDamage && PsxPadNameKey() == 'P');
    field += 5;
    UpdateCheatSequence(0);
    Type("PSY");
    Key(SDL_SCANCODE_LEFT, 1, 0, SDL_KMOD_NONE);
    Key(SDL_SCANCODE_LEFT, 0, 0, SDL_KMOD_NONE);
    Type("PROTECT"); assert(!cheatDamage);
    Type("PSY");
    Key(SDL_SCANCODE_P, 1, 0, SDL_KMOD_CTRL);
    Key(SDL_SCANCODE_P, 0, 0, SDL_KMOD_CTRL);
    Type("ROTECT"); assert(!cheatDamage);
    field += 5;
    Type("PSYPROTECT"); assert(!cheatDamage); /* stale race context during loading */
    UpdateCheatSequence(1);
    Type("PSY");
    UpdateCheatSequence(0);
    Type("PROTECT"); assert(!cheatDamage); /* no code spanning menu/race contexts */

    /* Existing console sequences continue to toggle the same effects. */
    raceType = RACE_ARCADE;
    keysDown = PAD_CHEAT_MODIFIERS;
    for (i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        keysPressed = buttons[i];
        UpdateCheatSequence(0);
    }
    assert(cheatDamage);
    SDL_Quit();
    puts("PC cheats and PlayStation sequences: OK");
    return 0;
}
