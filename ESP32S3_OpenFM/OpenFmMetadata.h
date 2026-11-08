#pragma once
#include <Arduino.h>

// Czytnik ID3v2 transportowanego wewnatrz prywatnego PID MPEG-TS.
// Nie modyfikuje ani jednego bajtu przekazywanego do dekodera AAC.
namespace OpenFmMetadata {
  void reset();
  void feed(const uint8_t* data, size_t size);
  // Bezpieczny odczyt danych pochodzacych z zadania FreeRTOS.
  bool snapshot(String& artist, String& title, uint32_t& version);
  void diagnostics();
}
