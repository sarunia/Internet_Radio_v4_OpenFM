# ESP32-S3 OpenFM HLS — 200 nazw stacji i metadane ID3v2

**Wersja testowa (2026-11-08).** Komplet wszystkich plików źródłowych. Zmiany dotyczą
katalogu i ekranu; tor odtwarzania HLS/TS/AAC, zegar i pilot zachowano.

## Co nowego

- **200 pozycji z prawdziwymi nazwami i slugami** z publicznej listy https://getradio.reconv.pl/openfm.
- Nazwa na drugiej linii TFT, numer `KANAL 001/200` na pierwszej.
- **Tytuł i wykonawca**: dotychczasowy działający parser MPEG-TS PID 0x15 / PES / ID3v2 (`TIT2`, `TPE1`).
- Numer `OFMxxx` nie jest numerem pozycji. Dla nieznanych kanałów urządzenie pyta `https://getradio.reconv.pl/openfm?s=<slug>` i bez śledzenia przekierowania czyta nagłówek `Location` (HTTP 3xx), z którego pobiera `OFMxxx`. Potem **dotychczasowy** `OpenFmResolver` pobiera token z OpenFM.
- Dwa sprawdzone publiczne powiązania: `90s Dance -> OFM168` (opis Elektroda przekazany przez użytkownika) oraz `Impreza -> OFM57` (redirect GETRADIO); nie wymagają katalogu przy starcie. Pozostałe ustalane są w trakcie pracy, zapamiętywane tylko do restartu.

## WAŻNE: ograniczenia

**Lista zawiera 200 rzeczywistych nazw katalogowych, lecz NIE oznacza to 200 zweryfikowanych strumieni OpenFM HLS.** GETRADIO wymienia także radia partnerskie/niezależne. Dla pozycji, która nie przekierowuje na domenę CDN OpenFM i `OFMxxx`, aplikacja wypisze błąd i nie uruchomi odtwarzania. Skorzystaj z `n`/`p`, aby przejść dalej. Format usługi GETRADIO nie jest stabilnym API.

Dostępność strumieni, metadanych ID3 i poprawność wszystkich 200 pozycji **nie została przetestowana** na urządzeniu. W testach użytkownika parser ID3 działał na `OFM10`. Długie nazwy na TFT są ucinane; czcionka GFX nie ma polskich znaków, dlatego nazwy na ekranie zapisano bez diakrytyków. Oryginalne nazwy są w katalogu GETRADIO.

## Instalacja

1. Rozpakuj **całe archiwum**. Nie mieszaj plików z różnymi wersjami.
2. Otwórz `ESP32S3_OpenFM_200_Stacji_ID3.ino` w Arduino IDE z ESP32 core 3.3.12.
3. Wszystkie pliki `.ino`, `.cpp`, `.h` pozostaw razem w jednym folderze o nazwie `ESP32S3_OpenFM_200_Stacji_ID3`.
4. Biblioteki: WiFiManager, ArduinoJson, Adafruit GFX, DevLab_AudioTools, arduino-libhelix, czcionka `DS_DIGII35pt7b.h` zgodna z dotychczasowym projektem.
5. Wybierz właściwą płytkę ESP32-S3 N16R8 z PSRAM i wgraj.

## Obsługa

- Pilot: w lewo/w prawo = poprzednia/następna, VOL +/- głośność, STOP/OK/MUTE jak wcześniej.
- Monitor Serial **115200**: `g 1` .. `g 200`; `n`, `p`; `o OFM10` dla ręcznego kanału; `m` diagnostyka, `h` pomoc.
- Numeracja jest pozycją w tej konkretnej liście (nie ID `OFM`).

## Łącza sprzętowe

- TFT ILI9488: CS GPIO5, DC GPIO4, SCK GPIO11, MOSI GPIO12, MISO GPIO13; canvas 480x320 i SPI 40 MHz.
- Pilot NEC GPIO15.
- I2S DAC PCM5102A: BCLK GPIO16, DIN GPIO17, LRCK GPIO18.

## Dziennik diagnostyczny

- `[KATALOG] ... -> OFMxxx`: pomyślne ustalenie identyfikatora.
- `[KATALOG] ... to nie strumien OpenFM HLS`: stacja w katalogu, ale nieobsługiwana przez ten player.
- `[ID3] ...`: pakiety transportowe i odczytane tagi.

## Uznanie autorstwa i prawa

GETRADIO i OpenFM są niezależnymi serwisami; ich dostępność i zasady użycia mogą się zmienić. Kod wykorzystuje biblioteki firm trzecich na ich własnych licencjach. Jeśli publikujesz ten projekt jako GPL-3.0, dołącz pełny `LICENSE` oraz wymagane informacje o autorach bibliotek, jak w swoim repozytorium. Nie umieszczaj kluczy Wi-Fi ani podpisanych tokenów w repozytorium.
