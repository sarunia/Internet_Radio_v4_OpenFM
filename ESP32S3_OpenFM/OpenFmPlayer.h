#pragma once
#include <Arduino.h>

// Samodzielny silnik: OpenFM API -> MASTER -> MEDIA -> MPEG-TS -> Helix AAC -> I2S.
// Bez HLSStream.h; ta wersja nie używa parsera z błędem assert.
namespace OpenFmPlayer {
  bool start(const String &channel, int volume);
  void stop();
  bool busy();
  void setVolume(int level); // 0..21
  void stats();
  String state();
  uint32_t audioBytes();
  uint32_t segments();
  uint32_t lastSegmentBytes(); // bajty ostatniego poprawnie pobranego segmentu TS
  uint32_t bitrateKbps();   // zmierzona przeplywnosc kontenera MPEG-TS [kb/s]
  uint32_t sampleRate();    // czestotliwosc z dekodera AAC [Hz]
  uint8_t channels();
  uint8_t bitsPerSample();  // rozdzielczosc PCM
}
