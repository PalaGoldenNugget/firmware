#ifndef SECRETS_H
#define SECRETS_H

// Primary network (also used as the compiled fallback).
#define WIFI_SSID   "...."
#define WIFI_PASS   "...."
// You need to fund your OPENAI / ChatGPT account (https://platform.openai.com/settings/organization/billing/overview) in order to get the transcription and query option to work then enter the key below
#define OPENAI_KEY  "...."

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
