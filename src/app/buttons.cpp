#include "Arduino.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../types.h"
#include "buttons.h"

extern void startRecordFlow();
extern void resetActivity();

bool isDown(int pin) { return digitalRead(pin) == LOW; }

ButtonEvent readButtonEvent(int pin) {
  static bool pwrReady = true;

  if (pin == BTN_PWR) {
    if (!isDown(BTN_PWR)) { pwrReady = true; return EV_NONE; }
    delay(6);
    if (!isDown(BTN_PWR)) return EV_NONE;
    if (!pwrReady) return EV_NONE;
    pwrReady = false;
    resetActivity();
    return EV_SINGLE;
  }

  if (!isDown(pin)) return EV_NONE;
  delay(6);
  if (!isDown(pin)) return EV_NONE;

  uint32_t t0 = millis();
  while (isDown(pin)) {
    if (millis() - t0 > BTN_LONG_MS) {
      while (isDown(pin)) delay(3);
      resetActivity();
      return EV_LONG;
    }
    delay(3);
  }

  uint32_t rel = millis();
  while (millis() - rel < DOUBLE_MS) {
    if (isDown(pin)) {
      delay(10);
      if (isDown(pin)) {
        while (isDown(pin)) delay(3);
        resetActivity();
        return EV_DOUBLE;
      }
    }
    delay(3);
  }

  resetActivity();
  return EV_SINGLE;
}

// Returns an action code for the idle BOOT button:
//   0 = nothing happened
//   1 = press-and-hold -> short note (already started here)
//   2 = double-press   -> caller should start the bot lookup
int handleIdleRecEx() {
  if (!isDown(BTN_REC)) return 0;
  resetActivity();
  delay(20);
  if (!isDown(BTN_REC)) return 0;

  // Wait to see if this becomes a hold.
  uint32_t t0 = millis();
  while (isDown(BTN_REC) && millis() - t0 < REC_HOLD_MS) delay(5);

  if (isDown(BTN_REC)) {
    // Still held past the threshold -> short note.
    startRecordFlow();
    return 1;
  }

  // Released before the hold threshold = a quick tap. Watch briefly for a
  // SECOND tap; two quick taps = bot lookup. A lone tap does nothing (matching
  // the old behavior), so normal short-note holds are unaffected.
  uint32_t releaseMs = millis();
  while (millis() - releaseMs < BOT_REC_DOUBLE_MS) {
    if (isDown(BTN_REC)) {
      delay(8);
      if (isDown(BTN_REC)) {
        while (isDown(BTN_REC)) delay(4);   // consume the second tap
        resetActivity();
        return 2;                            // double-press -> bot
      }
    }
    delay(5);
  }
  return 1;   // single tap, consumed (no-op), same as before
}

// Backward-compatible wrapper: true if the BOOT gesture was consumed.
bool handleIdleRec() {
  return handleIdleRecEx() != 0;
}

// Non-blocking PWR press classifier.
// Returns true exactly once when a double-press completes. If a single press
// is confirmed (window elapsed with no second press), sets *singleOut=true.
// This lets the idle state drive both gestures from one detector: double ->
// start long recording, single -> open menu. Pass nullptr for singleOut where
// only the double matters (e.g. while long-recording).
//
// State machine across calls:
//   0 idle, waiting for first press
//   1 first press seen, waiting for release
//   2 released after 1st press, watching for a 2nd press within the window
bool readPwrDouble(bool* singleOut) {
  static uint8_t  st        = 0;
  static uint32_t releaseMs = 0;
  if (singleOut) *singleOut = false;

  bool down = isDown(BTN_PWR);

  switch (st) {
    case 0:
      if (down) { delay(6); if (isDown(BTN_PWR)) { st = 1; resetActivity(); } }
      break;
    case 1:
      if (!down) { releaseMs = millis(); st = 2; }
      break;
    case 2:
      if (down) {                       // second press within window -> double
        delay(6);
        if (isDown(BTN_PWR)) {
          while (isDown(BTN_PWR)) delay(3);
          st = 0;
          resetActivity();
          return true;
        }
      }
      if (millis() - releaseMs > PWR_DOUBLE_MS) {   // window expired -> single
        st = 0;
        if (singleOut) *singleOut = true;
      }
      break;
  }
  return false;
}
