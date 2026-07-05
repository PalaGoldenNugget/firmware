#pragma once
#include <Arduino.h>

// Result of a sync/transcription pass, so the UI can show a specific reason.
enum SyncError {
  SYNC_OK = 0,
  SYNC_ERR_NO_CREDIT,   // HTTP 429 — insufficient OpenAI credit / quota
  SYNC_ERR_AUTH,        // HTTP 401 — bad/missing API key
  SYNC_ERR_HTTP,        // other non-200 HTTP status
  SYNC_ERR_NETWORK      // couldn't connect / no response
};

bool transcribe(const String& wavPath, int noteNum);
void transcribeAll();
SyncError lastSyncError();   // reason for the most recent transcribeAll() pass

// Ask the chat model a question (already-transcribed text). On success returns
// true and puts the reply in `answerOut`. On failure returns false and sets the
// sync-error reason (429 = no credit, 401 = bad key) so the UI can explain it.
bool botAsk(const String& question, String& answerOut);
bool transcribeToText(const String& wavPath, String& out);

// Connect to the best available known WiFi network. Scans for in-range
// networks, matches them against /wifi.txt (SD) + the compiled WIFI_NETWORKS
// list, and tries them strongest-signal-first. Shows progress via `progress`.
// If no known network connects and an OPEN network is in range, calls
// `confirmOpen(ssid)` (if provided) — return true to join it, false to skip.
// Returns true if connected.
bool connectWifiMulti(void (*progress)(int tries, int maxTries),
                      bool (*confirmOpen)(const String& ssid) = nullptr);

String htmlEscape(const String& s);
String readSmallFile(const char* path, size_t maxLen = 1600);
String urlDecodeSimple(String s);
String portalCss();

void handlePortalRoot();
void handlePortalJson();
void handleExportTxt();
void sendFileByNum(const char* ext, const char* mime, bool attachment);
void handleTagsPage();
void handleTagAdd();
void handleTagDelete();
void handleNoteDelete();

void setupTransferServer();
void stopTransferMode();
