#pragma once
// Co-op control options. Every option is a compile-time switch: pass -DNAME=value in C_FLAGS (config.mk) or edit here.
//
// Vanilla Bayonetta Origins already splits one controller between the two characters:
//   Cereza   = left half:  left stick, L (jump / interact / Hug Mode in), ZL (Umbran Arts / Witch Pulse), D-pad, Minus
//   Cheshire = right half: right stick, R (send out / extend in Hug Mode), ZR (attack), A/B/X/Y (element forms), Plus
// The mod builds that one controller from two: P1 (npad No1, or the console in handheld) drives the left half,
// P2 (npad No2) drives the right half. Each player may hold a full controller; the options below fold the half of it
// that vanilla gives to the other character onto that player's own character.

// Stick that moves the player's character: 0 = left stick, 1 = right stick, 2 = whichever is pushed further.
#ifndef COOP_P1_STICK
#define COOP_P1_STICK 2
#endif
#ifndef COOP_P2_STICK
#define COOP_P2_STICK 2
#endif

// 1 = P1's face buttons press the D-pad, mirrored by position (Y = left, X = up, A = right, B = down).
// 0 = P1's face buttons are dropped outside menus (they belong to Cheshire). Default 0: the D-pad opens a
// menu in gameplay, so P1 pressing A to advance text opened it by mistake. P1's real D-pad works either way.
#ifndef COOP_P1_FACE_TO_DPAD
#define COOP_P1_FACE_TO_DPAD 0
#endif

// 1 = P2's D-pad presses the face buttons, mirrored by position (left = Y, up = X, right = A, down = B), so P2 can switch
// elements from either side. 0 = P2's D-pad is dropped.
#ifndef COOP_P2_DPAD_TO_FACE
#define COOP_P2_DPAD_TO_FACE 1
#endif

// 1 = P2's L presses the game's L, so P2 can put Cheshire into Hug Mode as well as send him out with R.
// Vanilla L is contextual (Cereza also jumps / interacts with it), so P2's L can trigger those too.
// 0 = P2's L acts as R; only P1 can enter Hug Mode (vanilla split).
#ifndef COOP_P2_HUG
#define COOP_P2_HUG 1
#endif

// 1 = follow the game's state. Menu mode (pause menu 0x1DB5280 bit 0x10000, title / main menu 0x1DB5260 bit 0x200,
// world stopped 0x1DB5260 bit 0x4000, UI mode 0x4FF0074 other than 0x800): P1's controller
// passes through unchanged (A confirms, B backs out, ZL / ZR / L / R switch tabs) and P2 is ignored. Text mode (event
// queue 0x1DB53B0, message block 0x5036648 / 0x503664C, UI mode 0x800): co-op goes on, and P1's face buttons also pass
// as A / B / X / Y so P1 can advance or close the text. 0 = the co-op split everywhere.
#ifndef COOP_MENU_TOGGLE
#define COOP_MENU_TOGGLE 1
#endif

// 1 = also accept single Joy-Cons held sideways (one per player). Vanilla supports Pro / dual Joy-Con / handheld only.
#ifndef COOP_SINGLE_JOYCON
#define COOP_SINGLE_JOYCON 1
#endif

// 1 = extra [ORCOOP] diagnostics in the emulator log: a status line every 600 frames, every new button press per
// player, and changes in the game's globals for ~20 s after P1 interacts (used to find the menu and text flags).
// Costs ~6 MB of memory and a per-frame scan, so release builds leave it off. Mode switches are logged either way.
#ifndef COOP_DIAG
#define COOP_DIAG 0
#endif
