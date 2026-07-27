# Golden Nugget — Firmware v1.3 Release Notes

This release adds spoken answers from the Voice Bot, fixes an audio-quality issue
in long recordings, and tidies up how notes are stored.

---

## New

**The Voice Bot now talks back**
When you ask the bot a question, it now reads the answer aloud through the
device speaker using ElevenLabs, right after the answer appears. The answer text
and the voice arrive together, so there's no silent gap. Tap the front button to
stop the voice early.

**Read-aloud on/off in Settings**
A new "read aloud" toggle in the Settings menu turns spoken answers on or off
whenever you like — no re-flashing needed.

---

## Fixed

**Audio dropouts in long recordings**
Long recordings had small gaps at a regular interval (audible on playback, and
missing words in the transcript). This was the countdown display briefly
interrupting audio capture; the recording buffer has been enlarged so audio is
captured continuously. Long recordings should now be gap-free.

---

## Changed

**Cleaner storage**
- Very short accidental recordings (under 1 second) are no longer saved.
- Bot questions and answers are no longer stored on the device — the answer is
  shown (and now spoken), but nothing is written to the card.

---

## Notes for this release

- Read-aloud requires an ElevenLabs account and API key (set in secrets.h), with
  available character credits. It uses a device-friendly audio format that works
  on ElevenLabs' lower tiers.
- Read-aloud is billed per character by ElevenLabs; short answers are cheap, and
  the free monthly credits cover occasional use.
- The read-aloud toggle resets to "on" at each power-up (matching the sounds
  toggle). The master switch is still the TTS_ENABLED setting in the firmware.
- OpenAI (bot + transcription) still requires its own funded API key.
- Wi-Fi is 2.4GHz only.

# Firmware v1.2 Release Notes

A custom build on the Pala Note base, rebranded "Golden Nugget," adding long-form
recording, an on-device AI assistant, smarter connectivity, and a more capable
sync experience.

---

## Highlights

**Voice Bot — ask a question, get an answer on the device**
Double-press the front button and ask a question out loud. The device records for
a short window, transcribes it, sends it to an AI model, and shows the answer
right on the e-ink screen — no phone needed. Each question and answer is saved as
a searchable note (tagged "Bot"). The prospector mascot reacts through the whole
flow: cupping his ear while listening, thinking while it works, and an "aha!"
moment when the answer lands.

**Long (meeting) recording — up to 30 minutes**
A new hands-free recording mode for meetings and long thoughts. Start it with a
double-press, watch a clean countdown ring tick down the remaining time, and it
saves automatically. Long recordings are fully transcribed too (see Sync below).

**Golden Nugget look**
New prospector logo on the home screen, "Golden Nugget ready" branding, a refined
countdown display, and a battery percentage indicator in the corner of every
screen.

---

## Recording & Transcription

- **Long recordings now transcribe reliably.** A 30-minute recording is far too
  large for the transcription service in one piece, so the device now splits long
  audio into segments automatically, transcribes each, and stitches the text back
  together — with a small overlap so no words are lost at the seams.
- **Two-bar sync progress.** The syncing screen now shows both the current note's
  progress (chunk-by-chunk for long recordings) and overall progress across all
  notes, so a long sync no longer looks frozen.
- **Cancel a sync anytime.** Tap any button during a sync to stop it. The note or
  segment in progress finishes cleanly first, so nothing is left half-done.

---

## Connectivity

- **Multiple Wi-Fi networks.** The device can now remember several networks and
  automatically connects to the strongest one in range. Networks can be set in
  the firmware or added on the SD card without re-flashing.
- **Open-network fallback (with your permission).** As a last resort, if no known
  network is available, the device can offer to join an open Wi-Fi — but only
  after you confirm on screen, so it never silently joins an untrusted network.
- **Quieter Wi-Fi indicator.** Connecting now shows a small Wi-Fi icon in the
  corner instead of taking over the whole screen.

---

## Sync from the browser

- **Sync button in the web portal.** When the device is in transfer mode, the
  built-in web page now has a Sync button, so you can kick off transcription from
  your computer as well as from the device.

---

## Organization

- **New tags.** Added a "Task" tag (a foundation for routing notes to a to-do
  workflow) and a "Bot" tag for saved question-and-answer notes.

---

## Notes for this release

- Transcription and the Voice Bot require an OpenAI API key with available credit.
  If credit runs out, the device now shows a clear "Insufficient credit" message
  instead of failing silently.
- Long-recording transcription takes several minutes and costs roughly what the
  audio length costs at standard rates (a 30-minute note is about 18 cents).
- Wi-Fi is 2.4GHz only.
- Coming next: optional text-to-speech so the Voice Bot can read answers aloud.
