/**
 * @file OpenFmPlayer.h
 * @brief Publiczny interfejs silnika odtwarzania OpenFM HLS dla ESP32-S3.
 *
 * Główny szkic .ino używa tych funkcji do uruchamiania stacji, regulacji
 * głośności oraz odczytu statystyk prezentowanych na wyświetlaczu ILI9488.
 * Implementacja w OpenFmPlayer.cpp samodzielnie obsługuje token OpenFM,
 * playlisty HLS, MPEG-TS i dekodowanie AAC z wyjściem I2S do PCM5102A.
 *
 * Zasady współpracy:
 * - start() uruchamia zadanie asynchroniczne, a nie blokującą sesję audio.
 * - stop() zgłasza zakończenie; busy() pozwala sprawdzić jego wykonanie.
 * - Parametry audio pojawiają się po rozpoznaniu formatu przez dekoder.
 * - Funkcje pomiarowe zwracają wartości z bieżącej sesji odtwarzania.
 *
 * Kompatybilność: ESP32 Arduino Core 3.3.12 używany w projekcie;
 * wersje zewnętrznych bibliotek należy sprawdzić w Arduino IDE.
 * Dokumentacja opisuje istniejący kod i nie zmienia jego zachowania.
 */

#pragma once
#include <Arduino.h>

// Przebieg: OpenFM API -> MASTER -> MEDIA -> MPEG-TS -> Helix AAC -> I2S.
// Samodzielna obsługa HLS bez HLSStream.h i jego problematycznego parsera.
namespace OpenFmPlayer {
  /** @brief Uruchamia odtwarzanie stacji (asynchronicznie).
   * @param channel Identyfikator stacji, np. OFM10.
   * @param volume Głośność początkowa 0..21.
   * @return true, jeśli udało się uruchomić zadanie sesji.
   */
  bool start(const String &channel, int volume);
  /** @brief Zgłasza zatrzymanie; zasoby są zwalniane asynchronicznie. */
  void stop();
  /** @brief true, gdy sesja nadal pracuje lub kończy działanie. */
  bool busy();
  /** @brief Ustawia poziom głośności w skali 0..21. */
  void setVolume(int level); // Zakres głośności: 0..21.
  /** @brief Wypisuje statystyki HLS, metadanych, RAM oraz PSRAM. */
  void stats();
  /** @brief Zwraca tekstowy stan sesji (STOP/ŁĄCZENIE/ODTWARZANIE itd.). */
  String state();
  /** @brief Licznik bajtów przekazanych z kolejki do dekodera. */
  uint32_t audioBytes();
  /** @brief Liczba ukończonych segmentów MPEG-TS. */
  uint32_t segments();
  /** @brief Rozmiar ostatniego w pełni pobranego segmentu TS w bajtach. */
  uint32_t lastSegmentBytes(); // Bajty ostatniego poprawnie pobranego segmentu TS.
  /** @brief Zmierzona przepływność MPEG-TS, nie samego AAC, w kb/s. */
  uint32_t bitrateKbps();   // Przepływność kontenera MPEG-TS [kb/s].
  /** @brief Próbkowanie audio odczytane z dekodera AAC, w Hz. */
  uint32_t sampleRate();    // Częstotliwość z dekodera AAC [Hz].
  /** @brief Liczba kanałów dźwięku (np. 2 dla stereo). */
  uint8_t channels();
  /** @brief Rozdzielczość zdekodowanych próbek PCM (np. 16 bitów). */
  uint8_t bitsPerSample();  // Rozdzielczość PCM.
}
