#pragma once
#include <Arduino.h>
namespace OpenFmCatalog {
  struct Entry { const char* name; const char* slug; uint16_t fixedId; };
  int count();
  const Entry& entry(int slot);
  String knownId(int slot); // bez zapytania sieciowego, przydatne na ekranie
  String resolveId(int slot); // HTTP redirect z GETRADIO -> prawdziwe OFMxxx
}
