#include "Arduino.h"
#include "SD_MMC.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include "WiFiClientSecure.h"
#include <WebServer.h>
#include <vector>
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"

extern "C" {
#include "config.h"
#include "src/i2c_bsp/i2c_bsp.h"
#include "src/audio/audio_bsp.h"
}

#include "src/power/board_power_bsp.h"
#include "src/display/epaper_driver_bsp.h"
#include "logo_bitmap.h"
#include "secrets.h"
#include "sounds.h"

#include <Adafruit_GFX.h>
#include <pgmspace.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>

#include "types.h"
#include "globals.h"
#include "src/app/draw.h"
#include "src/app/battery.h"
#include "src/app/rtc.h"
#include "src/app/notes.h"
#include "src/app/ui.h"
#include "src/app/buttons.h"
#include "src/app/network.h"
#include "src/app/sleep.h"
#include "src/app/record.h"
#include "src/app/long_record.h"

// All pin, timing, path and threshold constants live in config.h.

// ─── Content arrays ───────────────────────────────────────────────────────
const char* DEFAULT_TAGS[]    = { "Note", "Work", "Idea", "Instrument", "Private", "Task", "Bot" };
const char* MENU_ITEMS[]     = { "Notes", "Tags", "Sync", "Settings" };
const char* SETTINGS_ITEMS[] = { "Sounds", "Read Aloud", "Transfer", "Device" };

// ─── Global variable definitions ─────────────────────────────────────────
board_power_bsp_t      board(EPD_PWR_PIN, Audio_PWR_PIN, VBAT_PWR_PIN);
epaper_driver_display* display = nullptr;

std::vector<NoteEntry> noteIndex;

AppState state          = STATE_IDLE;
int      listCursor     = 0;
int      tagCursor      = 2;
int      menuCursor     = 0;
int      settingsCursor = 0;
bool     soundsOn       = true;
bool     ttsOn          = true;   // read bot answers aloud (menu-toggleable)
int      activeFilter   = -1;
int      lastRecNum     = -1;

uint32_t lastActivityMs      = 0;
bool     wokeFromUltraSleep  = false;
bool     wakeToMenuRequested = false;
bool     wakeToRecRequested  = false;

uint32_t tickerLastMs = 0;
int      tickerOffset = 0;
int      tickerCursor = -1;

WebServer transferServer(80);
bool      transferServerActive = false;
String    transferUrl          = "";

bool timeReady    = false;
bool audioPlaying = false;
bool stopPlayback = false;

int detailScrollPage = 0;
int detailTotalLines = 0;

uint32_t lastBatCheckMs    = 0;
bool     batLowWarned      = false;
bool     batWarnActive     = false;
uint32_t batWarnShowUntilMs = 0;

char tags[20][32];
int  tagCount = 0;

// ─── Power latch ──────────────────────────────────────────────────────────
void keepBatteryPowerOn() {
  pinMode(PWR_HOLD_PIN, OUTPUT);
  digitalWrite(PWR_HOLD_PIN, HIGH);
}

// ─── Flow functions ───────────────────────────────────────────────────────
void startRecordFlow() {
  state = STATE_RECORDING;
  showRecording();

  palaSoundSetEnabled(false);
  bool recOk = record();
  palaSoundSetEnabled(true);

  if (!recOk) {
    if (g_lastRecTooShort) {
      showError("TOO SHORT");   // intentional discard, not a failure
    } else {
      showError("REC FAIL");
    }
    delay(1400);
    state = STATE_IDLE;
    showIdle();
    return;
  }

  soundSaved();

  state = STATE_SAVED;
  showSaved(lastRecNum);
  delay(900);

  tagCursor = min(2, max(tagCount - 1, 0));
  state = STATE_TAG_SELECT;
  showTagSelect(tagCursor);
}

// ─── Long (meeting) recording ──────────────────────────────────────────────
static uint32_t longUiLastMs = 0;

// Both the manual stop (PWR double-press) and the auto-stop (cap / low battery)
// converge here: finalize already done by longRecStop(), then go to tag-select,
// exactly like a short note.
static void finishLongRecToTagSelect() {
  soundSaved();
  state = STATE_SAVED;
  showSaved(lastRecNum);
  delay(900);

  tagCursor = min(2, max(tagCount - 1, 0));
  state = STATE_TAG_SELECT;
  showTagSelect(tagCursor);
}

void startLongRecordFlow() {
  if (!longRecStart()) {
    showError("REC FAIL");
    delay(1600);
    state = STATE_IDLE;
    showIdle();
    return;
  }
  state = STATE_RECORDING_LONG;

  // Draw the initial full countdown (full refresh to start from a clean panel).
  uint32_t total = (uint32_t)LONGREC_MAX_MIN * 60u * 1000u;
  showLongRecording(longRecRemainingMs(), total, /*fullClear=*/true);
  longUiLastMs = millis();
}

// Serviced every loop() tick while in STATE_RECORDING_LONG.
static void serviceLongRecording() {
  // 1) Keep audio flowing to SD (one chunk per tick). Returns false if it
  //    auto-stopped (hit the cap or low battery) inside the pump.
  bool stillRunning = longRecPump();

  // 2) Manual stop: PWR double-press.
  bool stopped = false;
  if (stillRunning && readPwrDouble()) {
    longRecStop();
    stopped = true;
  }

  if (!stillRunning || stopped) {
    finishLongRecToTagSelect();
    return;
  }

  // 3) Refresh the countdown every LONGREC_UI_INTERVAL_MS (10 s).
  //    NOTE: a full panel refresh blocks ~2 s, during which longRecPump() can't
  //    run and audio would gap. So during active recording we ONLY do partial
  //    refreshes. The screen is mostly static (just the ring + digits change),
  //    so ghosting stays mild over the session; we clear it fully once at start
  //    and the next screen after stop is a fresh full draw anyway.
  if (millis() - longUiLastMs >= LONGREC_UI_INTERVAL_MS) {
    longUiLastMs = millis();
    uint32_t total = (uint32_t)LONGREC_MAX_MIN * 60u * 1000u;
    showLongRecording(longRecRemainingMs(), total, /*fullClear=*/false);
  }
}

// ─── Bot lookup ─────────────────────────────────────────────────────────────
String botAnswer    = "";
int    botAnswerPage = 0;
static const char* BOT_TMP_WAV = "/bot_q.wav";

// Open-AP confirm gate: shows the warning screen and waits for an explicit
// BOOT (yes) or PWR (no). Returns the user's choice. A long no-answer timeout
// defaults to NO (safer). Passed to connectWifiMulti as the last-resort hook.
static bool confirmOpenAp(const String& ssid) {
  showOpenApConfirm(ssid);
  // Drain any held buttons first.
  while (digitalRead(BTN_REC) == LOW || digitalRead(BTN_PWR) == LOW) delay(5);
  delay(120);
  uint32_t t0 = millis();
  while (millis() - t0 < 30000) {            // 30s to decide, else NO
    if (digitalRead(BTN_REC) == LOW) {       // BOOT = yes
      while (digitalRead(BTN_REC) == LOW) delay(5);
      return true;
    }
    if (digitalRead(BTN_PWR) == LOW) {       // PWR = no
      while (digitalRead(BTN_PWR) == LOW) delay(5);
      return false;
    }
    delay(10);
  }
  return false;
}

// Connect to WiFi (shared by sync and bot). Returns true if connected.
static bool ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  showWifiConnecting(0, 20);
  return connectWifiMulti(showWifiConnecting, confirmOpenAp);
}

// Save the Q&A as a Bot-tagged note so it's findable in the portal. Reuses the
// note machinery: the question audio becomes the note's WAV, and the transcript
// file holds "Q: ... / A: ...".
// (Bot queries are intentionally not stored — no saveBotNote. The temp question
// recording is deleted after the answer is shown.)

void startBotFlow() {
  // 1) Record the spoken question for a fixed window (no per-second screen
  //    refresh — an e-ink partial refresh mid-record would gap the audio).
  state = STATE_BOT_RECORDING;
  showBotRecording();
  if (!recordQuestion(BOT_TMP_WAV)) {
    showError("NO AUDIO");
    delay(1500);
    state = STATE_IDLE; showIdle(); return;
  }

  // 2) Need WiFi for both Whisper and the chat call.
  state = STATE_BOT_THINKING;
  showBotThinking();
  if (!ensureWifi()) {
    showError("NO WIFI");
    delay(1800);
    state = STATE_IDLE; showIdle(); return;
  }

  // 3) Transcribe the question with Whisper (to a scratch text via transcribe()
  //    on a temp note number is overkill; do a direct transcription instead).
  String question;
  if (!transcribeToText(BOT_TMP_WAV, question) || question.length() == 0) {
    SyncError e = lastSyncError();
    if      (e == SYNC_ERR_NO_CREDIT) showSyncError("Insufficient", "ChatGPT credit.", "Add funds.");
    else if (e == SYNC_ERR_AUTH)      showSyncError("API key rejected.", "Check your key", "in secrets.h");
    else                              showError("ASK FAILED");
    soundDelete();
    delay(3000);
    WiFi.disconnect(true);
    state = STATE_IDLE; showIdle(); return;
  }

  // 4) Ask the chat model. Keep WiFi UP — TTS (below) needs it too.
  String answer;
  bool ok = botAsk(question, answer);

  if (!ok) {
    WiFi.disconnect(true);
    SyncError e = lastSyncError();
    if      (e == SYNC_ERR_NO_CREDIT) showSyncError("Insufficient", "ChatGPT credit.", "Add funds.");
    else if (e == SYNC_ERR_AUTH)      showSyncError("API key rejected.", "Check your key", "in secrets.h");
    else                              showSyncError("Bot failed.", "Could not reach", "the server.");
    soundDelete();
    delay(3000);
    state = STATE_IDLE; showIdle(); return;
  }

  // 5) Show the "aha!" beat. Bot queries are NOT stored — no note, transcript,
  //    or index entry — so delete the temp question audio.
  SD_MMC.remove(BOT_TMP_WAV);
  showBotResponse();
  delay(3000);

  botAnswer = answer;
  botAnswerPage = 0;

  // 6) If read-aloud is enabled, fetch the voice FIRST (keeping the "aha!"
  //    screen up meanwhile) so the answer text appears in sync with the audio.
  //    If TTS is off or the fetch fails, just show the text right away.
  bool haveVoice = false;
#if TTS_ENABLED
  if (ttsOn) {
    drawWifiCorner();               // small "working" indicator over the "aha!" screen
    haveVoice = ttsFetch(answer);   // "aha!" screen stays up during the download
  }
#endif
  WiFi.disconnect(true);

  // Now reveal the answer text.
  state = STATE_BOT_ANSWER;
  showBotAnswer(botAnswer, botAnswerPage);

  // Play the downloaded voice (blocking; a BOOT tap during playback stops it).
  if (haveVoice) {
    speakPcmFile(TTS_TMP_PCM);
    SD_MMC.remove(TTS_TMP_PCM);
  }
}

void startSyncFlow() {
  showWifiConnecting(0, 20);
  connectWifiMulti(showWifiConnecting, confirmOpenAp);

  if (WiFi.status() == WL_CONNECTED) {
    syncTimeFromNTP(6000);
    transcribeAll();
    loadIndex();
    WiFi.disconnect(true);

    // Surface a specific reason if the pass failed, instead of silently
    // finishing on a hang. 429 = no OpenAI credit; 401 = bad API key.
    SyncError err = lastSyncError();
    if (syncWasCancelled()) {
      showError("SYNC STOPPED");
      soundBack();
      // Wait for the cancel button to be released so it doesn't carry into the
      // next screen (e.g. a held BOOT starting a note).
      while (digitalRead(BTN_REC) == LOW || digitalRead(BTN_PWR) == LOW) delay(5);
      delay(400);
    } else if (err == SYNC_ERR_NO_CREDIT) {
      showSyncError("Insufficient", "ChatGPT credit.", "Add funds.");
      soundDelete();
      delay(3200);
    } else if (err == SYNC_ERR_AUTH) {
      showSyncError("API key rejected.", "Check your key", "in secrets.h");
      soundDelete();
      delay(3200);
    } else if (err == SYNC_ERR_HTTP || err == SYNC_ERR_NETWORK) {
      showSyncError("Sync failed.", "Could not reach", "the server.");
      soundDelete();
      delay(3200);
    } else {
      showDone();
      soundSuccess();
      delay(1600);
    }
  } else {
    showError("NO WIFI");
    delay(1800);
  }

  if (wakeToMenuRequested) {
    menuCursor = 0;
    state = STATE_MENU;
    showMenu(menuCursor);
  } else {
    showIdle();
  }
}

void startTransferMode() {
  state = STATE_TRANSFER;
  showTransferConnecting();

  connectWifiMulti(nullptr, confirmOpenAp);

  if (WiFi.status() != WL_CONNECTED) {
    showError("NO WIFI");
    delay(1600);
    state = STATE_SETTINGS;
    showSettings(settingsCursor);
    return;
  }

  syncTimeFromNTP(8000);
  setupTransferServer();
  transferServer.begin();
  transferServerActive = true;

  IPAddress ip = WiFi.localIP();
  transferUrl = ip.toString();
  showTransferMode(transferUrl.c_str());
}

// ─── Setup ────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Pala Note " FIRMWARE_VERSION " ===");

  pinMode(BTN_REC, INPUT_PULLUP);
  pinMode(BTN_PWR, INPUT_PULLUP);

  board.VBAT_POWER_ON();

  wokeFromUltraSleep  = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1);
  delay(50);

  wakeToMenuRequested = (wokeFromUltraSleep && digitalRead(BTN_PWR) == LOW);
  wakeToRecRequested  = (wokeFromUltraSleep && digitalRead(BTN_REC) == LOW);

  resetActivity();
  keepBatteryPowerOn();
  delay(20);

  board.POWEER_EPD_ON();
  board.POWEER_Audio_ON();
  delay(200);

  custom_lcd_spi_t dispCfg = {};
  dispCfg.cs       = EPD_CS_PIN;
  dispCfg.dc       = EPD_DC_PIN;
  dispCfg.rst      = EPD_RST_PIN;
  dispCfg.busy     = EPD_BUSY_PIN;
  dispCfg.mosi     = EPD_MOSI_PIN;
  dispCfg.scl      = EPD_SCK_PIN;
  dispCfg.spi_host = EPD_SPI_NUM;
  dispCfg.buffer_len = (200*200)/8;

  display = new epaper_driver_display(200, 200, dispCfg);
  display->EPD_Init();
  display->EPD_Clear();
  display->EPD_DisplayPartBaseImage();
  display->EPD_Init_Partial();

  i2c_master_Init();
  delay(50);

  audio_bsp_init();
  audio_play_init();

  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  if (!SD_MMC.begin("/sdcard", true)) {
    showError("SD ERR");
    while (true) delay(1000);
  }
  if (!SD_MMC.exists(NOTES_DIR)) SD_MMC.mkdir(NOTES_DIR);
  loadTags();
  loadIndex();
  Serial.printf("[SD] %d notes\n", (int)noteIndex.size());

  if (wakeToMenuRequested) {
    menuCursor = 0;
    state = STATE_MENU;
    showMenu(menuCursor);
  } else if (wakeToRecRequested) {
    startRecordFlow();
  } else {
    showIdle();
  }
}

// ─── Main loop ────────────────────────────────────────────────────────────
void loop() {

  if (state != STATE_RECORDING && state != STATE_TRANSFER && state != STATE_RECORDING_LONG) {
    if (millis() - lastActivityMs > ULTRA_SLEEP_MS) {
      enterUltraSleep();
      return;
    }
  }

  if (state == STATE_NOTE_LIST && activeTickerNeedsScroll(listCursor)) {
    if (millis() - tickerLastMs > TICKER_INTERVAL_MS) {
      tickerLastMs = millis();
      tickerOffset++;
      showNoteList(listCursor);
      return;
    }
  }

  if (transferServerActive) transferServer.handleClient();

  // Battery warning: dismiss after 2.5 s without blocking
  if (batWarnActive && millis() >= batWarnShowUntilMs) {
    batWarnActive = false;
    switch (state) {
      case STATE_IDLE:           showIdle();                     break;
      case STATE_MENU:           showMenu(menuCursor);           break;
      case STATE_NOTE_LIST:      showNoteList(listCursor);       break;
      case STATE_NOTE_DETAIL:    showNoteDetail(listCursor);     break;
      case STATE_TAG_SELECT:     showTagSelect(tagCursor);       break;
      case STATE_TAG_BROWSER:    showTagBrowser(tagCursor);      break;
      case STATE_SETTINGS:       showSettings(settingsCursor);   break;
      case STATE_DEVICE_INFO:    showDeviceInfo();               break;
      case STATE_DELETE_CONFIRM: {
        int idx = noteAtFilteredIndex(listCursor);
        if (idx >= 0) showDeleteConfirm(noteIndex[idx].num);
        break;
      }
      default: break;
    }
  }

  // Periodic battery check
  if (state != STATE_RECORDING && state != STATE_RECORDING_LONG && !audioPlaying && !batWarnActive) {
    if (millis() - lastBatCheckMs > BAT_CHECK_INTERVAL_MS) {
      lastBatCheckMs = millis();
      int pct = readBatteryPercent();
      if (pct >= 0 && pct <= BAT_LOW_THRESHOLD && !batLowWarned) {
        batLowWarned        = true;
        batWarnActive       = true;
        batWarnShowUntilMs  = millis() + 2500;
        showBatteryLow(pct);
      } else if (pct > BAT_RECOVER_THRESHOLD) {
        batLowWarned = false;
      }
    }
  }

  // IDLE ─────────────────────────────────────────────────────────────────
  if (state == STATE_IDLE) {
    int rec = handleIdleRecEx();
    if (rec == 2) { soundSelect(); startBotFlow(); return; }  // double-press -> bot
    if (rec == 1) return;                                     // short note handled

    // PWR: double-press starts a long (meeting) recording; single opens menu.
    // readPwrDouble waits out the double-press window before confirming a
    // single, so the menu opens ~PWR_DOUBLE_MS after release rather than
    // instantly — the cost of overloading PWR with both gestures.
    bool pwrSingle = false;
    if (readPwrDouble(&pwrSingle)) {
      soundRecordingStart();
      startLongRecordFlow();
    } else if (pwrSingle) {
      soundSelect();
      menuCursor = 0;
      state = STATE_MENU;
      showMenu(menuCursor);
    }
  }

  // LONG RECORDING ───────────────────────────────────────────────────────
  else if (state == STATE_RECORDING_LONG) {
    serviceLongRecording();
  }

  // BOT ANSWER (paged) ───────────────────────────────────────────────────
  else if (state == STATE_BOT_ANSWER) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);
    int pages = botAnswerPages(botAnswer);
    if (rec == EV_SINGLE || rec == EV_LONG) {
      // Advance a page; wrap to exit after the last page.
      if (botAnswerPage + 1 < pages) {
        botAnswerPage++;
        showBotAnswer(botAnswer, botAnswerPage);
      } else {
        soundBack();
        state = STATE_IDLE; showIdle();
      }
    } else if (pwr == EV_SINGLE || pwr == EV_LONG) {
      soundBack();
      state = STATE_IDLE; showIdle();
    }
  }

  // TAG SELECT after recording ──────────────────────────────────────────
  else if (state == STATE_TAG_SELECT) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (rec == EV_SINGLE || rec == EV_LONG) {
      soundSelect();
      saveTag(lastRecNum, tags[constrain(tagCursor, 0, max(tagCount - 1, 0))]);
      enterUltraSleep();
    } else if (pwr == EV_SINGLE) {
      soundNext();
      if (tagCount > 0) tagCursor = (tagCursor + 1) % tagCount;
      showTagSelect(tagCursor);
    }
  }

  // MENU ────────────────────────────────────────────────────────────────
  else if (state == STATE_MENU) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (pwr == EV_SINGLE) {
      soundNext();
      menuCursor = (menuCursor + 1) % MENU_COUNT;
      showMenu(menuCursor);
    } else if (rec == EV_SINGLE) {
      soundSelect();
      if (menuCursor == 0) {
        activeFilter = -1; listCursor = 0;
        state = STATE_NOTE_LIST;
        showNoteList(listCursor);
      } else if (menuCursor == 1) {
        tagCursor = 0;
        state = STATE_TAG_BROWSER;
        showTagBrowser(tagCursor);
      } else if (menuCursor == 2) {
        startSyncFlow();
      } else {
        settingsCursor = 0;
        state = STATE_SETTINGS;
        showSettings(settingsCursor);
      }
    } else if (rec == EV_LONG || rec == EV_DOUBLE) {
      soundBack();
      state = STATE_IDLE;
      showIdle();
    }
  }

  // SETTINGS ────────────────────────────────────────────────────────────
  else if (state == STATE_SETTINGS) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (pwr == EV_SINGLE) {
      soundNext();
      settingsCursor = (settingsCursor + 1) % SETTINGS_COUNT;
      showSettings(settingsCursor);
    } else if (rec == EV_SINGLE) {
      soundSelect();
      if (settingsCursor == 0) {
        palaSoundSetEnabled(!palaSoundIsEnabled());
        showSettings(settingsCursor);
      } else if (settingsCursor == 1) {
        ttsOn = !ttsOn;                    // toggle read-aloud
        showSettings(settingsCursor);
      } else if (settingsCursor == 2) {
        startTransferMode();
      } else {
        state = STATE_DEVICE_INFO;
        showDeviceInfo();
      }
    } else if (rec == EV_DOUBLE || rec == EV_LONG) {
      soundBack();
      state = STATE_MENU;
      showMenu(menuCursor);
    }
  }

  // DEVICE INFO ─────────────────────────────────────────────────────────
  else if (state == STATE_DEVICE_INFO) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (rec == EV_DOUBLE || rec == EV_LONG || rec == EV_SINGLE || pwr == EV_SINGLE) {
      soundBack();
      state = STATE_SETTINGS;
      showSettings(settingsCursor);
    }
  }

  // TRANSFER MODE ───────────────────────────────────────────────────────
  else if (state == STATE_TRANSFER) {
    if (transferServerActive) transferServer.handleClient();
    ButtonEvent rec = readButtonEvent(BTN_REC);
    if (rec == EV_DOUBLE || rec == EV_LONG) {
      soundBack();
      stopTransferMode();
      state = STATE_SETTINGS;
      showSettings(settingsCursor);
    }
  }

  // TAG BROWSER ─────────────────────────────────────────────────────────
  else if (state == STATE_TAG_BROWSER) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (pwr == EV_SINGLE) {
      soundNext();
      if (tagCount > 0) tagCursor = (tagCursor + 1) % tagCount;
      showTagBrowser(tagCursor);
    } else if (rec == EV_SINGLE) {
      soundSelect();
      activeFilter = tagCursor; listCursor = 0;
      state = STATE_NOTE_LIST;
      showNoteList(listCursor);
    } else if (rec == EV_LONG || rec == EV_DOUBLE) {
      soundBack();
      state = STATE_MENU;
      showMenu(menuCursor);
    }
  }

  // NOTE LIST ───────────────────────────────────────────────────────────
  else if (state == STATE_NOTE_LIST) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);
    int count = filteredCount();

    if (pwr == EV_SINGLE && count > 0) {
      soundNext();
      listCursor = (listCursor + 1) % count;
      showNoteList(listCursor);
    } else if (pwr == EV_DOUBLE && count > 0) {
      soundNext();
      listCursor = (listCursor - 1 + count) % count;
      showNoteList(listCursor);
    } else if (rec == EV_SINGLE && count > 0) {
      soundSelect();
      detailScrollPage = 0;
      state = STATE_NOTE_DETAIL;
      showNoteDetail(listCursor);
    } else if (rec == EV_LONG || rec == EV_DOUBLE) {
      soundBack();
      state = STATE_MENU;
      showMenu(menuCursor);
    }
  }

  // NOTE DETAIL ─────────────────────────────────────────────────────────
  else if (state == STATE_NOTE_DETAIL) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (pwr == EV_SINGLE) {
      soundNext();
      const int linesPerPage = 7;
      int totalPages = (detailTotalLines + linesPerPage - 1) / linesPerPage;
      if (detailScrollPage + 1 < totalPages) {
        detailScrollPage++;
      } else {
        detailScrollPage = 0;
        int count = filteredCount();
        if (count > 0) listCursor = (listCursor + 1) % count;
      }
      showNoteDetail(listCursor);
    } else if (rec == EV_SINGLE) {
      int idx = noteAtFilteredIndex(listCursor);
      if (idx >= 0) {
        char wavPath[64];
        snprintf(wavPath, sizeof(wavPath), "%s/note_%03d.wav", NOTES_DIR, noteIndex[idx].num);
        showPlaybackOverlay();
        playWavFile(wavPath);
        showNoteDetail(listCursor);
      }
    } else if (rec == EV_LONG) {
      int idx = noteAtFilteredIndex(listCursor);
      if (idx >= 0) {
        state = STATE_DELETE_CONFIRM;
        showDeleteConfirm(noteIndex[idx].num);
      }
    } else if (rec == EV_DOUBLE) {
      soundBack();
      detailScrollPage = 0;
      state = STATE_NOTE_LIST;
      showNoteList(listCursor);
    }
  }

  // DELETE CONFIRM ──────────────────────────────────────────────────────
  else if (state == STATE_DELETE_CONFIRM) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (rec == EV_SINGLE) {
      int idx = noteAtFilteredIndex(listCursor);
      if (idx >= 0) {
        deleteNote(noteIndex[idx].num);
        soundDelete();
      }
      detailScrollPage = 0;
      listCursor = constrain(listCursor, 0, max(filteredCount() - 1, 0));
      state = STATE_NOTE_LIST;
      showNoteList(listCursor);
    } else if (pwr == EV_SINGLE || rec == EV_DOUBLE || rec == EV_LONG) {
      soundBack();
      state = STATE_NOTE_DETAIL;
      showNoteDetail(listCursor);
    }
  }

  // During long recording the blocking codec read already paces the loop;
  // an extra delay here would leave gaps the DMA ring can't always cover.
  if (state != STATE_RECORDING_LONG) delay(15);
}
