#include "Arduino.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../types.h"
#include "network.h"
#include "notes.h"
#include "rtc.h"
#include "ui.h"
#include "WiFi.h"
#include "WiFiClientSecure.h"
#include <WebServer.h>
#include "SD_MMC.h"
#include "esp_heap_caps.h"
#include "../../secrets.h"

static String parseWhisperText(const String& resp) {
  int s = resp.indexOf("\"text\":\"");
  if (s < 0) return "";
  s += 8;
  int e = s;
  while (e < (int)resp.length()) {
    if (resp[e] == '\\' && e + 1 < (int)resp.length()) { e += 2; continue; }
    if (resp[e] == '"') break;
    e++;
  }
  if (e >= (int)resp.length()) return "";
  String text = "";
  for (int i = s; i < e; i++) {
    if (resp[i] == '\\' && i + 1 < e) {
      char nx = resp[++i];
      if      (nx == '"')  text += '"';
      else if (nx == '\\') text += '\\';
      else if (nx == 'n')  text += ' ';
      else                 text += nx;
    } else {
      text += resp[i];
    }
  }
  return text;
}

// Reason the most recent transcription pass failed (read by the UI).
static SyncError g_syncError = SYNC_OK;

static bool transcribeOnce(const String& wavPath, int noteNum, String* outText = nullptr) {
  File f = SD_MMC.open(wavPath.c_str());
  if (!f) return false;
  size_t fileSize = f.size();

  String bnd = "----PalaBoundary";
  String pre = "--" + bnd + "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-1\r\n"
               "--" + bnd + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"note.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
  String post = "\r\n--" + bnd + "--\r\n";
  size_t totalLen = pre.length() + fileSize + post.length();

  WiFiClientSecure client;
  client.setInsecure();  // TODO: pin api.openai.com cert for production use
  client.setTimeout(90);

  if (!client.connect("api.openai.com", 443)) { g_syncError = SYNC_ERR_NETWORK; f.close(); return false; }

  client.printf("POST /v1/audio/transcriptions HTTP/1.1\r\n"
                "Host: api.openai.com\r\n"
                "Authorization: Bearer %s\r\n"
                "Content-Type: multipart/form-data; boundary=%s\r\n"
                "Content-Length: %u\r\n"
                "Connection: close\r\n\r\n",
                OPENAI_KEY, bnd.c_str(), (unsigned)totalLen);
  client.print(pre);

  uint8_t* chunk = (uint8_t*)heap_caps_malloc(4096, MALLOC_CAP_8BIT);
  if (!chunk) { f.close(); client.stop(); return false; }
  while (f.available()) {
    int n = f.read(chunk, 4096);
    if (n <= 0) break;
    client.write(chunk, n);
  }
  heap_caps_free(chunk);
  f.close();
  client.print(post);

  uint32_t deadline = millis() + 90000;
  while (!client.available() && millis() < deadline) delay(20);

  String resp = "";
  bool inBody = false;
  while (client.available() || (client.connected() && millis() < deadline)) {
    if (!client.available()) { delay(10); continue; }
    String line = client.readStringUntil('\n');
    if (!inBody) {
      if (line == "\r" || line == "") inBody = true;
      if (line.startsWith("HTTP/") && line.indexOf(" 200 ") < 0) {
        Serial.printf("[Whisper] %s\n", line.c_str());
        // Record a specific reason so the UI can explain the failure.
        if      (line.indexOf(" 429 ") >= 0) g_syncError = SYNC_ERR_NO_CREDIT;
        else if (line.indexOf(" 401 ") >= 0) g_syncError = SYNC_ERR_AUTH;
        else                                 g_syncError = SYNC_ERR_HTTP;
        client.stop(); return false;
      }
    } else {
      resp += line;
      if (resp.length() > 8192) break;
    }
  }
  client.stop();

  String text = parseWhisperText(resp);
  if (text.length() == 0) { Serial.println("[Whisper] empty response"); return false; }

  // Bot mode: return the text directly, don't touch files or the note index.
  if (outText) { *outText = text; return true; }

  String tp = wavPath; tp.replace(".wav", ".txt");
  File tf = SD_MMC.open(tp.c_str(), FILE_WRITE);
  if (tf) { tf.print(text); tf.close(); }

  updateIndexHasText(noteNum);
  return true;
}

// Transcribe a WAV and return the text directly (no file/index side effects).
bool transcribeToText(const String& wavPath, String& out) {
  for (int attempt = 0; attempt < 2; attempt++) {
    if (transcribeOnce(wavPath, -1, &out)) return true;
    if (g_syncError == SYNC_ERR_NO_CREDIT || g_syncError == SYNC_ERR_AUTH) break;
    if (attempt < 1) delay(2000);
  }
  return false;
}

bool transcribe(const String& wavPath, int noteNum) {
  for (int attempt = 0; attempt < 3; attempt++) {
    if (transcribeOnce(wavPath, noteNum)) return true;
    if (attempt < 2) { Serial.printf("[Whisper] retry %d/2\n", attempt + 1); delay(3000); }
  }
  return false;
}

SyncError lastSyncError() { return g_syncError; }

// Look up a password for an SSID: first /wifi.txt on SD (lines "ssid,pass"),
// then the compiled WIFI_NETWORKS list. Returns true if found.
static bool lookupWifiPass(const String& ssid, String& passOut) {
  // SD /wifi.txt
  File f = SD_MMC.open("/wifi.txt", FILE_READ);
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0 || line[0] == '#') continue;
      int comma = line.indexOf(',');
      if (comma < 0) continue;
      String s = line.substring(0, comma); s.trim();
      if (s == ssid) {
        passOut = line.substring(comma + 1); passOut.trim();
        f.close();
        return true;
      }
    }
    f.close();
  }
  // Compiled list
  for (int i = 0; i < WIFI_NETWORKS_COUNT; i++) {
    if (strlen(WIFI_NETWORKS[i].ssid) == 0) continue;
    if (ssid == WIFI_NETWORKS[i].ssid) { passOut = WIFI_NETWORKS[i].pass; return true; }
  }
  return false;
}

bool connectWifiMulti(void (*progress)(int tries, int maxTries),
                      bool (*confirmOpen)(const String& ssid)) {
  if (WiFi.status() == WL_CONNECTED) return true;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (progress) progress(0, 100);

  // Scan for networks in range. Results come back ordered by signal strength.
  int n = WiFi.scanNetworks();
  if (n <= 0) {
    Serial.println("[WiFi] no networks found in scan");
    return false;
  }

  // Pass 1: known networks, strongest-first.
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    String pass;
    if (!lookupWifiPass(ssid, pass)) continue;   // not a known network

    Serial.printf("[WiFi] trying %s (RSSI %d)\n", ssid.c_str(), WiFi.RSSI(i));
    WiFi.begin(ssid.c_str(), pass.c_str());

    const int MAX_TRIES = 20;
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < MAX_TRIES) {
      delay(500); tries++;
      if (progress) progress(tries, MAX_TRIES);
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[WiFi] connected to %s\n", ssid.c_str());
      WiFi.scanDelete();
      return true;
    }
    WiFi.disconnect();
  }

  // Pass 2 (last resort): the strongest OPEN network, only with user confirm.
  if (confirmOpen) {
    for (int i = 0; i < n; i++) {
      if (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) continue;
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;           // skip hidden/blank

      Serial.printf("[WiFi] open network available: %s\n", ssid.c_str());
      if (!confirmOpen(ssid)) {                    // user declined
        Serial.println("[WiFi] user declined open network");
        break;
      }
      WiFi.begin(ssid.c_str());                     // no password
      const int MAX_TRIES = 20;
      int tries = 0;
      while (WiFi.status() != WL_CONNECTED && tries < MAX_TRIES) {
        delay(500); tries++;
        if (progress) progress(tries, MAX_TRIES);
      }
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WiFi] connected (open) to %s\n", ssid.c_str());
        WiFi.scanDelete();
        return true;
      }
      WiFi.disconnect();
      break;   // only offer the single strongest open network
    }
  }

  WiFi.scanDelete();
  Serial.println("[WiFi] no network connected");
  return false;
}

// Escape a string for embedding in JSON (quotes, backslashes, control chars).
static String jsonEscape(const String& s) {
  String o; o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"':  o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n";  break;
      case '\r': o += "\\r";  break;
      case '\t': o += "\\t";  break;
      default:
        if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o += c;
    }
  }
  return o;
}

bool botAsk(const String& question, String& answerOut) {
  g_syncError = SYNC_OK;

  String body = String("{\"model\":\"") + BOT_MODEL + "\",\"messages\":["
    + "{\"role\":\"system\",\"content\":\"" + jsonEscape(BOT_SYSTEM_PROMPT) + "\"},"
    + "{\"role\":\"user\",\"content\":\""   + jsonEscape(question)          + "\"}"
    + "],\"max_tokens\":300,\"temperature\":0.4}";

  WiFiClientSecure client;
  client.setInsecure();   // TODO: pin api.openai.com cert for production use
  client.setTimeout(60);
  if (!client.connect("api.openai.com", 443)) { g_syncError = SYNC_ERR_NETWORK; return false; }

  client.print("POST /v1/chat/completions HTTP/1.1\r\n");
  client.print("Host: api.openai.com\r\n");
  client.printf("Authorization: Bearer %s\r\n", OPENAI_KEY);
  client.print("Content-Type: application/json\r\n");
  client.printf("Content-Length: %u\r\n", (unsigned)body.length());
  client.print("Connection: close\r\n\r\n");
  client.print(body);

  uint32_t deadline = millis() + 60000;
  while (!client.available() && millis() < deadline) delay(20);

  // Status line
  String status = client.readStringUntil('\n');
  if (status.indexOf(" 200 ") < 0) {
    Serial.printf("[Bot] %s\n", status.c_str());
    if      (status.indexOf(" 429 ") >= 0) g_syncError = SYNC_ERR_NO_CREDIT;
    else if (status.indexOf(" 401 ") >= 0) g_syncError = SYNC_ERR_AUTH;
    else                                   g_syncError = SYNC_ERR_HTTP;
    client.stop();
    return false;
  }
  // Skip headers
  while (client.connected() || client.available()) {
    String line = client.readStringUntil('\n');
    if (line == "\r" || line.length() <= 1) break;
  }
  // Body
  String resp;
  while ((client.connected() || client.available()) && millis() < deadline) {
    while (client.available()) resp += (char)client.read();
    delay(5);
  }
  client.stop();

  // Extract the assistant message content. The JSON is
  //   ...\"message\":{\"role\":\"assistant\",\"content\":\"...\"}...
  int ci = resp.indexOf("\"content\"");
  if (ci < 0) { g_syncError = SYNC_ERR_HTTP; return false; }
  int q1 = resp.indexOf('"', ci + 9);
  if (q1 < 0) { g_syncError = SYNC_ERR_HTTP; return false; }
  // Walk the string, honoring escapes, to the closing quote.
  String out;
  for (int i = q1 + 1; i < (int)resp.length(); i++) {
    char c = resp[i];
    if (c == '\\' && i + 1 < (int)resp.length()) {
      char n = resp[++i];
      switch (n) {
        case 'n': out += '\n'; break;
        case 'r': break;
        case 't': out += ' ';  break;
        case '"': out += '"';  break;
        case '\\': out += '\\'; break;
        case 'u': {
          if (i + 4 < (int)resp.length()) {
            String hex = resp.substring(i + 1, i + 5);
            long cp = strtol(hex.c_str(), nullptr, 16);
            if (cp < 0x80) out += (char)cp; else out += '?';  // panel is ASCII
            i += 4;
          }
          break;
        }
        default: out += n; break;
      }
    } else if (c == '"') {
      break;
    } else {
      out += c;
    }
  }
  out.trim();
  if (out.length() == 0) { g_syncError = SYNC_ERR_HTTP; return false; }
  answerOut = out;
  return true;
}

void transcribeAll() {
  g_syncError = SYNC_OK;                 // reset at the start of each pass
  int pending = 0;
  for (int i=0; i<(int)noteIndex.size(); i++) if(!noteIndex[i].hasText) pending++;
  int done = 0;
  for (int i=0; i<(int)noteIndex.size(); i++) {
    if (noteIndex[i].hasText) continue;
    showTranscribing(done, pending);
    char wp[64]; snprintf(wp, sizeof(wp), "%s/note_%03d.wav", NOTES_DIR, noteIndex[i].num);
    if (transcribe(String(wp), noteIndex[i].num)) {
      done++;
    } else if (g_syncError == SYNC_ERR_NO_CREDIT || g_syncError == SYNC_ERR_AUTH) {
      // No point hammering the API for every note when the account/key is the
      // problem — stop the pass so the UI can show the reason promptly.
      return;
    }
  }
}

// ─── Portal helpers ────────────────────────────────────────────────────────

String htmlEscape(const String& s) {
  String out = s;
  out.replace("&", "&amp;"); out.replace("<", "&lt;");
  out.replace(">", "&gt;"); out.replace("\"", "&quot;");
  return out;
}

String readSmallFile(const char* path, size_t maxLen) {
  File f = SD_MMC.open(path);
  if (!f) return "";
  String out;
  while (f.available() && out.length() < maxLen) out += (char)f.read();
  f.close();
  return out;
}

String urlDecodeSimple(String s) {
  s.replace("+", " ");
  String out = "";
  for (int i = 0; i < (int)s.length(); i++) {
    if (s[i] == '%' && i + 2 < (int)s.length()) {
      String hex = s.substring(i + 1, i + 3);
      out += (char)strtol(hex.c_str(), nullptr, 16);
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

String portalCss() {
  return String(
    "<style>"
    ":root{font-family:-apple-system,BlinkMacSystemFont,'Inter','Segoe UI',sans-serif;color:#111;background:#f3f0e9;}"
    "body{margin:0;padding:24px;background:#f3f0e9;}"
    ".wrap{max-width:780px;margin:0 auto;}"
    ".top{display:flex;align-items:flex-end;justify-content:space-between;gap:16px;margin-bottom:24px;}"
    "h1{font-size:44px;letter-spacing:-.06em;line-height:.9;margin:0;font-weight:800;}"
    ".sub{font-size:13px;text-transform:uppercase;letter-spacing:.12em;color:#6a665f;margin-top:10px;}"
    ".pill{display:inline-flex;border:1px solid #111;border-radius:999px;padding:8px 12px;font-size:13px;background:#fffaf1;}"
    ".grid{display:grid;grid-template-columns:1fr;gap:14px;}"
    ".card{background:#fffaf1;border:1.5px solid #111;border-radius:24px;padding:18px;box-shadow:4px 4px 0 #111;}"
    ".row{display:flex;justify-content:space-between;gap:16px;align-items:flex-start;}"
    ".num{font-size:13px;letter-spacing:.08em;text-transform:uppercase;color:#6a665f;margin-bottom:8px;}"
    ".date{font-size:13px;color:#6a665f;margin:-4px 0 12px;}"
    ".title{font-size:24px;line-height:1.05;letter-spacing:-.04em;font-weight:750;margin:0 0 12px;}"
    ".tag{border:1px solid #111;border-radius:999px;padding:5px 9px;font-size:12px;white-space:nowrap;background:#111;color:#fff;}"
    ".text{font-size:15px;line-height:1.45;color:#222;margin:0 0 14px;white-space:pre-wrap;}"
    ".actions{display:flex;flex-wrap:wrap;gap:8px;margin-top:14px;}"
    "a.btn{color:#111;text-decoration:none;border:1px solid #111;border-radius:999px;padding:8px 12px;background:#f3f0e9;font-size:13px;}"
    "a.btn.primary{background:#111;color:#fff;}"
    ".empty{border:1.5px dashed #111;border-radius:24px;padding:34px;text-align:center;color:#6a665f;}"
    "audio{width:100%;margin-top:8px;}"
    "@media(max-width:520px){body{padding:16px}h1{font-size:36px}.card{border-radius:20px}.title{font-size:21px}}"
    "</style>"
  );
}

// ─── Portal handlers ───────────────────────────────────────────────────────

void handlePortalRoot() {
  loadIndex();

  String filter = "All";
  if (transferServer.hasArg("tag")) filter = transferServer.arg("tag");

  String html = "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Pala Portal</title>" + portalCss() + "</head><body><div class='wrap'>";

  html += "<div class='top'><div><h1>pala<br>portal</h1>"
          "<div class='sub'>local note transfer · <a href=\"/tags\" style=\"color:inherit\">tags</a></div></div>"
          "<div class='pill'>" + String((int)noteIndex.size()) + " notes</div></div>";

  html += "<div class='actions' style='margin-bottom:18px'>";
  html += "<a class='btn " + String(filter == "All" ? "primary" : "") + "' href='/'>All</a>";
  for (int t = 0; t < tagCount; t++) {
    String tag = String(tags[t]);
    html += "<a class='btn " + String(filter == tag ? "primary" : "") + "' href='/?tag=" + tag + "'>" + htmlEscape(tag) + "</a>";
  }
  html += "</div>";

  html += "<div class='actions' style='margin-bottom:24px'>";
  html += "<a class='btn primary' href='/export.txt'>Download all TXT</a>";
  if (filter != "All")
    html += "<a class='btn' href='/export.txt?tag=" + filter + "'>Download " + htmlEscape(filter) + " TXT</a>";
  html += "</div>";

  int visibleCount = 0;
  for (int i = 0; i < (int)noteIndex.size(); i++)
    if (filter == "All" || filter == String(noteIndex[i].tag)) visibleCount++;

  if (visibleCount <= 0) {
    html += "<div class='empty'>No notes for this filter.</div>";
  } else {
    html += "<div class='grid'>";
    for (int v = 0; v < (int)noteIndex.size(); v++) {
      int i = (int)noteIndex.size() - 1 - v;
      if (!(filter == "All" || filter == String(noteIndex[i].tag))) continue;
      int num = noteIndex[i].num;

      char txtPath[64], wavPath[64];
      snprintf(txtPath, sizeof(txtPath), "%s/note_%03d.txt", NOTES_DIR, num);
      snprintf(wavPath, sizeof(wavPath), "%s/note_%03d.wav", NOTES_DIR, num);

      String transcript = readSmallFile(txtPath, 1200);
      if (transcript.length() == 0)
        transcript = noteIndex[i].hasText ? "(empty transcript)" : "Not transcribed yet.";

      String title = transcript; title.replace("\n", " "); title.trim();
      if (title.length() > 58) title = title.substring(0, 58) + "...";
      if (title.length() == 0 || title == "Not transcribed yet.")
        title = String("Voice note ") + String(num);

      html += "<div class='card'>";
      html += "<div class='row'><div><div class='num'>#" + String(num) + "</div>";
      html += "<h2 class='title'>" + htmlEscape(title) + "</h2>";
      String createdUtc = noteCreatedUtc(num);
      if (createdUtc.length() > 0)
        html += "<div class='date' data-utc='" + createdUtc + "'>" + createdUtc + "</div>";
      else
        html += "<div class='date'>time not set</div>";
      html += "</div>";
      html += "<div class='tag'>" + htmlEscape(String(noteIndex[i].tag)) + "</div></div>";
      html += "<p class='text'>" + htmlEscape(transcript) + "</p>";
      if (SD_MMC.exists(wavPath))
        html += "<audio controls src='/audio?num=" + String(num) + "'></audio>";
      html += "<div class='actions'>";
      html += "<a class='btn primary' href='/txt?num=" + String(num) + "'>Download TXT</a>";
      if (SD_MMC.exists(wavPath))
        html += "<a class='btn' href='/wav?num=" + String(num) + "'>Download WAV</a>";
      html += "<a class='btn' style='margin-left:auto;color:#c0392b;border-color:#c0392b' "
              "href='/note/delete?num=" + String(num) + "' "
              "onclick=\"return confirm('Delete note #" + String(num) + "? This cannot be undone.')\">Delete</a>";
      html += "</div></div>";
    }
    html += "</div>";
  }

  html += "<script>"
          "document.querySelectorAll('[data-utc]').forEach(function(el){"
          "var d=new Date(el.dataset.utc);"
          "if(!isNaN(d)){el.textContent=d.toLocaleString([],{year:'numeric',month:'short',day:'2-digit',hour:'2-digit',minute:'2-digit'});}"
          "});"
          "</script>";
  html += "</div></body></html>";
  transferServer.send(200, "text/html", html);
}

void handlePortalJson() {
  loadIndex();
  String json = "[";
  for (int v = 0; v < (int)noteIndex.size(); v++) {
    int i = (int)noteIndex.size() - 1 - v;
    if (v > 0) json += ",";
    json += "{";
    json += "\"num\":" + String(noteIndex[i].num) + ",";
    json += "\"tag\":\"" + String(noteIndex[i].tag) + "\",";
    json += "\"hasText\":" + String(noteIndex[i].hasText ? "true" : "false");
    json += "}";
  }
  json += "]";
  transferServer.send(200, "application/json", json);
}

void handleExportTxt() {
  loadIndex();
  String filter = "All";
  if (transferServer.hasArg("tag")) filter = transferServer.arg("tag");

  String exportText = "Pala Note Export\nFilter: " + filter + "\n------------------------------\n\n";

  for (int v = 0; v < (int)noteIndex.size(); v++) {
    int i = (int)noteIndex.size() - 1 - v;
    if (!(filter == "All" || filter == String(noteIndex[i].tag))) continue;
    int num = noteIndex[i].num;
    char txtPath[64]; snprintf(txtPath, sizeof(txtPath), "%s/note_%03d.txt", NOTES_DIR, num);
    String transcript = readSmallFile(txtPath, 4000);
    if (transcript.length() == 0)
      transcript = noteIndex[i].hasText ? "(empty transcript)" : "Not transcribed yet.";
    exportText += "#";
    if (num < 100) exportText += "0";
    if (num < 10)  exportText += "0";
    exportText += String(num) + " · " + String(noteIndex[i].tag) + "\n";
    String createdUtc = noteCreatedUtc(num);
    if (createdUtc.length() > 0) exportText += createdUtc + "\n";
    exportText += "\n" + transcript + "\n\n------------------------------\n\n";
    if (exportText.length() > 55000) {
      exportText += "\nExport truncated on device because it became too large.\n";
      break;
    }
  }

  String filename = "pala_notes_export";
  if (filter != "All") filename += "_" + filter;
  filename += ".txt";
  transferServer.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  transferServer.send(200, "text/plain", exportText);
}

void sendFileByNum(const char* ext, const char* mime, bool attachment) {
  if (!transferServer.hasArg("num")) { transferServer.send(400, "text/plain", "Missing num"); return; }
  int num = transferServer.arg("num").toInt();
  if (num <= 0) { transferServer.send(400, "text/plain", "Invalid num"); return; }
  char path[64]; snprintf(path, sizeof(path), "%s/note_%03d.%s", NOTES_DIR, num, ext);
  File f = SD_MMC.open(path);
  if (!f) { transferServer.send(404, "text/plain", "File not found"); return; }
  if (attachment) {
    String filename = String("note_") + String(num) + "." + String(ext);
    transferServer.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  }
  transferServer.streamFile(f, mime);
  f.close();
}

void handleTagAdd() {
  if (!transferServer.hasArg("name")) {
    transferServer.sendHeader("Location", "/tags?msg=missing");
    transferServer.send(303); return;
  }
  String name = urlDecodeSimple(transferServer.arg("name"));
  bool ok = addCustomTag(name.c_str());
  transferServer.sendHeader("Location", ok ? "/tags?msg=added" : "/tags?msg=exists");
  transferServer.send(303);
}

void handleTagDelete() {
  if (!transferServer.hasArg("name")) {
    transferServer.sendHeader("Location", "/tags?msg=missing");
    transferServer.send(303); return;
  }
  String name = urlDecodeSimple(transferServer.arg("name"));
  bool hadNotes = tagHasNotes(name.c_str());
  bool ok = deleteTag(name.c_str());
  if (ok && hadNotes) transferServer.sendHeader("Location", "/tags?msg=moved");
  else                transferServer.sendHeader("Location", ok ? "/tags?msg=deleted" : "/tags?msg=protected");
  transferServer.send(303);
}

void handleTagsPage() {
  loadTags();
  loadIndex();
  activeFilter = -1;

  String html = "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Pala Tags</title>"
                "<style>"
                "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;margin:0;padding:24px;background:#f3f0e9;color:#111}"
                ".wrap{max-width:720px;margin:0 auto}"
                "h1{font-size:42px;line-height:.9;letter-spacing:-.05em;margin:0 0 22px;font-weight:800}"
                ".card{background:#fffaf1;border:1.5px solid #111;border-radius:24px;padding:18px;margin:14px 0;box-shadow:4px 4px 0 #111}"
                ".row{display:flex;justify-content:space-between;align-items:center;gap:12px;border-top:1px solid #ddd;padding:12px 0}"
                ".row:first-child{border-top:0}"
                ".tag{font-size:20px;font-weight:700}"
                ".meta{font-size:13px;color:#666;margin-top:4px}"
                "input{font:inherit;padding:12px;border:1.5px solid #111;border-radius:999px;background:#fff;width:100%;box-sizing:border-box}"
                "button,.btn{font:inherit;border:1.5px solid #111;border-radius:999px;padding:10px 14px;background:#111;color:#fff;text-decoration:none;white-space:nowrap}"
                ".danger{background:#fffaf1;color:#111}"
                ".msg{border:1.5px solid #111;border-radius:18px;padding:12px 14px;background:#fff;margin:12px 0}"
                ".hint{font-size:13px;color:#666;line-height:1.4}"
                "form.add{display:flex;gap:10px}"
                "</style></head><body><div class='wrap'>";

  html += "<h1>pala<br>tags</h1>";
  html += "<a class='btn' href='/'>Back to notes</a>";

  if (transferServer.hasArg("msg")) {
    String msg = transferServer.arg("msg");
    html += "<div class='msg'>";
    if (msg == "added") html += "Tag added.";
    else if (msg == "exists")    html += "Tag already exists or cannot be added.";
    else if (msg == "deleted")   html += "Tag deleted.";
    else if (msg == "moved")     html += "Tag deleted. Existing notes were moved to Untagged.";
    else if (msg == "protected") html += "This tag cannot be deleted.";
    else html += "Please enter a tag name.";
    html += "</div>";
  }

  html += "<div class='card'><form class='add' action='/tag/add' method='get'>"
          "<input name='name' maxlength='31' placeholder='New tag name'>"
          "<button type='submit'>Add</button></form>"
          "<p class='hint'>Tags appear on the device after recording. Keep them short for the e-paper UI.</p></div>";

  html += "<div class='card'>";
  for (int i = 0; i < tagCount; i++) {
    int cnt = 0;
    for (int n = 0; n < (int)noteIndex.size(); n++)
      if (strcmp(noteIndex[n].tag, tags[i]) == 0) cnt++;
    html += "<div class='row'><div><div class='tag'>" + htmlEscape(String(tags[i])) + "</div>";
    html += "<div class='meta'>" + String(cnt) + (cnt == 1 ? " note" : " notes");
    if (cnt > 0) html += " · deleting moves them to Untagged";
    html += "</div></div>";
    if (strcasecmp(tags[i], "Untagged") != 0) {
      html += "<a class='btn danger' href='/tag/delete?name=" + htmlEscape(String(tags[i])) + "' "
              "onclick=\"return confirm('Delete this tag? Notes will not be deleted. Existing notes will move to Untagged.');\">Delete</a>";
    }
    html += "</div>";
  }
  html += "</div></div></body></html>";
  transferServer.send(200, "text/html", html);
}

void handleNoteDelete() {
  if (!transferServer.hasArg("num")) { transferServer.send(400, "text/plain", "Missing num"); return; }
  int num = transferServer.arg("num").toInt();
  if (num <= 0) { transferServer.send(400, "text/plain", "Invalid num"); return; }
  deleteNote(num);
  transferServer.sendHeader("Location", "/");
  transferServer.send(303);
}

void setupTransferServer() {
  transferServer.on("/", HTTP_GET, handlePortalRoot);
  transferServer.on("/tags", HTTP_GET, handleTagsPage);
  transferServer.on("/tag/add", HTTP_GET, handleTagAdd);
  transferServer.on("/tag/delete", HTTP_GET, handleTagDelete);
  transferServer.on("/note/delete", HTTP_GET, handleNoteDelete);
  transferServer.on("/api/notes", HTTP_GET, handlePortalJson);
  transferServer.on("/export.txt", HTTP_GET, handleExportTxt);
  transferServer.on("/txt",   HTTP_GET, [](){ sendFileByNum("txt", "text/plain", true); });
  transferServer.on("/wav",   HTTP_GET, [](){ sendFileByNum("wav", "audio/wav",  true); });
  transferServer.on("/audio", HTTP_GET, [](){ sendFileByNum("wav", "audio/wav",  false); });
  transferServer.onNotFound([](){
    transferServer.send(404, "text/plain", "Not found");
  });
}

void stopTransferMode() {
  if (transferServerActive) {
    transferServer.stop();
    transferServerActive = false;
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  transferUrl = "";
}
