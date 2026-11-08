#include "OpenFmResolver.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Przykladowe wywolanie z publicznego skryptu PowerShell:
// https://open.fm/api/user/token?fp=https://stream-cdn-1.open.fm/OFM10/ngrp:standard/playlist.m3u8
// Format API nie jest publicznie gwarantowany: w razie zmiany zwracamy blad.

static String escapeParam(const String& text) {
  static const char* HEX_DIGITS = "0123456789ABCDEF";
  String encoded;
  encoded.reserve(text.length() * 3);
  for (size_t i = 0; i < text.length(); ++i) {
    unsigned char c = (unsigned char)text[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += char(c);
    } else {
      encoded += '%';
      encoded += HEX_DIGITS[c >> 4];
      encoded += HEX_DIGITS[c & 15];
    }
  }
  return encoded;
}

OpenFmResult resolveOpenFm(const String& rawChannelId, const String& streamPrefix) {
  OpenFmResult result;
  String id = rawChannelId;
  id.trim();
  id.toUpperCase();
  if (id.length() && id[0] >= '0' && id[0] <= '9') id = "OFM" + id;
  if (!id.startsWith("OFM") || id.length() < 4) {
    result.error = "Niepoprawny identyfikator (wpisz np. OFM10)";
    return result;
  }
  for (size_t i = 3; i < id.length(); ++i) {
    if (!isDigit(id[i])) {
      result.error = "Identyfikator musi miec postac OFM + cyfry";
      return result;
    }
  }
  if (WiFi.status() != WL_CONNECTED) {
    result.error = "Brak polaczenia WiFi";
    return result;
  }
  if (!streamPrefix.startsWith("https://")) {
    result.error = "Adres serwera musi zaczynac sie od https://";
    return result;
  }
  String base = streamPrefix;
  if (base.endsWith("/")) base.remove(base.length()-1);
  const String source = base + "/" + id + "/ngrp:standard/playlist.m3u8";
  const String requestUrl = "https://open.fm/api/user/token?fp=" + escapeParam(source);
  Serial.println("[OPENFM] Kanal: " + id);
  Serial.println("[OPENFM] Adres bazowy: " + source);
  Serial.println("[OPENFM] Pobieram aktualny URL HLS...");

  WiFiClientSecure client;
  // TYLKO TEST! Brak weryfikacji certyfikatu TLS. Produkcyjnie ustaw CA.
  client.setInsecure();
  client.setTimeout(12000);
  HTTPClient http;
  http.setTimeout(12000);
  http.setConnectTimeout(8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, requestUrl)) {
    result.error = "HTTPClient.begin() nie powiodl sie";
    return result;
  }
  http.addHeader("Accept", "application/json");
  http.addHeader("Cache-Control", "no-cache");
  http.addHeader("User-Agent", "ESP32-S3-HLS-Test/1.0");
  result.httpCode = http.GET();
  Serial.printf("[OPENFM] HTTP=%d, Content-Type=%s\n", result.httpCode, http.header("Content-Type").c_str());
  if (result.httpCode != HTTP_CODE_OK) {
    result.error = "API OpenFM: HTTP " + String(result.httpCode);
    if (result.httpCode <= 0) result.error += " (blad polaczenia/TLS)";
    if (result.httpCode == 403) result.error += " (dostep zabroniony; nie probujemy obchodzic blokady)";
    http.end();
    return result;
  }
  // Odczyt ograniczony do 16 kB - brak niekontrolowanej alokacji na odpowiedz.
  const int size = http.getSize();
  if (size > 16384) {
    result.error = "Zbyt duza odpowiedz API";
    http.end();
    return result;
  }
  JsonDocument doc;
  DeserializationError jsonError = deserializeJson(doc, http.getStream(), DeserializationOption::NestingLimit(8));
  http.end();
  if (jsonError) {
    result.error = "API nie zwrocilo poprawnego JSON: " + String(jsonError.c_str());
    return result;
  }
  const char* value = doc["url"] | "";
  if (!value[0]) {
    result.error = "Brak pola 'url' w JSON (API moglo zmienic format)";
    return result;
  }
  result.playlistUrl = String(value);
  if (!result.playlistUrl.startsWith("https://") && !result.playlistUrl.startsWith("http://")) {
    result.error = "Pole 'url' nie zawiera poprawnego adresu HTTP";
    result.playlistUrl = "";
    return result;
  }
  // Nie wypisujemy pelnego URL z tokenem do logu.
  Serial.printf("[OPENFM] Otrzymano adres strumienia (%u znakow).\n", (unsigned)result.playlistUrl.length());
  result.success = true;
  return result;
}
