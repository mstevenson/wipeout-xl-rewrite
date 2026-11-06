/* PC cheats from the Windows release, alongside the PlayStation button sequences. */
#ifndef PC_CHEATS_H
#define PC_CHEATS_H

enum {
    PC_CHEAT_WEAPONS, PC_CHEAT_PROTECT, PC_CHEAT_TIME, PC_CHEAT_MINIGUN,
    PC_CHEAT_TEAM, PC_CHEAT_CLASS, PC_CHEAT_TRACK, PC_CHEAT_RUSH, PC_CHEAT_FPS
};
enum { PC_CHEAT_IDLE, PC_CHEAT_STARTUP, PC_CHEAT_MENU, PC_CHEAT_RACE };

/* The game installs its handler at startup and refreshes the menu/race context each frame.
   The input layer expires that context during loads; startup ends before ship models load. */
void PcCheatsInit(void);
void PcCheatsSetHandler(void (*handler)(int code, int context));
void PcCheatsSetContext(int context);
void PortToggleFps(void);

#endif
