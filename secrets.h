#ifndef SECRETS_H
#define SECRETS_H

// Primary network (also used as the compiled fallback).
#define WIFI_SSID   "...."
#define WIFI_PASS   "...."
#define OPENAI_KEY  "...."

// ElevenLabs text-to-speech (reads bot answers aloud). Get the key from your
// ElevenLabs Profile page, and a voice ID from the Voices page (or the API).
#define ELEVENLABS_KEY      "...."
#define ELEVENLABS_VOICE_ID "nPczCjzI2devNBz1zQrb"   // default "Rachel" voice; replace as desired

// Additional known networks. The device scans, then connects to the strongest
// one it can see from this list (plus any in /wifi.txt on the SD card). Add as
// many rows as you like. Leave extras as {"",""} or delete them.
struct WifiCred { const char* ssid; const char* pass; };
static const WifiCred WIFI_NETWORKS[] = {
  { WIFI_SSID, WIFI_PASS },     // primary (from the defines above)
  { "",        ""        },     // e.g. { "Phone-Hotspot", "hotspotpass" }
  { "",        ""        },     // e.g. { "Work-WiFi",     "workpass"    }
};
static const int WIFI_NETWORKS_COUNT = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);

#endif
