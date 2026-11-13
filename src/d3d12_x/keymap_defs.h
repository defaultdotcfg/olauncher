// Keyboard-as-controller map, shared by the bridge (onrush_d3d12_x.cpp: PollInput) and the launcher ("Controles" tab).
//
// Every action has up to KM_SLOTS keys (Windows virtual-key codes, 0 = none). One key may drive several actions:
// by default W/Up both accelerate (RT) and push the left stick up, so the same key drives and walks the menus.
// The D-pad has its own actions: in a race it opens the taunt wheel, so it must not share the driving keys.
//
// Launcher -> game: onrush.ini [Keyboard] <IniName>=<vk>,<vk> (decimal), passed to the game as
//   ONRUSH_KEYMAP="<IniName>=<hex>,<hex>;..."   (actions left out keep their defaults; an empty value means no key).
#pragma once

enum KeymapAction {
  KM_ACCEL, KM_BRAKE, KM_LEFT, KM_RIGHT, KM_UP, KM_DOWN,
  KM_DPAD_UP, KM_DPAD_DOWN, KM_DPAD_LEFT, KM_DPAD_RIGHT,
  KM_A, KM_B, KM_X, KM_Y, KM_LB, KM_RB, KM_VIEW, KM_MENU, KM_L3, KM_R3,
  KM_RS_LEFT, KM_RS_RIGHT, KM_RS_UP, KM_RS_DOWN,
  KM_COUNT
};

#define KM_SLOTS 2

struct KeymapDef { const char *ini; unsigned char keys[KM_SLOTS]; };

// VK codes: 0x0D Enter, 0x20 Space, 0x1B Esc, 0x08 Backspace, 0x09 Tab, 0x70 F1, 0x25..0x28 Left/Up/Right/Down.
static const KeymapDef kKeymapDefaults[KM_COUNT] = {
  { "Accelerate", { 'W', 0x26 } },  // RT
  { "Brake",      { 'S', 0x28 } },  // LT
  { "Left",       { 'A', 0x25 } },  // left stick
  { "Right",      { 'D', 0x27 } },
  { "Up",         { 'W', 0x26 } },
  { "Down",       { 'S', 0x28 } },
  { "DpadUp",     { '1', 0 } },     // D-pad (taunts in a race)
  { "DpadDown",   { '2', 0 } },
  { "DpadLeft",   { '3', 0 } },
  { "DpadRight",  { '4', 0 } },
  { "A",          { 0x0D, 0x20 } },
  { "B",          { 0x1B, 0x08 } },
  { "X",          { 'X', 0 } },
  { "Y",          { 'Y', 0 } },
  { "LB",         { 'Q', 0 } },
  { "RB",         { 'E', 0 } },
  { "View",       { 0x09, 0 } },
  { "Menu",       { 0x70, 0 } },
  { "L3",         { 0, 0 } },
  { "R3",         { 0, 0 } },
  { "LookLeft",   { 'J', 0 } },     // right stick
  { "LookRight",  { 'L', 0 } },
  { "LookUp",     { 'I', 0 } },
  { "LookDown",   { 'K', 0 } },
};

// Keys the bridge keeps for itself (F11 burst capture, F12 photo): the launcher refuses them.
static inline bool KeymapReserved(int vk) { return vk == 0x7A || vk == 0x7B; }
