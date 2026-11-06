/* Port: PC cheats use the console game's existing cheat effects. */
#include "functions.h"
#include "pad.h"
#include "racectrl.h"
#include "weapon.h"
#include "../../platform/pc_cheats.h"

extern MenuEntry *menu;
extern ConfigData *gameData;
extern ShipData *shipIndex;
extern int ownShip;
extern int checkTimer;
extern short cheatfade;

static void ApplyPcCheat(int code, int context)
{
    if (code == PC_CHEAT_FPS) {
        PortToggleFps();
    } else if (context == PC_CHEAT_STARTUP) {
        if (code == PC_CHEAT_RUSH) sillyShips = 1;
    } else if (context == PC_CHEAT_MENU && tempGameData) {
        switch (code) {
        case PC_CHEAT_TEAM: tempGameData->teamCheatActive = 1; break;
        case PC_CHEAT_CLASS:
            tempGameData->phantomTrackCheatActive = 1;
            menu[2].options[3].enabled = 1;
            break;
        case PC_CHEAT_TRACK:
            tempGameData->trackCheatActive = 1;
            menu[2].options[3].enabled = 1;
            break;
        }
    } else if (context == PC_CHEAT_RACE && gameType == GAME_SINGLE_PLAYER) {
        switch (code) {
        case PC_CHEAT_WEAPONS:
            if (gameData->weaponsOn) {
                cheatWeapon ^= 1;
                if (cheatWeapon) GetNewWeapon(shipIndex, ownShip);
                cheatfade = 0xFF;
            }
            break;
        case PC_CHEAT_PROTECT: cheatDamage ^= 1; cheatfade = 0xFF; break;
        case PC_CHEAT_TIME:
            cheatTime ^= 1;
            if (cheatTime) checkTimer = 0x3E7;
            cheatfade = 0xFF;
            break;
        case PC_CHEAT_MINIGUN: cheatMinigun ^= 1; cheatfade = 0xFF; break;
        }
    }
}

void PcCheatsInit(void)
{
    PcCheatsSetHandler(ApplyPcCheat);
    PcCheatsSetContext(PC_CHEAT_STARTUP);
}
