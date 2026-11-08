#pragma once
#include <Arduino.h>
namespace OpenFmCatalog {
  struct Entry { const char* name; const char* slug; uint16_t fixedId; };
  int count();
  const Entry& entry(int slot);
  String knownId(int slot); // pusty tekst, gdy ID nie zostalo potwierdzone
  String resolveId(int slot); // katalog lokalny; bez HTTP do GETRADIO
}
