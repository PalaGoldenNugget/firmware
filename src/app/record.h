#pragma once

bool record();
extern bool g_lastRecTooShort;   // true if the last record() discarded a too-short note
bool recordQuestion(const char* path, void (*onTick)(int secsLeft) = nullptr);
bool playWavFile(const char* path);
bool speakPcmFile(const char* path);   // play raw 16k PCM (TTS output)
