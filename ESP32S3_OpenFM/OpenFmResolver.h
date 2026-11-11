/*=============================================================================
  OpenFmResolver.h - INTERFEJS MODUŁU POBIERANIA ADRESU OPENFM HLS
  -----------------------------------------------------------------------------
  Deklaracje używane przez odtwarzacz OpenFmPlayer.cpp. Moduł na podstawie
  identyfikatora kanału OpenFM pobiera z API adres playlisty HLS, który może
  zawierać czasowy token. Nie zarządza dźwiękiem, katalogiem stacji ani ID3.

  Wymagane zależności: Arduino-ESP32 (String / Arduino.h). Implementacja w
  OpenFmResolver.cpp używa WiFi, HTTPClient, WiFiClientSecure i ArduinoJson.
  Wersji bibliotek zainstalowanych w Arduino IDE nie można odczytać z nagłówka.

  Bezpieczeństwo: URL playlisty może zawierać token; traktować go jako dane
  wrażliwe na ujawnienie. Aktualny klient HTTPS ma wyłączoną weryfikację TLS.
=============================================================================*/

#pragma once
#include <Arduino.h>

// Struktura wyniku pojedynczej próby uzyskania playlisty z API OpenFM.
// channelId: np. OFM10; streamPrefix: bazowy adres HTTPS serwera HLS.
// Wynik jest przechowywany w RAM; funkcja nie zapisuje go na karcie SD.
// Pola informują zarówno o sukcesie, jak i o powodzie niepowodzenia.
// Nie należy wypisywać playlistUrl do logów: może zawierać parametr tokenu.
struct OpenFmResult {
  // Czy poprawnie odczytano i zweryfikowano adres playlisty HLS.
  bool success = false;
  // Kod odpowiedzi HTTP (0, gdy nie wykonano żądania).
  int httpCode = 0;
  // Pełny URL playlisty zwrócony przez API, potencjalnie z tokenem.
  String playlistUrl;
  // Powód niepowodzenia przeznaczony do diagnostyki.
  String error;
};

// -----------------------------------------------------------------------------
// resolveOpenFm()
// channelId: numer kanału, np. "OFM10" lub "10" (implementacja dodaje prefiks).
// streamPrefix: bazowy serwer HTTPS; domyślnie stream-cdn-1.open.fm.
// Wynik: OpenFmResult z kodem HTTP, adresem playlisty lub opisem błędu.
// Operacja wykonuje zapytanie sieciowe i może blokować do czasu odpowiedzi;
// nie należy jej wywoływać z przerwań ani w pętli odświeżania ekranu.
// -----------------------------------------------------------------------------
OpenFmResult resolveOpenFm(const String& channelId, const String& streamPrefix = "https://stream-cdn-1.open.fm");
