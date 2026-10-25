// Bayonetta Origins: Cereza and the Lost Demon (title 0100CF5010FEC000, build a1ce2940...) - local 2-player co-op.
// P1 (npad No1 or handheld) plays Cereza, P2 (npad No2) plays Cheshire. Options in config.hpp.
// Every decision is logged as an [ORCOOP] line in the emulator log.
//
// Input path (stripped binary; all names below are ours):
//   init (nnMain 0xFFC66C): InitializeNpad; SetSupportedNpadIdType({No1, Handheld}, 2); SetSupportedNpadStyleSet(7)
//     (FullKey | Handheld | JoyDual); pad objects No1 @0x50CBC10 and Handheld @0x50CBFF8 (+0 enabled, +8 style index,
//     +0x10 npad id).
//   InputManager 0x122E44C, once per frame: PollPad(No1, &a) @0x122E654, PollPad(Handheld, &b) @0x122E6C4, then when
//     the handheld pad's style index (0x50CC000) is 2 it merges b into a (buttons OR, each axis keeps the larger
//     magnitude). Only a goes on to the game.
//   PollPad 0x1530EA4 (pad, GameInput* out) -> any input: GetNpadStyleSet, GetNpadState for that style, sticks / 32767,
//     buttons through 0x153172C into the game's own bit layout. Six-axis handles are fetched fresh on every call, so a
//     pad object needs only the three fields above.
// The co-op split happens at 0x122E6C8 (right after the second poll): x21 = &b, so &a = x21 - 0x20.
#include "lib.hpp"
#include "config.hpp"
#include <cstring>

namespace {

template <typename T>
T& Abs(ptrdiff_t off) { return *reinterpret_cast<T*>(exl::util::modules::GetTargetStart() + off); }

template <typename F>
F Fn(ptrdiff_t off) { return reinterpret_cast<F>(exl::util::modules::GetTargetStart() + off); }

// Output of PollPad. Sticks are -1..1 (y up), triggers 0..1.
struct GameInput {
    s32 active;
    float lx, ly, rx, ry;
    float zl, zr;
    u32 buttons;
};
static_assert(sizeof(GameInput) == 0x20);

// Game button bits (from 0x153172C).
enum : u32 {
    kLeft = 0x1, kRight = 0x2, kDown = 0x4, kUp = 0x8,
    kA = 0x10, kB = 0x20, kX = 0x40, kY = 0x80,
    kPlus = 0x100, kMinus = 0x200, kL = 0x400, kLS = 0x1000, kR = 0x2000, kRS = 0x8000,
    kDpad = kLeft | kRight | kDown | kUp,
    kFace = kA | kB | kX | kY,
    kKnown = kDpad | kFace | kPlus | kMinus | kL | kLS | kR | kRS,
};

constexpr ptrdiff_t kPollPad          = 0x1530EA4;  // bool(void* pad, GameInput* out)
constexpr ptrdiff_t kGetNpadStyleSet  = 0x1583AA0;  // import thunk: u32(const u32& id)
constexpr ptrdiff_t kSetSupportedIds  = 0x1583890;  // import thunk: void(const u32* ids, u64 count)
constexpr ptrdiff_t kAfterSetIds      = 0xFFC680;   // mov w0, wzr (first instruction after SetSupportedNpadIdType)
constexpr ptrdiff_t kStyleSetArg      = 0xFFC688;   // mov w0, #7 (argument of SetSupportedNpadStyleSet)
constexpr ptrdiff_t kAfterPolls       = 0x122E6C8;  // ldr w8, [x20, #0x3fc] (after PollPad(Handheld))
constexpr ptrdiff_t kHandheldStyleIdx = 0x50CC000;  // Handheld pad object +8
constexpr ptrdiff_t kNo1StyleIdx      = 0x50CBC18;  // No1 pad object +8
constexpr ptrdiff_t kPauseFlags       = 0x1DB5280;  // u32; set 0x132AC08, cleared 0x132A234 / 0x12B8624
constexpr u32 kPauseBit = 0x10000;                  // pause menu open
constexpr ptrdiff_t kLoopFlags        = 0x1DB5260;  // u32 main loop flags (bit 23 = 30 FPS mode, bit 14 = paused)
constexpr u32 kMenuBit = 0x200;                     // title / main menu / save select (set until gameplay)
constexpr ptrdiff_t kEventQueueCount  = 0x1DB53B0;  // u64 entries in the event request queue (array 0x1DB53A8,
                                                    // capacity 0x1DB53B8, mutex 0x1DB5370; pushed by event scripts,
                                                    // emptied at 0xFDCF10 / 0xFDD214). 1-2 during dialogue.
constexpr ptrdiff_t kMessageBlock     = 0x5036648;  // u32 + u8 at +4 (0x503664C, set at 0x1376868): message on screen.
                                                    // 85 game sites skip actor logic while either is non-zero. In testing
                                                    // 0x503664C = 1 exactly while Cereza's examine comment was shown.
constexpr u32 kWorldPausedBit = 0x4000;             // in kLoopFlags: world stopped (pause menu, pop-up windows)
constexpr ptrdiff_t kUiMode           = 0x4FF0074;  // u32 UI mode: 0 in gameplay, 1 pop-up window, 0x800 dialogue,
                                                    // 4 / 5 menu opened from the D-pad
constexpr u32 kUiText = 0x800;                      // in kUiMode: dialogue / text, the game may go on
constexpr ptrdiff_t kUiPrompt         = 0x4FBF368;  // u32: 1 with pop-ups, but also stuck at 1 for minutes in
                                                    // Tir na nOg. Logged only; stopped + UI mode cover pop-ups.

constexpr u32 kNpadNo2 = 1;

using PollPadFn = bool (*)(void* pad, GameInput* out);
using GetStyleFn = u32 (*)(const u32* id);
using SetIdsFn = void (*)(const u32* ids, u64 count);

// P2's pad object, laid out like the game's (0x3E8 bytes apart in .bss).
alignas(16) u8 g_p2Pad[0x400];
u64 g_frame = 0;
bool g_menu = false;     // menu mode: P1 normal controller, P2 ignored (COOP_MENU_TOGGLE)
bool g_text = false;     // text mode: co-op, plus P1's face buttons as A / B / X / Y
u32 g_p1Prev = 0;        // P1 buttons last frame (edge detection)
bool g_p2Seen = false;
u32 g_p2Style = 0;

[[maybe_unused]] int Milli(float f) { return static_cast<int>(f * 1000.0f); }

// Style index as PollPad computes it from the style set: JoyDual 0, FullKey 1, Handheld 2, JoyLeft 3, JoyRight 4,
// none 5. Pre-setting it in P2's pad object keeps PollPad off its re-init path, which would set up vibration on our
// pad object (the game's pad objects hold constructed VibrationTargets at +0x160; ours does not).
s32 StyleIndex(u32 styles) {
    if (styles & 0x1) return 1;
    if (styles & 0x4) return 0;
    if (styles & 0x2) return 2;
    if (styles & 0x8) return 3;
    if (styles & 0x10) return 4;
    return 5;
}

// P2 through the game's own PollPad, so P2 gets the same scaling and sideways Joy-Con handling as P1.
bool PollP2(GameInput& out) {
    std::memset(&out, 0, sizeof(out));
    u32 id = kNpadNo2;
    u32 styles = Fn<GetStyleFn>(kGetNpadStyleSet)(&id);
    if (styles != g_p2Style) {
        Logging.Log("[ORCOOP] P2 style %x -> %x (index %d) frame=%lu", g_p2Style, styles, StyleIndex(styles), g_frame);
        g_p2Style = styles;
    }
    if (StyleIndex(styles) == 5) return false;
    u32* pad = reinterpret_cast<u32*>(g_p2Pad);
    pad[0] = 1;                                  // enabled
    pad[1] = 1;
    pad[2] = static_cast<u32>(StyleIndex(styles));
    pad[4] = kNpadNo2;
    Fn<PollPadFn>(kPollPad)(g_p2Pad, &out);
    if (!g_p2Seen) {
        g_p2Seen = true;
        Logging.Log("[ORCOOP] P2 connected (style %x) frame=%lu", styles, g_frame);
    }
    return true;
}

// The game's own handheld merge: buttons OR, each axis keeps the larger magnitude, triggers likewise.
float Larger(float a, float b) { return __builtin_fabsf(b) > __builtin_fabsf(a) ? b : a; }

void MergeInto(GameInput& a, const GameInput& b) {
    a.lx = Larger(a.lx, b.lx); a.ly = Larger(a.ly, b.ly);
    a.rx = Larger(a.rx, b.rx); a.ry = Larger(a.ry, b.ry);
    a.zl = Larger(a.zl, b.zl); a.zr = Larger(a.zr, b.zr);
    a.buttons |= b.buttons;
}

void PickStick(const GameInput& in, int mode, float& x, float& y) {
    bool right = mode == 1 || (mode == 2 && in.rx * in.rx + in.ry * in.ry > in.lx * in.lx + in.ly * in.ly);
    x = right ? in.rx : in.lx;
    y = right ? in.ry : in.ly;
}

// Face buttons <-> D-pad by position: Y/left, X/up, A/right, B/down.
[[maybe_unused]] u32 FaceToDpad(u32 b) {
    return (b & kY ? kLeft : 0) | (b & kX ? kUp : 0) | (b & kA ? kRight : 0) | (b & kB ? kDown : 0);
}
[[maybe_unused]] u32 DpadToFace(u32 b) {
    return (b & kLeft ? kY : 0) | (b & kUp ? kX : 0) | (b & kRight ? kA : 0) | (b & kDown ? kB : 0);
}

bool Sideways(s32 styleIndex) { return styleIndex == 3 || styleIndex == 4; }

// Single sideways Joy-Con (style index 3 / 4): PollPad turns SL into L and SR into R and never sets ZL / ZR, so the
// player could not attack or use Umbran Arts. For the player's own character: SR = trigger (ZL for Cereza, ZR for
// Cheshire), SL = shoulder (L for Cereza, R for Cheshire); Cheshire's stick click becomes L (Hug Mode in).
void FixSideways(GameInput& in, bool cheshire) {
    u32 b = in.buttons;
    float trigger = b & kR ? 1.0f : 0.0f;
    bool shoulder = b & kL;
    b &= ~(kL | kR);
    if (cheshire) {
        in.zr = trigger;
        if (shoulder) b |= kR;
        if (b & kLS) b = (b & ~kLS) | kL;
    } else {
        in.zl = trigger;
        if (shoulder) b |= kL;
    }
    in.buttons = b;
}

// One controller from two: P1 gets the left half of the game's controls (Cereza), P2 the right half (Cheshire).
GameInput Compose(GameInput p1, GameInput p2) {
    if (Sideways(Abs<s32>(kNo1StyleIdx))) FixSideways(p1, false);
    if (Sideways(StyleIndex(g_p2Style))) FixSideways(p2, true);
    GameInput o{};
    o.active = p1.active;
    PickStick(p1, COOP_P1_STICK, o.lx, o.ly);
    PickStick(p2, COOP_P2_STICK, o.rx, o.ry);
    o.zl = p1.zl > p1.zr ? p1.zl : p1.zr;
    o.zr = p2.zl > p2.zr ? p2.zl : p2.zr;

    u32 b1 = p1.buttons, b2 = p2.buttons, b = 0;
    b |= b1 & (kDpad | kPlus | kMinus);
    if (b1 & (kL | kR)) b |= kL;
    if (b1 & (kLS | kRS)) b |= kLS;
#if COOP_P1_FACE_TO_DPAD
    b |= FaceToDpad(b1);
#endif

    b |= b2 & kFace;                             // P2's + and - are never passed: P1 owns pause and menus
    if (b2 & kR) b |= kR;
    if (b2 & kL) b |= COOP_P2_HUG ? kL : kR;
    if (b2 & (kLS | kRS)) b |= kRS;
#if COOP_P2_DPAD_TO_FACE
    b |= DpadToFace(b2);
#endif

    b |= (b1 | b2) & ~kKnown;                    // bits this table does not know: pass them on unchanged
    o.buttons = b;
    return o;
}

#if COOP_DIAG
// Wide state logger, to find the state of the examine-comment box (no change in 0x1DB0000-0x1DB8000, event queue
// empty). Watches the game's globals in two regions, 1/8 of them per frame, and logs words that changed only while
// armed: for ~20 s after P1 presses L or a face button outside menus (interact, then the comment). Words that change
// on two visits in a row are muted for good; each word logs at most 16 times.
struct Region { ptrdiff_t off; u32 words; };
constexpr Region kRegions[] = {
    {0x1D60000, 0xA0000 / 4},                       // end of .data + start of .bss (0x1DB5xxx state block)
    {0x4F00000, 0x1F5000 / 4},                      // .bss globals up to the end of main (0x50F5F60): UI 0x4FF1760, input 0x50CBC10
};
constexpr u32 kWatchWords = 0xA0000 / 4 + 0x1F5000 / 4;
constexpr u32 kSlices = 8;
u32 g_wLast[kWatchWords];
u32 g_wLastVisit[kWatchWords];                      // visit (sweep) of the last change + 1
u8 g_wLogs[kWatchWords];                            // lines logged; 0xFF = muted
u32 g_wSweep = 0, g_wLines = 0;
u64 g_armedUntil = 0;
bool g_wReady = false;

void StateFrame(bool arm) {
    if (arm) {
        if (g_frame >= g_armedUntil) Logging.Log("[ORCOOP] watch armed frame=%lu", g_frame);
        g_armedUntil = g_frame + 4800;
    }
    bool armed = g_frame < g_armedUntil;
    u32 slice = static_cast<u32>(g_frame % kSlices);
    if (slice == 0) g_wSweep++;
    u32 base = 0;
    for (const Region& r : kRegions) {
        const u32* cur = &Abs<u32>(r.off);
        u32 from = r.words * slice / kSlices, to = r.words * (slice + 1) / kSlices;
        for (u32 k = from; k < to; k++) {
            u32 i = base + k, v = cur[k];
            if (!g_wReady) { g_wLast[i] = v; continue; }
            if (v == g_wLast[i]) continue;
            u32 old = g_wLast[i];
            g_wLast[i] = v;
            bool fast = g_wLastVisit[i] == g_wSweep;   // also changed on the previous visit
            g_wLastVisit[i] = g_wSweep + 1;
            if (g_wLogs[i] == 0xFF) continue;
            if (fast) { g_wLogs[i] = 0xFF; continue; }
            if (!armed || g_wLogs[i] >= 16 || g_wLines >= 8000) continue;
            g_wLogs[i]++;
            g_wLines++;
            Logging.Log("[ORCOOP] watch %lx: %08x -> %08x frame=%lu", r.off + k * 4, old, v, g_frame);
        }
        base += r.words;
    }
    if (slice == kSlices - 1) g_wReady = true;
}
#endif

// After both game polls: P1 = No1 (+ Handheld when the console is in handheld), P2 = No2.
HOOK_DEFINE_INLINE(SplitHook) {
    static void Callback(exl::hook::InlineCtx* ctx) {
        GameInput* hh = reinterpret_cast<GameInput*>(ctx->X[21]);
        GameInput* no1 = hh - 1;

        GameInput p1 = *no1;
        if (Abs<s32>(kHandheldStyleIdx) == 2) MergeInto(p1, *hh);
        GameInput p2;
        bool p2on = PollP2(p2);

        // Two levels, both from the game's own state.
        // Menu mode (the world waits for input): pause flag, title / main menu bit, world-stopped bit,
        // UI mode 1 / 4 / 5. P1 gets a normal controller and P2 is ignored.
        // Text mode (text on screen, the game may go on: Cereza can walk during both): event queue,
        // message block, UI mode 0x800. Co-op goes on, and P1's face buttons also pass as A / B / X / Y so P1
        // can advance or close the text.
        u32 loop = Abs<u32>(kLoopFlags);
        bool paused = (Abs<u32>(kPauseFlags) & kPauseBit) != 0;
        bool title = (loop & kMenuBit) != 0;
        bool stopped = (loop & kWorldPausedBit) != 0;
        u64 events = Abs<u64>(kEventQueueCount);
        u32 msgWord = Abs<u32>(kMessageBlock);
        u8 msgOpen = Abs<u8>(kMessageBlock + 4);
        u32 ui = Abs<u32>(kUiMode), prompt = Abs<u32>(kUiPrompt);
        u32 uiMenu = ui & ~kUiText;
        bool menu = COOP_MENU_TOGGLE && (paused || title || stopped || uiMenu != 0);
        bool text = COOP_MENU_TOGGLE && !menu && (events != 0 || msgWord != 0 || msgOpen != 0 || (ui & kUiText) != 0);
        if (menu != g_menu || text != g_text) {
            g_menu = menu;
            g_text = text;
            Logging.Log("[ORCOOP] mode %s (pause=%d title/menu=%d stopped=%d events=%lu msg=%x/%d ui=%x prompt=%x) "
                        "frame=%lu", menu ? "MENU" : text ? "TEXT" : "PLAY", paused, title, stopped, events, msgWord,
                        msgOpen, ui, prompt, g_frame);
        }
        u32 p1Was = g_p1Prev;
        g_p1Prev = p1.buttons;

        // No P2 controller: P1 plays both characters as in vanilla.
        GameInput o = g_menu || !p2on ? p1 : Compose(p1, p2);
        if (g_text && p2on) o.buttons |= p1.buttons & kFace;
        *no1 = o;
        // The game merges the handheld result into No1 after this point; that input is already in p1.
        std::memset(&hh->lx, 0, sizeof(GameInput) - sizeof(s32));

#if COOP_DIAG
        // New presses per player and the game buttons they became (first 400 per player), so a test run shows every
        // action reaching the right character.
        static u32 p2Prev = 0, p1Logged = 0, p2Logged = 0;
        u32 new1 = p1.buttons & ~p1Was, new2 = p2.buttons & ~p2Prev;
        if (new1 && p1Logged < 400) {
            p1Logged++;
            Logging.Log("[ORCOOP] P1 press %x -> out %x (menu=%d) frame=%lu", new1, o.buttons, g_menu, g_frame);
        }
        if (new2 && p2Logged < 400) {
            p2Logged++;
            Logging.Log("[ORCOOP] P2 press %x -> out %x (menu=%d) frame=%lu", new2, o.buttons, g_menu, g_frame);
        }
        p2Prev = p2.buttons;
        StateFrame(!g_menu && (new1 & (kL | kR | kFace)) != 0);
        if (g_frame % 600 == 0)
            Logging.Log("[ORCOOP] frame=%lu menu=%d p2=%d p1b=%x p2b=%x out=%x L=%d,%d R=%d,%d", g_frame, g_menu, p2on,
                        p1.buttons, p2.buttons, o.buttons, Milli(o.lx), Milli(o.ly), Milli(o.rx), Milli(o.ry));
#else
        (void)p2on; (void)p1Was;
#endif
        g_frame++;
    }
};

// After the game's SetSupportedNpadIdType({No1, Handheld}): set it again with No2 added, or hid never reports P2.
HOOK_DEFINE_INLINE(SupportedIdsHook) {
    static void Callback(exl::hook::InlineCtx*) {
        static const u32 ids[] = {0, kNpadNo2, 0x20};
        Fn<SetIdsFn>(kSetSupportedIds)(ids, 3);
        Logging.Log("[ORCOOP] supported npads = No1, No2, Handheld");
    }
};

bool Check(ptrdiff_t off, u32 expected) {
    u32 got = Abs<u32>(off);
    if (got != expected) Logging.Log("[ORCOOP] %lx MISMATCH got=%08x want=%08x", off, got, expected);
    return got == expected;
}

}  // namespace

extern "C" void exl_main(void* x0, void* x1) {
    exl::hook::Initialize();
    Logging.Log("[ORCOOP] v0.8.0 p1stick=%d p2stick=%d p1face=%d p2dpad=%d p2hug=%d menu=%d joycon=%d diag=%d",
                COOP_P1_STICK, COOP_P2_STICK, COOP_P1_FACE_TO_DPAD, COOP_P2_DPAD_TO_FACE, COOP_P2_HUG,
                COOP_MENU_TOGGLE, COOP_SINGLE_JOYCON, COOP_DIAG);

    // Wrong game build: install nothing.
    if (!Check(kAfterSetIds, 0x2A1F03E0) || !Check(kStyleSetArg, 0x528000E0) || !Check(kAfterPolls, 0xB943FE88) ||
        !Check(kPollPad, 0xA9BC7BFD))
        return;

#if COOP_SINGLE_JOYCON
    // SetSupportedNpadStyleSet(7) -> (0x1F): add JoyLeft and JoyRight. PollPad already handles both (style 3 / 4).
    exl::patch::RandomAccessPatcher p;
    p.Write<u32>(kStyleSetArg, 0x528003E0);     // mov w0, #0x1f
    p.Flush();
#endif
    SupportedIdsHook::InstallAtOffset(kAfterSetIds);
    SplitHook::InstallAtOffset(kAfterPolls);
    Logging.Log("[ORCOOP] hooks installed");
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("Default exception handler called!");
}
