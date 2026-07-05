#include "Arduino.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../types.h"
#include "long_record.h"
#include "notes.h"
#include "battery.h"
#include "SD_MMC.h"
#include "esp_heap_caps.h"

extern "C" {
#include "../../src/audio/audio_bsp.h"
}

// ─── Module state ──────────────────────────────────────────────────────────
static File      s_file;
static int16_t*  s_sbuf      = nullptr;   // interleaved stereo from codec
static int16_t*  s_mbuf      = nullptr;   // downmixed mono written to SD
static uint32_t  s_totalMono = 0;         // bytes of mono PCM written
static uint32_t  s_startMs   = 0;
static bool      s_active    = false;
static bool      s_hitCap    = false;
static int       s_num       = -1;

// Max mono PCM bytes for the cap: SAMPLE_RATE * 2 bytes/sample * seconds.
static const uint32_t LONGREC_MAX_BYTES =
    (uint32_t)SAMPLE_RATE * 2u * (uint32_t)(LONGREC_MAX_MIN * 60);

static void writeWavHeader(File& f, uint32_t dataBytes) {
  uint32_t dB = dataBytes, fS = dB + 36, bR = SAMPLE_RATE * 2;
  uint16_t bA = 2, aF = 1, ch = 1, bps = 16;
  uint32_t fL = 16, sr = SAMPLE_RATE;
  f.seek(0);
  f.write((uint8_t*)"RIFF", 4); f.write((uint8_t*)&fS, 4);
  f.write((uint8_t*)"WAVE", 4); f.write((uint8_t*)"fmt ", 4);
  f.write((uint8_t*)&fL, 4);    f.write((uint8_t*)&aF, 2);
  f.write((uint8_t*)&ch, 2);    f.write((uint8_t*)&sr, 4);
  f.write((uint8_t*)&bR, 4);    f.write((uint8_t*)&bA, 2);
  f.write((uint8_t*)&bps, 2);
  f.write((uint8_t*)"data", 4); f.write((uint8_t*)&dB, 4);
}

bool longRecStart() {
  if (s_active) return true;

  s_num = nextNoteNumber();
  char path[64];
  snprintf(path, sizeof(path), "%s/note_%03d.wav", NOTES_DIR, s_num);
  Serial.printf("[LongRec] start %s (cap %d min)\n", path, LONGREC_MAX_MIN);

  s_file = SD_MMC.open(path, FILE_WRITE);
  if (!s_file) { Serial.println("[LongRec] open failed"); return false; }

  uint8_t header[44] = {};
  s_file.write(header, 44);   // placeholder, patched on stop

  s_sbuf = (int16_t*)heap_caps_malloc(REC_BUF,     MALLOC_CAP_8BIT);
  s_mbuf = (int16_t*)heap_caps_malloc(REC_BUF / 2, MALLOC_CAP_8BIT);
  if (!s_sbuf || !s_mbuf) {
    if (s_sbuf) heap_caps_free(s_sbuf);
    if (s_mbuf) heap_caps_free(s_mbuf);
    s_sbuf = s_mbuf = nullptr;
    s_file.close();
    return false;
  }

  s_totalMono = 0;
  s_hitCap    = false;
  s_startMs   = millis();
  s_active    = true;
  return true;
}

bool longRecPump() {
  if (!s_active) return false;

  // Read a SMALL chunk per tick. esp_codec_dev_read() blocks until the chunk
  // is filled from the I2S DMA, so the chunk size sets how long each pump
  // blocks: REC_BUF (8KB stereo) would block ~128 ms and make the PWR
  // double-press feel sluggish. We read REC_BUF/8 (~16 ms) so loop() cycles
  // ~8x faster and the stop gesture is responsive. The DMA ring buffers the
  // audio between reads, so nothing is lost.
  const uint32_t CHUNK = REC_BUF / 8;
  audio_playback_read((void*)s_sbuf, CHUNK);
  int mono = CHUNK / 4;                          // stereo16 -> mono16 sample count
  for (int i = 0; i < mono; i++) s_mbuf[i] = s_sbuf[i * 2];
  size_t written = s_file.write((uint8_t*)s_mbuf, mono * 2);
  s_totalMono += written;

  // Auto-stop at the time/size cap.
  if (s_totalMono >= LONGREC_MAX_BYTES) {
    Serial.println("[LongRec] cap reached");
    s_hitCap = true;
    longRecStop();
    return false;
  }

  // Safeguard: auto-stop if the battery drops to the low threshold mid-record
  // so a forgotten recording can't drain the cell to nothing. Checked sparsely
  // (it's an ADC read) — once every ~2048 chunks (~30 s of audio).
  static uint32_t pumpCount = 0;
  if ((++pumpCount & 0x7FF) == 0) {
    int pct = readBatteryPercent();
    if (pct >= 0 && pct <= BAT_LOW_THRESHOLD) {
      Serial.printf("[LongRec] low battery %d%%, auto-saving\n", pct);
      longRecStop();
      return false;
    }
  }

  return true;
}

void longRecStop() {
  if (!s_active) return;
  s_active = false;

  writeWavHeader(s_file, s_totalMono);
  s_file.close();

  if (s_sbuf) { heap_caps_free(s_sbuf); s_sbuf = nullptr; }
  if (s_mbuf) { heap_caps_free(s_mbuf); s_mbuf = nullptr; }

  lastRecNum = s_num;
  Serial.printf("[LongRec] saved note_%03d.wav (%lu mono bytes, %lus)\n",
                s_num, (unsigned long)s_totalMono,
                (unsigned long)(s_totalMono / (SAMPLE_RATE * 2)));
}

bool     longRecIsActive()   { return s_active; }
bool     longRecHitCap()     { return s_hitCap; }
uint32_t longRecElapsedMs()  { return s_active ? (millis() - s_startMs) : 0; }

uint32_t longRecRemainingMs() {
  uint32_t cap = (uint32_t)LONGREC_MAX_MIN * 60u * 1000u;
  uint32_t el  = longRecElapsedMs();
  return (el >= cap) ? 0 : (cap - el);
}
