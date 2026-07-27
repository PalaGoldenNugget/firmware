#ifndef CONFIG_H
#define CONFIG_H

#define EPD_SPI_NUM        SPI2_HOST
#define ESP32_I2C_DEV_NUM  I2C_NUM_0

#define EPD_WIDTH  200
#define EPD_HEIGHT 200
#define LVGL_SPIRAM_BUFF_LEN (EPD_WIDTH * EPD_HEIGHT * 2)

/* EPD SPI pins */
#define EPD_DC_PIN    GPIO_NUM_10
#define EPD_CS_PIN    GPIO_NUM_11
#define EPD_SCK_PIN   GPIO_NUM_12
#define EPD_MOSI_PIN  GPIO_NUM_13
#define EPD_RST_PIN   GPIO_NUM_9
#define EPD_BUSY_PIN  GPIO_NUM_8


/* Power control pins */
#define EPD_PWR_PIN     GPIO_NUM_6
#define Audio_PWR_PIN   GPIO_NUM_42
#define VBAT_PWR_PIN    GPIO_NUM_17
#define BAT_ADC_PIN     4   // ADC1_CHANNEL_3 on ESP32-S3

#define BOOT_BUTTON_PIN GPIO_NUM_0
#define PWR_BUTTON_PIN  GPIO_NUM_18

/* Deep-sleep wake-up pin */
#define ext_wakeup_pin_1 GPIO_NUM_0

/* I2C bus */
#define ESP32_I2C_SDA_PIN GPIO_NUM_47
#define ESP32_I2C_SCL_PIN GPIO_NUM_48

/* LVGL tick timing */
#define EXAMPLE_LVGL_TICK_PERIOD_MS    5
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 100

/* I2C peripheral addresses */
#define I2C_RTC_DEV_Address        0x51
#define I2C_SHTC3_DEV_Address      0x70

/* Button GPIO aliases */
#define BTN_REC      0
#define BTN_PWR      18
#define PWR_HOLD_PIN 17

/* SD-MMC pins */
#define SD_CLK  39
#define SD_CMD  41
#define SD_D0   40

/* Audio */
#define SAMPLE_RATE  16000
#define REC_BUF      (8 * 1024)
#define MIN_NOTE_SECONDS 1     // notes shorter than this (in seconds) are discarded

/* Storage paths */
#define NOTES_DIR  "/notes"
#define INDEX_FILE "/notes/index.csv"
#define TAG_FILE   "/notes/tags.txt"
#define MAX_TAGS   20

/* UI timing */
#define REC_HOLD_MS         350
#define BTN_LONG_MS         600
#define DOUBLE_MS           200
#define ULTRA_SLEEP_MS      120000UL
#define TICKER_INTERVAL_MS  950

/* Long (meeting) recording */
#define LONGREC_MAX_MIN        30      // hard cap; auto-stops and saves at 00:00
#define LONGREC_UI_INTERVAL_MS 10000   // countdown screen refresh cadence
#define LONGREC_FULL_EVERY     30      // full refresh every Nth update (~5 min) to clear ghosting
#define PWR_DOUBLE_MS          350     // window to detect a PWR double-press

/* Bot lookup (BOOT double-press: ask a spoken question, get an answer) */
#define BOT_MODEL          "gpt-4o-mini"
#define BOT_ASK_SECONDS    10          // fixed recording window after double-press
#define BOT_MAX_Q_SECONDS  20          // hard safety cap
#define BOT_SYSTEM_PROMPT  "You are a helpful assistant answering through a tiny 200x200 e-ink screen. Answer in plain text, no markdown, as briefly as possible — ideally one or two short sentences. Avoid lists unless essential."
#define BOT_TAG            "Bot"       // reserved tag for saved Q&A notes
#define BOT_REC_DOUBLE_MS  300         // window to detect a BOOT double-press

/* Text-to-speech (ElevenLabs reads bot answers aloud) */
#define TTS_ENABLED        1                    // set 0 to disable read-aloud
#define TTS_MODEL          "eleven_flash_v2_5"  // fast, low-cost; good for short answers
#define TTS_OUTPUT_FORMAT  "pcm_16000"          // matches the device's native 16kHz playback
#define TTS_TMP_PCM        "/tts.pcm"           // temp raw-PCM file on SD
#define TTS_TIMEOUT_MS     60000UL              // per request
#define TTS_MAX_CHARS      800                  // cap answer length sent to TTS (cost/latency)
#define TTS_VOLUME         85                   // playback volume for spoken answers

/* Whisper transcription / chunking */
#define WHISPER_TIMEOUT_MS   240000UL   // 4 min per request (large uploads are slow on ESP32)
#define WHISPER_MAX_BYTES    24000000UL // API hard limit is 25MB; stay safely under
#define CHUNK_SECONDS        600        // ~10 min per chunk (~19MB at 16kHz mono 16-bit)
#define CHUNK_OVERLAP_SEC    2          // overlap so no word is lost at a seam

/* Battery warning */
#define BAT_CHECK_INTERVAL_MS  30000
#define BAT_LOW_THRESHOLD      15
#define BAT_RECOVER_THRESHOLD  20

/* Time & firmware */
#define LOCAL_TIME_OFFSET_MIN  120   // UTC+2 (Germany summer). Set to your offset.
#define FIRMWARE_VERSION       "v1.3"
#define FW_VERSION             "v1.3"

#endif // CONFIG_H