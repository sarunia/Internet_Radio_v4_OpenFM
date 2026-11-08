#pragma once
#include <Arduino.h>

// Test nieoficjalnego interfejsu OpenFM opisanego w publicznym skrypcie.
// channelId: np. OFM10, streamPrefix: pelny adres bazowy playlisty.
// Nie zwraca tokenu poza pamiecia RAM i nie zapisuje go na karcie SD.
struct OpenFmResult {
  bool success = false;
  int httpCode = 0;
  String playlistUrl;
  String error;
};

OpenFmResult resolveOpenFm(const String& channelId, const String& streamPrefix = "https://stream-cdn-1.open.fm");
