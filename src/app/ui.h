#pragma once
#include <Arduino.h>

// Icons
void iconMicWhite(int cx, int cy);
void iconRecordBig(int cx, int cy);
void iconCheck(int cx, int cy, bool filled);
void iconError(int cx, int cy);
void iconThinking(int cx, int cy);
void iconTag(int cx, int cy);
void iconSync(int cx, int cy);
void iconWifi(int cx, int cy);
void iconNoteLines(int cx, int cy);

// Layout helpers
void drawHeader(const char* title, const char* rightInfo = nullptr);
void drawHints(const char* recLabel, const char* pwrLabel);
void drawBadge(int cx, int cy, const char* text, bool filled);
void drawPageDots(int cur, int total);
void drawChevronRight(int x, int cy, uint8_t c);
void drawTinyHint(const char* left, const char* right);
void drawKicker(const char* txt, int y);
void drawBatteryBadge();
void drawSoftFrame();
void drawProductWordmark(int cx, int y, uint8_t color);
void drawModernPill(int x, int y, int w, int h, const char* label, bool active);
void drawDotSelector(int cur, int total, int y);
void drawCheckSmall(int cx, int cy, uint8_t color);
void drawMinimalDocIcon(int cx, int cy, uint8_t color);
void drawMinimalTagIcon(int cx, int cy, uint8_t color);
void drawMinimalCloudIcon(int cx, int cy, uint8_t color);
void drawMenuTile(int x, int y, int w, int h, const char* label, int icon, bool active);
void drawNoteCard(int y, int idx, bool active);
void drawListMenuCard(int y, const char* title, const char* meta, bool active);

// Screens
void showIdle();
void showBatteryLow(int pct);
void showRecording();
void showLongRecording(uint32_t remainingMs, uint32_t totalMs, bool fullClear);
void showSaved(int num);
void showTagSelect(int cursor);
void showMenu(int cursor);
void showTagBrowser(int cursor);
void showNoteList(int cursor);
void showNoteDetail(int cursor);
void showDeleteConfirm(int noteNum);
void showTranscribing(int done, int total);
void showWifiConnecting(int attempt, int maxA);
void drawWifiCorner();
void showOpenApConfirm(const String& ssid);
void showDone();
void showError(const char* msg);
void showSyncError(const char* line1, const char* line2 = "", const char* line3 = "");
void showBotRecording(int secsLeft = -1);
void showBotThinking();
void showBotResponse();
void showBotAnswer(const String& answer, int page);   // returns nothing; total pages via botAnswerPages()
int  botAnswerPages(const String& answer);
void showUltraSleepScreen();
void showPlaybackOverlay();
void showTransferConnecting();
void showTransferMode(const char* ip);
void showSettings(int cursor);
void showDeviceInfo();
