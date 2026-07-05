#pragma once
#include <Arduino.h>

// ─── Long (meeting) recording ──────────────────────────────────────────────
// A 30-minute, non-blocking recording mode. Unlike record() (which blocks
// until the BOOT button is released), this is pumped from the main loop()
// one chunk at a time so the firmware can keep watching for the PWR
// double-press that stops it, redraw the countdown, and auto-stop at 00:00
// or on low battery.
//
// Lifecycle:
//   longRecStart()   -> opens the WAV, begins capture
//   longRecPump()     -> called every loop() tick while recording; reads one
//                        audio chunk to SD. Returns true while still running,
//                        false once it has stopped (auto/cap/battery).
//   longRecStop()     -> finalize + close the WAV, sets lastRecNum
//   longRecElapsedMs / longRecRemainingMs -> for the countdown UI
//
// Both the manual stop (PWR double-press, handled in the main loop) and the
// automatic stop at the cap land in the same place: finalize, then the caller
// transitions to STATE_TAG_SELECT — identical to the short-note flow.

bool     longRecStart();           // returns false if SD/alloc failed
bool     longRecPump();            // returns false when recording has ended
void     longRecStop();            // finalize WAV (safe to call once)
bool     longRecIsActive();

uint32_t longRecElapsedMs();
uint32_t longRecRemainingMs();
bool     longRecHitCap();          // true if it stopped by reaching the cap
