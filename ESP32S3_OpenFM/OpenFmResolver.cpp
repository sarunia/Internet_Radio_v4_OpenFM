/*=============================================================================
  OpenFmResolver.cpp - UZYSKIWANIE ADRESU HLS DLA STACJI OPENFM
  -----------------------------------------------------------------------------
  Przeznaczenie:
    Moduł rozpoznaje identyfikator kanału (np. OFM10), buduje adres źródłowy
    playlisty HLS i wysyła żądanie do interfejsu OpenFM w celu uzyskania
    bieżącego adresu playlisty, który może zawierać czasowy token dostępu.

  Schemat wywołania:
    OpenFmPlayer::worker() -> resolveOpenFm() -> HTTPS GET API -> JSON["url"]
    -> wynik OpenFmResult -> pobranie playlisty MASTER przez odtwarzacz.
    Ten moduł nie dekoduje AAC, nie pobiera segmentów MPEG-TS ani nie
    przetwarza metadanych ID3. Za te działania odpowiadają inne moduły.

  Zależności:
    - Arduino-ESP32: Arduino.h, WiFi.h, WiFiClientSecure.h, HTTPClient.h.
    - ArduinoJson: JsonDocument i deserializeJson() (wersja zainstalowanej
      biblioteki wymaga potwierdzenia w Arduino IDE).
    - OpenFmResolver.h: struktura wyniku i deklaracja funkcji publicznej.

  Bezpieczeństwo i ograniczenia:
    - setInsecure() wyłącza sprawdzanie certyfikatu serwera HTTPS. Jest to
      istniejące ustawienie testowe, a nie rekomendacja produkcyjna.
    - Adres wynikowy może zawierać poufny token; jego pełnej treści nie
      drukujemy w monitorze Serial ani nie zapisujemy w pamięci trwałej.
    - Wykorzystywany interfejs API nie ma gwarantowanej stabilności formatu.
    - Ten plik jedynie dokumentuje istniejące zachowanie; nie wprowadzono
      żadnych zmian w transmisji, limitach czasu ani obsłudze błędów.
=============================================================================*/

#include "OpenFmResolver.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Przykładowe wywołanie interfejsu, zaczerpnięte z publicznego skryptu PowerShell:
// https://open.fm/api/user/token?fp=https://stream-cdn-1.open.fm/OFM10/ngrp:standard/playlist.m3u8
// Format odpowiedzi API nie jest gwarantowany: przy niezgodności zwracany jest błąd.

// -----------------------------------------------------------------------------
// escapeParam()
// Koduje tekst jako wartość parametru URL (percent-encoding zgodny z regułami
// znaków niezastrzeżonych). Pozostawia litery, cyfry oraz - _ . ~, a pozostałe
// bajty zapisuje jako %HH. Funkcja pracuje na bajtach String, nie na znakach
// Unicode; jest to właściwe dla kodowania adresu w zapytaniu HTTP.
// Parametr text: wartość parametru fp, czyli źródłowy URL playlisty HLS.
// Wynik: nowy napis przeznaczony do umieszczenia po znaku równości w URL API.
// -----------------------------------------------------------------------------
static String escapeParam(const String& text) {
  // Tablica cyfr szesnastkowych służąca do zapisu pojedynczego bajtu jako %HH.
  static const char* HEX_DIGITS = "0123456789ABCDEF";
  String encoded;
  // W najgorszym przypadku każdy bajt wejściowy zajmie trzy bajty w wyniku.
  encoded.reserve(text.length() * 3);
  for (size_t i = 0; i < text.length(); ++i) {
    // Traktujemy bajt jako bez znaku, aby przesunięcia bitowe były jednoznaczne.
    unsigned char c = (unsigned char)text[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += char(c);
    } else {
      // Kodowanie znaku zastrzeżonego: procent i dwie cyfry szesnastkowe.
      encoded += '%';
      encoded += HEX_DIGITS[c >> 4];
      encoded += HEX_DIGITS[c & 15];
    }
  }
  return encoded;
}

// -----------------------------------------------------------------------------
// resolveOpenFm()
// Główna funkcja modułu: weryfikuje kanał, łączność i bazowy adres CDN,
// następnie uzyskuje od API OpenFM aktualny URL playlisty HLS.
//
// Parametry:
//   rawChannelId - numer kanału z prefiksem OFM, np. "OFM149", lub same cyfry.
//   streamPrefix - adres HTTPS serwera strumieniowego, bez końcowego ukośnika.
//
// Zwraca OpenFmResult:
//   success     - true wyłącznie po otrzymaniu poprawnego URL playlisty.
//   httpCode    - kod HTTP odpowiedzi, jeżeli żądanie zostało wykonane.
//   playlistUrl - uzyskany URL HLS; może zawierać czasowo ważny token.
//   error       - komunikat diagnostyczny dla niepowodzenia.
//
// Funkcja działa synchronicznie; w projekcie wywołuje ją zadanie robocze HLS,
// a nie procedura obsługi przerwania pilota czy funkcja rysowania TFT.
// -----------------------------------------------------------------------------
OpenFmResult resolveOpenFm(const String& rawChannelId, const String& streamPrefix) {
  OpenFmResult result;
  // Kopia wejścia pozwala uporządkować identyfikator bez modyfikacji argumentu.
  String id = rawChannelId;
  // Usuwamy białe znaki z początku i końca identyfikatora.
  id.trim();
  // Nazwa kanału w API jest używana w formacie OFM + cyfry.
  id.toUpperCase();
  // Obsługujemy również zapis numeryczny: "10" przechodzi na "OFM10".
  if (id.length() && id[0] >= '0' && id[0] <= '9') id = "OFM" + id;
  if (!id.startsWith("OFM") || id.length() < 4) {
    result.error = "Niepoprawny identyfikator (wpisz np. OFM10)";
    return result;
  }
  // Po prefiksie OFM dopuszczalne są wyłącznie cyfry dziesiętne.
  for (size_t i = 3; i < id.length(); ++i) {
    if (!isDigit(id[i])) {
      result.error = "Identyfikator musi miec postac OFM + cyfry";
      return result;
    }
  }
  // Bez aktywnego Wi-Fi nie wykonujemy żadnego połączenia sieciowego.
  if (WiFi.status() != WL_CONNECTED) {
    result.error = "Brak polaczenia WiFi";
    return result;
  }
  // Istniejąca walidacja dopuszcza wyłącznie bazowy adres HTTPS.
  if (!streamPrefix.startsWith("https://")) {
    result.error = "Adres serwera musi zaczynac sie od https://";
    return result;
  }
  // Normalizujemy adres bazowy, aby nie powstał podwójny ukośnik.
  String base = streamPrefix;
  if (base.endsWith("/")) base.remove(base.length()-1);
  // Ścieżka źródłowa MASTER dla wskazanego kanału OpenFM.
  const String source = base + "/" + id + "/ngrp:standard/playlist.m3u8";
  // W zapytaniu fp przekazujemy zakodowany adres źródłowej playlisty.
  const String requestUrl = "https://open.fm/api/user/token?fp=" + escapeParam(source);
  Serial.println("[OPENFM] Kanal: " + id);
  Serial.println("[OPENFM] Adres bazowy: " + source);
  Serial.println("[OPENFM] Pobieram aktualny URL HLS...");

  // Klient TLS dla zapytania HTTPS do API OpenFM.
  WiFiClientSecure client;
  // Uwaga: istniejący tryb testowy bez weryfikacji certyfikatu TLS; docelowo użyć CA.
  client.setInsecure();
  // Limit czasu operacji sieciowych: 12 sekund (wartość bez zmian).
  client.setTimeout(12000);
  // Klient HTTP korzystający z przygotowanego połączenia TLS.
  HTTPClient http;
  http.setTimeout(12000);
  // Osobny limit zestawiania połączenia: 8 sekund.
  http.setConnectTimeout(8000);
  // Zezwalamy na obsługę przekierowań zgodnie z trybem biblioteki HTTPClient.
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, requestUrl)) {
    result.error = "HTTPClient.begin() nie powiodl sie";
    return result;
  }
  // Nagłówki żądania: oczekujemy JSON i prosimy o świeżą odpowiedź.
  http.addHeader("Accept", "application/json");
  http.addHeader("Cache-Control", "no-cache");
  http.addHeader("User-Agent", "ESP32-S3-HLS-Test/1.0");
  // Wykonanie żądania GET; zapisujemy kod HTTP do struktury wyniku.
  result.httpCode = http.GET();
  Serial.printf("[OPENFM] HTTP=%d, Content-Type=%s\n", result.httpCode, http.header("Content-Type").c_str());
  // Każdy kod różny od 200 zatrzymuje pobieranie przed analizą JSON.
  // Błędy ujemne oznaczają niepowodzenie na poziomie klienta HTTP.
  if (result.httpCode != HTTP_CODE_OK) {
    result.error = "API OpenFM: HTTP " + String(result.httpCode);
    if (result.httpCode <= 0) result.error += " (blad polaczenia/TLS)";
    if (result.httpCode == 403) result.error += " (dostep zabroniony; nie probujemy obchodzic blokady)";
    http.end();
    return result;
  }
  // Odczyt ograniczony do 16 kB, aby ograniczyć alokację pamięci na odpowiedź.
  // Content-Length może być nieznany; tutaj odrzucamy znane zbyt duże odpowiedzi.
  const int size = http.getSize();
  if (size > 16384) {
    result.error = "Zbyt duza odpowiedz API";
    http.end();
    return result;
  }
  // Dokument JSON przechowuje wynik odpowiedzi API w pamięci RAM.
  JsonDocument doc;
  // Deserializacja bezpośrednio ze strumienia HTTP; limit głębokości
  // zabezpiecza przed nadmiernie zagnieżdżonym dokumentem JSON.
  DeserializationError jsonError = deserializeJson(doc, http.getStream(), DeserializationOption::NestingLimit(8));
  http.end();
  // Po odczycie kończymy połączenie; dalsze operacje dotyczą danych w RAM.
  if (jsonError) {
    result.error = "API nie zwrocilo poprawnego JSON: " + String(jsonError.c_str());
    return result;
  }
  // Oczekiwane pole "url" zawiera kompletny adres playlisty z tokenem.
  // Operator | "" daje pustą wartość przy braku pola lub niewłaściwym typie.
  const char* value = doc["url"] | "";
  if (!value[0]) {
    result.error = "Brak pola 'url' w JSON (API moglo zmienic format)";
    return result;
  }
  // Kopiujemy URL do pola wyniku, które zwrócimy odtwarzaczowi HLS.
  result.playlistUrl = String(value);
  // Minimalna kontrola schematu URL (http/https), bez sprawdzania domeny.
  if (!result.playlistUrl.startsWith("https://") && !result.playlistUrl.startsWith("http://")) {
    result.error = "Pole 'url' nie zawiera poprawnego adresu HTTP";
    result.playlistUrl = "";
    return result;
  }
  // Nie wypisujemy pełnego adresu URL zawierającego token do dziennika Serial.
  Serial.printf("[OPENFM] Otrzymano adres strumienia (%u znakow).\n", (unsigned)result.playlistUrl.length());
  // Dopiero po wszystkich kontrolach oznaczamy próbę jako udaną.
  result.success = true;
  return result;
}
