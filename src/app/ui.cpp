#include "Arduino.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../types.h"
#include "ui.h"
#include "draw.h"
#include "notes.h"
#include "battery.h"
#include "rtc.h"
#include "../../logo_bitmap.h"
#include "../../bot_images.h"
#include "../../sounds.h"
#include "SD_MMC.h"
#include <math.h>

#define W   200
#define H   200

// ─── Icons ────────────────────────────────────────────────────────────────

void iconMicWhite(int cx, int cy) {
  fillRect(cx-13, cy-36, 26, 44, WHITE);
  fillCircle(cx, cy-36, 13, WHITE);
  fillCircle(cx, cy+8,  13, WHITE);
  strokeCircle(cx, cy-4, 40, 5, WHITE);
  fillRect(cx-50, cy-50, 100, 50, BLACK);
  fillRect(cx-3,  cy+38, 6,  18, WHITE);
  fillRect(cx-24, cy+54, 48,  5, WHITE);
}

void iconRecordBig(int cx, int cy) {
  fillCircle(cx, cy, 36, WHITE);
  strokeCircle(cx, cy, 52, 5, WHITE);
  strokeCircle(cx, cy, 68, 2, WHITE);
}

void iconCheck(int cx, int cy, bool filled) {
  if (filled) {
    fillCircle(cx, cy, 44, BLACK);
    for (int t=-3;t<=3;t++) {
      line(cx-22, cy-2+t, cx-6, cy+17+t, WHITE);
      line(cx-6,  cy+17+t, cx+30, cy-22+t, WHITE);
    }
  } else {
    strokeCircle(cx, cy, 44, 3, BLACK);
    for (int t=-2;t<=2;t++) {
      line(cx-22, cy-2+t, cx-6, cy+17+t, BLACK);
      line(cx-6,  cy+17+t, cx+30, cy-22+t, BLACK);
    }
  }
}

void iconError(int cx, int cy) {
  strokeCircle(cx, cy, 44, 3, BLACK);
  for (int t=-3;t<=3;t++) {
    line(cx-22, cy-22+t, cx+22, cy+22+t, BLACK);
    line(cx+22, cy-22+t, cx-22, cy+22+t, BLACK);
  }
}

void iconThinking(int cx, int cy) {
  fillCircle(cx-28, cy, 8, BLACK);
  fillCircle(cx,    cy, 8, BLACK);
  fillCircle(cx+28, cy, 8, BLACK);
}

void iconTag(int cx, int cy) {
  const int pts[5][2] = {
    {cx-26, cy+4}, {cx-4, cy-18}, {cx+32, cy-18},
    {cx+32, cy+12}, {cx+4,  cy+36}
  };
  for(int i=0;i<4;i++) thickLine(pts[i][0],pts[i][1],pts[i+1][0],pts[i+1][1],4,BLACK);
  thickLine(pts[4][0],pts[4][1],pts[0][0],pts[0][1],4,BLACK);
  fillCircle(cx-2, cy-4, 5, BLACK);
}

void iconSync(int cx, int cy) {
  strokeCircle(cx, cy, 40, 4, BLACK);
  fillRect(cx+16, cy-46, 20, 20, WHITE);
  thickLine(cx+16, cy-36, cx+36, cy-36, 3, BLACK);
  thickLine(cx+36, cy-36, cx+26, cy-46, 3, BLACK);
  thickLine(cx+36, cy-36, cx+26, cy-26, 3, BLACK);
  fillRect(cx-36, cy+26, 20, 20, WHITE);
  thickLine(cx-36, cy+36, cx-16, cy+36, 3, BLACK);
  thickLine(cx-16, cy+36, cx-26, cy+26, 3, BLACK);
  thickLine(cx-16, cy+36, cx-26, cy+46, 3, BLACK);
}

void iconWifi(int cx, int cy) {
  int base = cy + 26;
  strokeCircle(cx, base, 50, 5, BLACK);
  strokeCircle(cx, base, 32, 5, BLACK);
  strokeCircle(cx, base, 14, 5, BLACK);
  fillRect(0, base, W, H - base, WHITE);
  fillCircle(cx, base, 5, BLACK);
}

void iconNoteLines(int cx, int cy) {
  fillRect(cx-32, cy-12, 64, 6, BLACK);
  fillRect(cx-32, cy+2,  64, 6, BLACK);
  fillRect(cx-32, cy+16, 44, 6, BLACK);
}

// ─── Layout helpers ────────────────────────────────────────────────────────

void drawHeader(const char* title, const char* rightInfo) {
  fillRect(0, 0, W, 28, BLACK);
  drawStrC(W/2, 10, title, 1, WHITE);
  if (rightInfo) {
    int rw = textW(rightInfo, 1);
    drawStr(W - 8 - rw, 10, rightInfo, 1, WHITE);
  }
}

void drawHints(const char* recLabel, const char* pwrLabel) {
  hline(0, 179, W, BLACK);
  fillRect(0, 180, W, 20, WHITE);
  drawStr(8, 186, recLabel, 1, BLACK);
  int rw = textW(pwrLabel, 1);
  drawStr(W - 8 - rw, 186, pwrLabel, 1, BLACK);
}

void drawBadge(int cx, int cy, const char* text, bool filled) {
  char up[32]; uppercaseCopy(up, text, sizeof(up));
  int tw = textW(up, 1);
  int bw = tw + 20, bh = 20;
  int bx = cx - bw/2, by = cy - bh/2;
  if (filled) {
    fillRoundRect(bx, by, bw, bh, 9, BLACK);
    drawStrC(cx, by + 6, up, 1, WHITE);
  } else {
    strokeRoundRect(bx, by, bw, bh, 9, 2, BLACK);
    drawStrC(cx, by + 6, up, 1, BLACK);
  }
}

void drawPageDots(int cur, int total) {
  if (total <= 1) return;
  int n = min(total, 7);
  int gap = 16;
  int startX = W/2 - ((n-1)*gap)/2;
  for (int i = 0; i < n; i++) {
    int x = startX + i*gap, y = 168;
    if (i == cur % n) fillCircle(x, y, 5, BLACK);
    else              strokeCircle(x, y, 4, 1, BLACK);
  }
}

void drawChevronRight(int x, int cy, uint8_t c) {
  thickLine(x,   cy-8, x+8, cy,   2, c);
  thickLine(x+8, cy,   x,   cy+8, 2, c);
}

void drawTinyHint(const char* left, const char* right) {
  (void)left; (void)right;
}

// Small battery meter for the top-right of the banner: icon + percentage.
// Drawn independently of the centered kicker label so they don't collide.
void drawBatteryBadge() {
  int pct = readBatteryPercent();

  // Percentage only, right-aligned in the top-right corner (icon removed).
  char buf[8];
  if (pct >= 0) snprintf(buf, sizeof(buf), "%d%%", constrain(pct, 0, 100));
  else          snprintf(buf, sizeof(buf), "--");
  int tw = textW(buf, 1);
  drawStr(W - 6 - tw, 7, buf, 1, BLACK);
}

void drawKicker(const char* txt, int y) {
  char up[40]; uppercaseCopy(up, txt, sizeof(up));
  drawStrC(W/2, y, up, 1, BLACK);
  drawBatteryBadge();
}

void drawSoftFrame() {
  strokeRoundRect(12, 12, W-24, H-24, 10, 1, BLACK);
}

void drawProductWordmark(int cx, int y, uint8_t color) {
  drawStr(cx - textW("golden", 2) / 2, y,      "golden", 2, color);
  drawStr(cx - textW("nugget", 2) / 2, y + 22, "nugget", 2, color);
}

void drawModernPill(int x, int y, int w, int h, const char* label, bool active) {
  if (active) {
    fillRoundRect(x, y, w, h, h/2, BLACK);
    drawStrInBox(x, y, w, h, label, 1, WHITE);
  } else {
    strokeRoundRect(x, y, w, h, h/2, 1, BLACK);
    drawStrInBox(x, y, w, h, label, 1, BLACK);
  }
}

void drawDotSelector(int cur, int total, int y) {
  int gap = 17, startX = W/2 - ((total-1)*gap)/2;
  for (int i=0; i<total; i++) {
    int x = startX + i*gap;
    if (i == cur) fillCircle(x, y, 4, BLACK);
    else          strokeCircle(x, y, 4, 1, BLACK);
  }
}

void drawCheckSmall(int cx, int cy, uint8_t color) {
  strokeCircle(cx, cy, 13, 1, color);
  thickLine(cx-6, cy, cx-1, cy+5, 2, color);
  thickLine(cx-1, cy+5, cx+8, cy-6, 2, color);
}

void drawMinimalDocIcon(int cx, int cy, uint8_t color) {
  strokeRoundRect(cx-13, cy-16, 26, 32, 3, 2, color);
  hline(cx-7, cy-5, 14, color);
  hline(cx-7, cy+4, 14, color);
  hline(cx-7, cy+13, 9, color);
}

void drawMinimalTagIcon(int cx, int cy, uint8_t color) {
  thickLine(cx-13, cy, cx-2, cy-13, 2, color);
  thickLine(cx-2, cy-13, cx+14, cy-13, 2, color);
  thickLine(cx+14, cy-13, cx+14, cy+2, 2, color);
  thickLine(cx+14, cy+2, cx+2, cy+15, 2, color);
  thickLine(cx+2, cy+15, cx-13, cy, 2, color);
  fillCircle(cx+4, cy-5, 3, color);
}

void drawMinimalCloudIcon(int cx, int cy, uint8_t color) {
  strokeCircle(cx-8, cy+2, 10, 2, color);
  strokeCircle(cx+4, cy-4, 13, 2, color);
  strokeCircle(cx+15, cy+4, 9, 2, color);
  fillRect(cx-22, cy+4, 47, 16, WHITE);
  hline(cx-21, cy+10, 44, color);
}

void drawMenuTile(int x, int y, int w, int h, const char* label, int icon, bool active) {
  if (active) fillRoundRect(x, y, w, h, 12, BLACK);
  else        strokeRoundRect(x, y, w, h, 12, 1, BLACK);
  uint8_t col = active ? WHITE : BLACK;
  int cx = x + w/2;
  fillCircle(cx, y + 17, 4, col);
  drawStrInBox(x + 4, y + 29, w - 8, 18, label, 1, col);
}

void drawNoteCard(int y, int idx, bool active) {
  const int x = 16, w = 168, h = 39;
  if (active) fillRoundRect(x, y, w, h, 8, BLACK);
  else        strokeRoundRect(x, y, w, h, 8, 1, BLACK);
  uint8_t col = active ? WHITE : BLACK;

  char n[8]; snprintf(n, sizeof(n), "#%03d", noteIndex[idx].num);
  String tagLabel = normalizeForDisplay(String(noteIndex[idx].tag));
  drawStr(x + 10, y + 5, n, 1, col);
  drawStrFit(x + 66, y + 5, 88, tagLabel.c_str(), 1, col);
  String ticker = noteTickerText(idx);
  drawTickerText(x + 10, y + 22, 145, ticker, active, col);
}

void drawListMenuCard(int y, const char* title, const char* meta, bool active) {
  const int x = 16, w = 168, h = 32;
  if (active) fillRoundRect(x, y, w, h, 8, BLACK);
  else        strokeRoundRect(x, y, w, h, 8, 1, BLACK);
  uint8_t col = active ? WHITE : BLACK;
  drawStrFit(x + 10, y + 8, meta ? 92 : 140, title, 1, col);
  if (meta && strlen(meta) > 0) {
    int mw = min(textW(meta, 1), 56);
    drawStrFit(x + w - 10 - mw, y + 8, 56, meta, 1, col);
  }
}

// ─── Screens ──────────────────────────────────────────────────────────────

void showIdle() {
  clearWhite();
  drawBatteryBadge();                       // top-right meter, same as other screens
  drawProductWordmark(100, 58, BLACK);
  fillCircle(100, 123, 5, BLACK);
  drawStrC(100, 144, "ready", 1, BLACK);
  refresh();
}

void showBatteryLow(int pct) {
  fillRect(0, 0, W, H, BLACK);
  fillRect(95, 48, 10, 50, WHITE);
  fillRect(95, 108, 10, 10, WHITE);
  char buf[8]; snprintf(buf, sizeof(buf), "%d%%", pct);
  drawStrC(100, 132, buf,       2, WHITE);
  drawStrC(100, 160, "battery", 1, WHITE);
  drawStrC(100, 176, "low",     1, WHITE);
  refresh();
}

void showRecording() {
  fillRect(0, 0, W, H, BLACK);
  fillCircle(W/2, H/2, 27, WHITE);
  refresh();
}

// Long-recording countdown: a depleting 180-segment ring (one tick per
// 10-second step over the 30-min cap) with MM:SS remaining in the center.
// Refreshed every ~10 s from the main loop. Pass fullClear=true periodically
// so the panel runs a full refresh to wipe partial-refresh ghosting.
void showLongRecording(uint32_t remainingMs, uint32_t totalMs, bool fullClear) {
  if (fullClear) {
    // Full-panel refresh cycle to clear accumulated ghosting, then resume
    // partial mode — same sequence used at boot.
    display->EPD_Init();
    display->EPD_Clear();
    display->EPD_DisplayPartBaseImage();
    display->EPD_Init_Partial();
  }

  fillRect(0, 0, W, H, WHITE);

  const int cx = W / 2, cy = H / 2;         // true center
  const int rOuter = 90, rInner = 74;
  const int SEGMENTS = 180;                 // one per 10-second step (30 min)

  // Filled segments = fraction of time remaining, rounded up so the ring only
  // empties completely at exactly 00:00.
  int filled = 0;
  if (totalMs > 0) {
    filled = (int)(((uint64_t)remainingMs * SEGMENTS + (totalMs - 1)) / totalMs);
    if (filled > SEGMENTS) filled = SEGMENTS;
    if (filled < 0) filled = 0;
  }

  // Draw the depleting ring as short radial spokes, starting at 12 o'clock
  // and going clockwise. Filled = remaining time; the rest is left blank.
  // Thicker spokes (4px) read better on e-ink than the old 3px.
  for (int i = 0; i < filled; i++) {
    float a = -M_PI / 2.0f + (2.0f * M_PI) * (float)i / (float)SEGMENTS;
    float ca = cosf(a), sa = sinf(a);
    int x0 = cx + (int)(ca * rInner), y0 = cy + (int)(sa * rInner);
    int x1 = cx + (int)(ca * rOuter), y1 = cy + (int)(sa * rOuter);
    thickLine(x0, y0, x1, y1, 4, BLACK);
  }

  // Bold guide circles frame the time: outer edge plus an inner ring. The
  // FreeSansBold18pt digits are small enough to sit clear of the inner ring.
  strokeCircle(cx, cy, rOuter + 1, 2, BLACK);
  strokeCircle(cx, cy, rInner - 1, 2, BLACK);

  // Three stacked bands, none overlapping (matches the agreed layout):
  //   REC kicker  above the digits
  //   MM:SS       centered, same FreeSansBold family as REC (scale 3 = 18pt)
  //   hint        below the digits, still inside the ring
  drawStrC(cx, cy - 40, "REC", 2, BLACK);

  uint32_t remSec = (remainingMs + 999) / 1000;   // round up so it shows 30:00 at start
  uint32_t mm = remSec / 60, ss = remSec % 60;
  char buf[8];
  snprintf(buf, sizeof(buf), "%lu:%02lu", (unsigned long)mm, (unsigned long)ss);
  // drawStr's y is the TOP of the text; FreeSansBold18pt digits are ~26px tall,
  // so offset by ~half that to center the row vertically on cy.
  drawStrC(cx, cy - 13, buf, 3, BLACK);

  // Hint on two lines so it stays clear of the inner ring — a single line was
  // wide enough that the ring clipped its ends on the device.
  drawStrC(cx, cy + 26, "double-press", 1, BLACK);
  drawStrC(cx, cy + 40, "to stop", 1, BLACK);

  refresh();
}

void showSaved(int num) {
  clearWhite();
  drawCheckSmall(100, 46, BLACK);
  drawStrC(100, 76, "saved", 1, BLACK);
  char b[8]; snprintf(b, sizeof(b), "#%03d", num);
  drawStrC(100, 105, b, 2, BLACK);
  refresh();
}

void showTagSelect(int cursor) {
  clearWhite();
  if (tagCount <= 0) {
    drawKicker("no tags", 34);
    drawStrC(100, 100, "open portal", 1, BLACK);
    refresh();
    return;
  }
  drawKicker("choose tag", 17);
  const int x = 36, w = 128, h = 21, gap = 7;
  int y0 = 40;
  cursor = constrain(cursor, 0, max(tagCount - 1, 0));
  for (int i=0; i<tagCount; i++) {
    int y = y0 + i*(h+gap);
    drawModernPill(x, y, w, h, tags[i], i == cursor);
  }
  refresh();
}

void showMenu(int cursor) {
  clearWhite();
  drawStr(16, 14, "menu", 1, BLACK);
  hline(16, 32, W-32, BLACK);
  const int y0 = 42, step = 36;
  for (int row = 0; row < MENU_COUNT; row++) {
    bool active = row == cursor;
    int y = y0 + row * step;
    if (active) fillRoundRect(16, y, 168, 31, 8, BLACK);
    else        strokeRoundRect(16, y, 168, 31, 8, 1, BLACK);
    uint8_t col = active ? WHITE : BLACK;
    drawStrInBox(16, y, 168, 31, MENU_ITEMS[row], 1, col);
  }
  refresh();
}

void showTagBrowser(int cursor) {
  clearWhite();
  if (tagCount <= 0) {
    drawKicker("tags", 16);
    drawStrC(100, 100, "no tags", 1, BLACK);
    refresh();
    return;
  }
  drawKicker("tags", 16);
  fillRoundRect(28, 56, 144, 54, 17, BLACK);
  cursor = constrain(cursor, 0, max(tagCount - 1, 0));
  drawStrInBox(28, 56, 144, 54, tags[cursor], 2, WHITE);
  int cnt = 0;
  for (int i=0; i<(int)noteIndex.size(); i++)
    if (strcmp(noteIndex[i].tag, tags[cursor])==0) cnt++;
  char cb[20]; snprintf(cb, sizeof(cb), "%d notes", cnt);
  drawStrC(100, 130, cb, 1, BLACK);
  refresh();
}

void showNoteList(int cursor) {
  if (tickerCursor != cursor) {
    tickerCursor = cursor;
    tickerOffset = 0;
    tickerLastMs = millis();
  }
  clearWhite();
  int count = filteredCount();
  char cb[16]; snprintf(cb, sizeof(cb), "%d notes", count);
  drawStr(16, 14, "notes", 1, BLACK);
  int cw = textW(cb, 1);
  drawStr(W-16-cw, 14, cb, 1, BLACK);
  if (count <= 0) {
    drawMinimalDocIcon(100, 76, BLACK);
    drawStrC(100, 116, "no notes yet", 1, BLACK);
    refresh();
    return;
  }
  const int pageSize = 3;
  int pageStart = (cursor / pageSize) * pageSize;
  int activeRow = cursor - pageStart;
  const int y0 = 43, step = 47;
  int shown = min(pageSize, count - pageStart);
  for (int row=0; row<shown; row++) {
    int vis = pageStart + row;
    int idx = noteAtFilteredIndex(vis);
    if (idx >= 0) drawNoteCard(y0 + row*step, idx, row == activeRow);
  }
  refresh();
}

void showNoteDetail(int cursor) {
  clearWhite();
  int idx = noteAtFilteredIndex(cursor);
  if (idx < 0) {
    drawStrC(100, 96, "not found", 1, BLACK);
    refresh();
    return;
  }
  char n[8]; snprintf(n, sizeof(n), "#%03d", noteIndex[idx].num);
  drawStr(16, 14, n, 1, BLACK);
  String tagLabel = normalizeForDisplay(String(noteIndex[idx].tag));
  int tw = textW(tagLabel.c_str(), 1);
  drawStrFit(W-16-min(tw, 82), 14, 82, tagLabel.c_str(), 1, BLACK);
  hline(16, 32, W-32, BLACK);

  if (noteIndex[idx].hasText) {
    char txtPath[64];
    snprintf(txtPath, sizeof(txtPath), "%s/note_%03d.txt", NOTES_DIR, noteIndex[idx].num);
    File f = SD_MMC.open(txtPath);
    char text[2048] = {0};
    if (f) { f.read((uint8_t*)text, 2047); f.close(); }
    String bodyText = normalizeForDisplay(String(text));
    const int linesPerPage = 7;
    int skip = detailScrollPage * linesPerPage;
    detailTotalLines = drawWrappedText(18, 48, 164, 18, linesPerPage, bodyText, BLACK, skip);
    int totalPages = (detailTotalLines + linesPerPage - 1) / linesPerPage;
    if (totalPages > 1) {
      char pageLabel[12];
      snprintf(pageLabel, sizeof(pageLabel), "%d/%d", detailScrollPage + 1, totalPages);
      int lw = textW(pageLabel, 1);
      drawStr(W - 8 - lw, 186, pageLabel, 1, BLACK);
      hline(0, 179, W, BLACK);
    }
  } else {
    iconThinking(100, 82);
    drawStrC(100, 122, "not synced", 1, BLACK);
  }
  refresh();
}

void showDeleteConfirm(int noteNum) {
  clearWhite();
  fillRect(0, 0, W, 28, BLACK);
  drawStrC(W/2, 10, "DELETE", 1, WHITE);
  char label[16]; snprintf(label, sizeof(label), "#%03d", noteNum);
  drawStrC(W/2, 52, label, 2, BLACK);
  drawStrC(W/2, 88, "Delete this note?", 1, BLACK);
  drawStrC(W/2, 108, "WAV + TXT + meta", 1, BLACK);
  hline(0, 179, W, BLACK);
  fillRect(0, 180, W, 20, WHITE);
  drawStr(8, 186, "confirm", 1, BLACK);
  int rw = textW("cancel", 1);
  drawStr(W - 8 - rw, 186, "cancel", 1, BLACK);
  refresh();
}

// Two stacked progress bars:
//   top    = current note's chunk progress (chunkDone / chunkTotal)
//   bottom = overall notes progress (done / total)
// For a short (single-request) note, pass chunkTotal <= 1 and the top bar just
// shows a full/working state.
void showTranscribing(int done, int total, int chunkDone, int chunkTotal, int noteNum) {
  clearWhite();
  drawKicker("syncing", 16);

  const int barW = 144, barH = 10, barX = 28;

  // --- Top bar: current note (chunks / short-note %) ---
  // Label above the bar names the note being worked on; caption below shows
  // progress (percentage, or "chunk N/M" for long recordings).
  int topLabelY = 46;
  int topBarY   = 64;
  int topCapY   = 80;
  if (noteNum >= 0) {
    char lbl[16]; snprintf(lbl, sizeof(lbl), "Note %d", noteNum);
    drawStrC(100, topLabelY, lbl, 1, BLACK);
  } else {
    drawStrC(100, topLabelY, "this note", 1, BLACK);
  }
  strokeRoundRect(barX, topBarY, barW, barH, 5, 1, BLACK);
  if (chunkTotal > 1) {
    int fill = (chunkDone * (barW - 4)) / chunkTotal;
    if (fill > 0) fillRoundRect(barX + 2, topBarY + 2, fill, barH - 4, 3, BLACK);
    int pctThis = (chunkDone * 100) / chunkTotal;
    char c[24];
    if (chunkTotal > 2) snprintf(c, sizeof(c), "chunk %d / %d", chunkDone, chunkTotal);
    else                snprintf(c, sizeof(c), "%d%%", pctThis);
    drawStrC(100, topCapY, c, 1, BLACK);
  } else {
    fillRoundRect(barX + 2, topBarY + 2, barW - 4, barH - 4, 3, BLACK);
    drawStrC(100, topCapY, "working", 1, BLACK);
  }

  // --- Bottom bar: overall notes ---
  int botLabelY = 110;
  int botBarY   = 128;
  int botCapY   = 144;
  drawStrC(100, botLabelY, "all notes", 1, BLACK);
  strokeRoundRect(barX, botBarY, barW, barH, 5, 1, BLACK);
  if (total > 0) {
    int fill = (done * (barW - 4)) / max(total, 1);
    if (fill > 0) fillRoundRect(barX + 2, botBarY + 2, fill, barH - 4, 3, BLACK);
    char b[20]; snprintf(b, sizeof(b), "%d / %d", done, total);
    drawStrC(100, botCapY, b, 1, BLACK);
  } else {
    drawStrC(100, botCapY, "please wait", 1, BLACK);
  }

  drawStrC(100, 178, "tap to stop", 1, BLACK);
  refresh();
}

// Small WiFi glyph in the upper-LEFT corner, drawn over whatever is already on
// screen (partial refresh of just the corner). Used as a quiet "connecting /
// using WiFi" indicator instead of the old full-screen progress bar.
void drawWifiCorner() {
  const int cx = 16, baseY = 20;   // arc origin near top-left
  // Three concentric quarter-ish arcs + dot, scaled small.
  strokeCircle(cx, baseY, 14, 2, BLACK);
  strokeCircle(cx, baseY, 9,  2, BLACK);
  strokeCircle(cx, baseY, 4,  2, BLACK);
  // Mask everything below the origin so we keep just the upper fan shape.
  fillRect(0, baseY + 1, 34, 22, WHITE);
  fillCircle(cx, baseY, 2, BLACK);
  refresh();
}

// Backward-compatible: connectWifiMulti calls this as its progress callback.
// We ignore the attempt counters and just ensure the corner icon is showing
// (drawn once on the first call), leaving the rest of the screen intact.
void showWifiConnecting(int attempt, int maxA) {
  if (attempt <= 0) drawWifiCorner();   // draw once at the start of a connect
}

void showOpenApConfirm(const String& ssid) {
  clearWhite();
  drawKicker("open network?", 18);
  iconWifi(100, 70);
  // SSID (truncated to fit), centered below the icon.
  String s = ssid;
  if (s.length() > 16) s = s.substring(0, 15) + "...";
  drawStrC(W/2, 108, s.c_str(), 1, BLACK);
  drawStrC(W/2, 128, "Not secure.", 1, BLACK);
  drawStrC(W/2, 146, "Connect anyway?", 1, BLACK);
  hline(0, 179, W, BLACK);
  drawStr(8, 186, "BOOT = yes", 1, BLACK);
  const char* no = "PWR = no";
  int nw = textW(no, 1);
  drawStr(W - 8 - nw, 186, no, 1, BLACK);
  refresh();
}

void showDone() {
  clearWhite();
  drawCheckSmall(100, 70, BLACK);
  drawStrC(100, 105, "all done", 1, BLACK);
  refresh();
}

// ─── Bot lookup screens ────────────────────────────────────────────────────
void showBotRecording(int secsLeft) {
  clearWhite();
  drawBatteryBadge();
  int iw = BOTIMG_LISTEN_W, ih = BOTIMG_LISTEN_H;
  drawBitmap1BPP((W - iw) / 2, 22, botimg_listen, iw, ih, BLACK);
  drawStrC(W/2, 168, "Ask Me Anything...", 1, BLACK);
  // Static hint of the window + how to finish early. We don't live-count here:
  // an e-ink refresh each second would drop audio mid-recording.
  char b[28];
  snprintf(b, sizeof(b), "%ds  -  tap to send", BOT_ASK_SECONDS);
  drawStrC(W/2, 188, b, 1, BLACK);
  refresh();
}

void showBotThinking() {
  clearWhite();
  drawBatteryBadge();
  int iw = BOTIMG_THINKING_W, ih = BOTIMG_THINKING_H;
  drawBitmap1BPP((W - iw) / 2, 26, botimg_thinking, iw, ih, BLACK);
  drawStrC(W/2, 178, "thinking...", 1, BLACK);
  refresh();
}

// Shown for a beat (3s) right before the answer — the "aha!" pose, full screen.
void showBotResponse() {
  clearWhite();
  int iw = BOTIMG_RESPONSE_W, ih = BOTIMG_RESPONSE_H;
  drawBitmap1BPP((W - iw) / 2, 25, botimg_response, iw, ih, BLACK);
  refresh();
}

static const int BOT_LINES_PER_PAGE = 7;

int botAnswerPages(const String& answer) {
  // Dry-run the wrap with skip past the end to get the total line count.
  // drawWrappedText returns total lines regardless of what it draws; we call it
  // with a huge skip so it draws nothing but still reports the count.
  int total = drawWrappedText(18, 48, 164, 18, 0, answer, BLACK, 1000000);
  int pages = (total + BOT_LINES_PER_PAGE - 1) / BOT_LINES_PER_PAGE;
  return pages < 1 ? 1 : pages;
}

void showBotAnswer(const String& answer, int page) {
  clearWhite();
  drawBatteryBadge();
  drawKicker("answer", 20);
  int skip = page * BOT_LINES_PER_PAGE;
  int total = drawWrappedText(18, 48, 164, 18, BOT_LINES_PER_PAGE, answer, BLACK, skip);
  int pages = (total + BOT_LINES_PER_PAGE - 1) / BOT_LINES_PER_PAGE;
  if (pages > 1) {
    char pageLabel[12];
    snprintf(pageLabel, sizeof(pageLabel), "%d/%d", page + 1, pages);
    int lw = textW(pageLabel, 1);
    drawStr(W - 8 - lw, 186, pageLabel, 1, BLACK);
    hline(0, 179, W, BLACK);
    drawStr(8, 186, "next", 1, BLACK);
  } else {
    hline(0, 179, W, BLACK);
    drawStr(8, 186, "exit", 1, BLACK);
  }
  refresh();
}

void showError(const char* msg) {
  clearWhite();
  iconError(100, 70);
  if (msg && strlen(msg) > 0) drawStrC(100, 118, msg, 1, BLACK);
  else drawStrC(100, 118, "error", 1, BLACK);
  refresh();
}

// Dedicated sync-error screen. The message is wrapped onto several short lines
// because the panel is only 200px wide. Each line prefers the clean bold font
// (scale 2, same as the REC label) but auto-shrinks to regular if a line would
// be too wide — so nothing clips and the whole message stays readable.
void showSyncError(const char* line1, const char* line2, const char* line3) {
  const int maxW = 192;
  clearWhite();
  iconError(100, 48);
  drawStrCFitScale(100, 92,  maxW, line1, 2, BLACK);
  if (line2 && strlen(line2) > 0) drawStrCFitScale(100, 120, maxW, line2, 2, BLACK);
  if (line3 && strlen(line3) > 0) drawStrCFitScale(100, 148, maxW, line3, 2, BLACK);
  refresh();
}

void showUltraSleepScreen() {
  clearWhite();
  #ifdef LOGO_WIDTH
    drawBitmap1BPP((W - LOGO_WIDTH) / 2, (H - LOGO_HEIGHT) / 2,
                   logo_bitmap, LOGO_WIDTH, LOGO_HEIGHT, BLACK);
  #else
    drawProductWordmark(100, 70, BLACK);
  #endif
  refresh();
}

void showPlaybackOverlay() {
  fillRoundRect(75, 145, 50, 34, 11, BLACK);
  fillTriangle(95, 154, 95, 170, 110, 162, WHITE);
  refresh();
}

void showTransferConnecting() {
  clearWhite();
  drawKicker("transfer", 18);
  iconWifi(100, 82);
  drawStrC(100, 138, "connecting", 1, BLACK);
  refresh();
}

void showTransferMode(const char* ip) {
  clearWhite();
  drawKicker("transfer", 16);
  fillRoundRect(26, 48, 148, 58, 16, BLACK);
  drawStrInBox(26, 48, 148, 24, "pala portal", 1, WHITE);
  drawStrInBox(26, 74, 148, 24, "active", 1, WHITE);
  drawStrC(100, 124, "open browser", 1, BLACK);
  drawStrC(100, 146, ip, 1, BLACK);
  drawStrC(100, 169, "double rec to exit", 1, BLACK);
  refresh();
}

void showSettings(int cursor) {
  clearWhite();
  drawStr(16, 14, "settings", 1, BLACK);
  hline(16, 32, W-32, BLACK);
  const int y0 = 40, step = 40;
  for (int row = 0; row < SETTINGS_COUNT; row++) {
    bool active = row == cursor;
    int y = y0 + row * step;
    if (active) fillRoundRect(16, y, 168, 34, 8, BLACK);
    else        strokeRoundRect(16, y, 168, 34, 8, 1, BLACK);
    uint8_t col = active ? WHITE : BLACK;
    if (row == 0) {
      drawStr(28, y + 8, "sounds", 1, col);
      drawStr(W - 70, y + 8, palaSoundIsEnabled() ? "on" : "off", 1, col);
    } else if (row == 1) {
      drawStr(28, y + 8, "read aloud", 1, col);
      drawStr(W - 70, y + 8, ttsOn ? "on" : "off", 1, col);
    } else if (row == 2) {
      drawStr(28, y + 8, "transfer", 1, col);
    } else {
      drawStr(28, y + 8, "device", 1, col);
    }
  }
  refresh();
}

void showDeviceInfo() {
  clearWhite();
  drawStr(16, 14, "device", 1, BLACK);
  hline(16, 32, W-32, BLACK);
  drawStr(18, 50, "firmware", 1, BLACK);
  drawStrFit(18, 68, 160, FIRMWARE_VERSION, 1, BLACK);
  drawStr(18, 94, "board", 1, BLACK);
  drawStrFit(18, 112, 160, "ESP32-S3 ePaper 1.54", 1, BLACK);
  char b[24]; snprintf(b, sizeof(b), "%d notes", (int)noteIndex.size());
  drawStr(18, 138, b, 1, BLACK);
  drawStr(18, 160, palaSoundIsEnabled() ? "sounds on" : "sounds off", 1, BLACK);
  drawStr(18, 178, rtcUtcIso().length() ? "rtc set" : "rtc not set", 1, BLACK);
  refresh();
}
