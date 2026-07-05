#pragma once

bool record();
bool recordQuestion(const char* path, void (*onTick)(int secsLeft) = nullptr);
bool playWavFile(const char* path);
