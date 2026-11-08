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
# OpenFM HLS Radio na ESP32-S3 — 200 stacji, ID3v2, ILI9488 i PCM5102A

![Status](https://img.shields.io/badge/status-projekt%20rozwojowy-orange)
![ESP32](https://img.shields.io/badge/platforma-ESP32--S3-blue)
![HLS](https://img.shields.io/badge/streaming-HLS%20%2F%20AAC-green)
![License](https://img.shields.io/badge/licencja-GPL--3.0--only-blue)

**Samodzielny internetowy radioodtwarzacz OpenFM** zbudowany na **ESP32-S3 N16R8** (16 MB Flash / 8 MB PSRAM), z kolorowym ekranem **ILI9488 IPS 480 × 320**, przetwornikiem **PCM5102A (I2S)**, pilotem **NEC IR** oraz obsługą metadanych **ID3v2** odczytywanych bezpośrednio ze strumienia **HLS / MPEG-TS**.

Projekt powstawał etapami: od uzyskania tokenu OpenFM i rozpracowania playlist, przez odtwarzanie AAC, po interfejs na TFT, katalog nazw stacji i rzeczywisty odczyt wykonawcy oraz tytułu utworu. Rozwiązanie jest **niezależnym projektem hobbystycznym**, a nie oficjalnym klientem OpenFM.

> **Stan na 8 listopada 2026 r.:** odtwarzanie HLS/AAC, obsługa pilota, ekran i odczyt tytułu oraz wykonawcy z ID3v2 zostały potwierdzone w testach urządzenia, w szczególności na kanale `OFM10`. Katalog zawiera **200 pozycji/nazw**, ale **nie oznacza to 200 zweryfikowanych, działających kanałów OpenFM**. Częstotliwość 48 kHz dla `OFM10` została poprawiona i użytkownik potwierdził prawidłowe tempo odtwarzania. Automatyczny dobór częstotliwości I2S dla każdej stacji wymaga jeszcze dopracowania.

## Spis treści

- [Najważniejsze możliwości](#najważniejsze-możliwości)
- [Potwierdzone testy i ograniczenia](#potwierdzone-testy-i-ograniczenia)
- [Sprzęt i połączenia](#sprzęt-i-połączenia)
- [Budowa i przepływ danych](#budowa-i-przepływ-danych)
- [Jak działa OpenFM HLS](#jak-działa-openfm-hls)
- [Metadane ID3v2 — wykonawca i tytuł](#metadane-id3v2--wykonawca-i-tytuł)
- [Katalog 200 stacji](#katalog-200-stacji)
- [Wyświetlacz i zegar](#wyświetlacz-i-zegar)
- [Ważne: prawidłowe tempo audio i I2S](#ważne-prawidłowe-tempo-audio-i-i2s)
- [Środowisko i biblioteki](#środowisko-i-biblioteki)
- [Struktura projektu](#struktura-projektu)
- [Instalacja i uruchomienie](#instalacja-i-uruchomienie)
- [Sterowanie pilotem i przez Serial](#sterowanie-pilotem-i-przez-serial)
- [Diagnostyka i typowe problemy](#diagnostyka-i-typowe-problemy)
- [Bezpieczeństwo i ograniczenia](#bezpieczeństwo-i-ograniczenia)
- [Plan rozwoju](#plan-rozwoju)
- [Licencja](#licencja)
- [Podziękowania i źródła](#podziękowania-i-źródła)

## Najważniejsze możliwości

- **OpenFM HLS** — pobieranie czasowego adresu playlisty przez endpoint tokenu, analiza playlist MASTER i MEDIA oraz pobieranie segmentów `.ts`.
- **AAC → PCM przez Helix → I2S → PCM5102A** — wyjście analogowe audio bez zewnętrznego konwertera ESP8266 HLS/ICY.
- **Osobny parser ID3v2** — odczyt `TIT2` (tytuł) i `TPE1` (wykonawca) z pakietów MPEG-TS/PES. Działa równolegle do dekodera audio.
- **Do 200 pozycji stacji z nazwami** — tabela katalogowa oraz próba wyznaczenia rzeczywistego `OFMxxx` na podstawie przekierowania ze strony katalogu.
- **Kolorowy TFT ILI9488 480 × 320** — rysowanie do `GFXcanvas16`, transfer RGB666 przez SPI przy **40 MHz** i aktualizacja wybranych obszarów.
- **Zegar NTP** — godzina z sekundami, czcionka `DS_DIGII35pt7b`, strefa Polski z automatyczną zmianą CET/CEST.
- **Pilot NEC** — przełączanie stacji, głośność, STOP, OK i MUTE.
- **Monitor szeregowy 115200** — komendy do sterowania, podgląd parametrów, diagnostyka HLS i ID3.
- **WiFiManager** — konfiguracja sieci przy pierwszym uruchomieniu.
- **Bufor ok. 96 KiB i zadania FreeRTOS** — oddzielenie odbioru segmentów od dekodowania AAC.

### Czego ten projekt *nie* obejmuje

To **osobny odtwarzacz testowo-rozwojowy OpenFM**. Nie jest automatycznie pełnym firmware wcześniejszego rozbudowanego radia z kartą SD, pogodą i kalendarzem. Te elementy nie są częścią niniejszego szkicu.

## Potwierdzone testy i ograniczenia

| Funkcja | Stan | Uwagi |
|---|---|---|
| Pobieranie tokenu i uruchamianie HLS OpenFM | ✅ Potwierdzone | API zwraca czasowy URL odtwarzania |
| MASTER → MEDIA → segmenty MPEG-TS | ✅ Potwierdzone | Działające odtwarzanie na ESP32-S3 |
| AAC / stereo przez PCM5102A | ✅ Potwierdzone | Dla `OFM10` dekoder zgłasza **48 000 Hz / 2 kanały** |
| Metadane ID3v2: `TIT2`, `TPE1` | ✅ Potwierdzone | Odczyt dwóch kolejnych utworów w logach `OFM10` |
| PID metadanych | ✅ Potwierdzone dla testu | `0x102` w jednym z testowanych strumieni; **nie kodować na stałe** |
| Pilot, TFT, zegar | ✅ Używane w działającym projekcie | Indywidualna konfiguracja pinów poniżej |
| Baza 200 nazw i pozycji | 🧪 Rozwojowe | Nazwy/slug, ale nie wszystkie streamy zidentyfikowane lub zweryfikowane |
| Automatyczne rozpoznawanie `OFMxxx` z katalogu | 🧪 Rozwojowe | Zależy od aktualnej struktury przekierowań GETRADIO |
| Automatyczna zmiana I2S 44,1 ↔ 48 kHz | ⏳ Do zrobienia | Poprawka **48 kHz** była skuteczna dla `OFM10` |
| Obsługa wszystkich wariantów HLS, fMP4 i szyfrowania | ❌ Nie | Implementacja celuje w nieszyfrowane segmenty TS/AAC |

## Sprzęt i połączenia

### Podstawowe moduły

- ESP32-S3 **N16R8** (16 MB pamięci Flash, 8 MB PSRAM).
- Wyświetlacz **ILI9488 IPS, 480 × 320** w trybie SPI.
- Przetwornik cyfrowo-analogowy **PCM5102A** z interfejsem I2S.
- Odbiornik podczerwieni współpracujący z pilotem NEC.
- Zasilanie odpowiednie do użytych modułów, wspólna masa GND, wyjście audio/wzmacniacz/głośniki.
- Sieć Wi-Fi z dostępem do Internetu.

### Połączenia ESP32-S3

| Funkcja | GPIO ESP32-S3 | Wejście modułu |
|---|---:|---|
| I2S BCLK | **16** | PCM5102A BCK/BCLK |
| I2S DATA | **17** | PCM5102A DIN |
| I2S LRCK / WS | **18** | PCM5102A LCK/LRCK |
| Odbiornik IR | **15** | OUT |
| TFT SCLK | **11** | ILI9488 SCK/CLK |
| TFT MOSI | **12** | ILI9488 MOSI/SDI |
| TFT MISO | **13** | ILI9488 MISO/SDO, jeśli moduł używa |
| TFT CS | **5** | ILI9488 CS |
| TFT DC / RS | **4** | ILI9488 DC |

> **Uwaga:** to połączenia z opisywanej implementacji, nie uniwersalny pinout wszystkich płytek ESP32-S3 i modułów ILI9488. Sprawdź dopuszczalne napięcia logiki i zasilania konkretnego wyświetlacza, polaryzację odbiornika IR oraz wspólną masę. Sterowanie RESET i podświetleniem TFT zależy od użytego modułu.

## Budowa i przepływ danych

```text
   Pilot IR NEC                    Monitor Serial
        |                                |
        +----------> główny szkic <------+
                         |
                  wybór pozycji 1..200
                         |
              OpenFmCatalog.cpp
            nazwa + slug + OFMxxx
                         |
               OpenFmResolver.cpp
            GET /api/user/token?fp=...
                         |
                 tymczasowy URL
                         |
               playlista MASTER
                         |
            playlista MEDIA / HLS
                         |
                  segmenty .ts
                         |
            +------------+------------+
            |                         |
     OpenFmPlayer.cpp         OpenFmMetadata.cpp
     bufor / FreeRTOS          PAT -> PMT -> PID
     MPEG-TS -> AAC           PES -> ID3v2
     Helix -> PCM             TIT2 / TPE1
            |                         |
       I2S / PCM5102A         wykonawca / tytuł
            |                         |
       WYJŚCIE AUDIO            GFXcanvas16
                                       |
                           SPI 40 MHz / ILI9488
```

Strumień metadanych **nie jest dokładany do strumienia audio** ani konwertowany do `ICY StreamTitle`: ESP32-S3 rozdziela te dane lokalnie i wyświetla je na ekranie. To upraszcza naszą konstrukcję w porównaniu z rozwiązaniami wymagającymi osobnego mikrokontrolera pośredniczącego.

### Moduły

| Plik | Zadanie |
|---|---|
| `ESP32S3_OpenFM_200_Stacji_ID3.ino` | `setup()/loop()`, Wi-Fi, pilot, obsługa komend, ekran, zegar, wybór stacji |
| `OpenFmCatalog.h/.cpp` | Tablica 200 nazw/slugów, znane identyfikatory i rozpoznawanie `OFMxxx` z przekierowania |
| `OpenFmResolver.h/.cpp` | Pobranie aktualnego URL z tokenem OpenFM i rozpoznanie odpowiedzi JSON |
| `OpenFmPlayer.h/.cpp` | Playlista MASTER/MEDIA, transfer segmentów, bufor, dekoder AAC, I2S, statystyki i STOP |
| `OpenFmMetadata.h/.cpp` | Wydzielenie PID metadanych, składanie PES/ID3, odczyt tytułu/wykonawcy i diagnostyka |
| `README.md` | Dokumentacja repozytorium |
| `LICENSE` | Pełny tekst licencji kodu projektu, jeśli umieszczony w repozytorium |

## Jak działa OpenFM HLS

### 1. Token i playlisty

Dla identyfikatora, np. `OFM10`, podstawowy adres ma postać:

```text
https://stream-cdn-1.open.fm/OFM10/ngrp:standard/playlist.m3u8
```

Aplikacja pobiera czasowy URL z API:

```text
https://open.fm/api/user/token?fp=https://stream-cdn-1.open.fm/OFM10/ngrp:standard/playlist.m3u8
```

Odpowiedź JSON zawiera pole `url`. Jest to **adres tymczasowy**, którego nie należy na stałe wpisywać w kod ani publikować wraz z tokenem w logach. Szczegóły działania endpointu mogą zostać zmienione przez operatora OpenFM.

### 2. MASTER i MEDIA

Po uzyskaniu URL aplikacja:

1. pobiera playlistę **MASTER** (`#EXT-X-STREAM-INF`);
2. wybiera wskazaną w niej playlistę **MEDIA**;
3. regularnie odświeża MEDIA, aby odczytać kolejne segmenty;
4. pobiera nowe segmenty **MPEG-TS**, unikając ponownego odtwarzania już obsłużonych numerów sekwencji;
5. przekazuje strumień do bufora odtwarzania oraz analizatora metadanych.

Przy implementacji okazało się istotne prawidłowe odbieranie playlist po HTTP i obsługa odpowiedzi `chunked`. Wcześniejsza próba użycia gotowego parsera `HLSStream` nie radziła sobie z playlistą OpenFM; dlatego zarządzanie HLS jest tutaj realizowane bezpośrednio w kodzie projektu.

### 3. Dekodowanie i równoległa praca

Dane z kontenera MPEG-TS trafiają do dekodera **AAC Helix** udostępnionego przez Arduino Audio Tools. Oddzielne zadania FreeRTOS i bufor około **96 KiB** zmniejszają ryzyko przerw w dźwięku podczas pobierania segmentów HTTPS i odświeżania TFT.

W kodzie ograniczono czas oczekiwania przy operacjach bufora — pozwala to zatrzymywać i przełączać stacje bez niekończącego się oczekiwania przy pełnym buforze. Nie oznacza to jednak, że każdy błąd sieciowy zakończy się natychmiast.

## Metadane ID3v2 — wykonawca i tytuł

To jedno z najważniejszych usprawnień projektu. W odróżnieniu od starszego README, **automatyczny odczyt wykonawcy i tytułu jest już zaimplementowany i potwierdzony w praktyce**.

### Skąd pochodzą metadane?

W jednym z testów na stacji `OFM10` log wskazał:

```text
[ID3] PAT: PMT PID=0xFFF
[ID3] PMT: metadata PID=0x102 (stream_type=0x15)
[ID3] PES stream_id=0xBD header=14 payload=164 pierwsze: 49 44 33 04 ...
[ID3] Sygnatura ID3 wykryta (nr=1)
[ID3] Nowe metadane: wykonawca=Inna | tytul=Que Dolor
[ID3] Nowe metadane: wykonawca=Layton Giordani & AR/CO | tytul=She's A Devil
[ID3] TS=3531 PMT=0xFFF PID=0x102 pakiety_meta=2 PES=2 PES_bad=0 bajty_PES=265 ID3_headers=2 tagi=2
```

Widać tu **dwa poprawnie rozpoznane tagi ID3 i zero błędów PES** w pokazanym wycinku logu. Nie oznacza to jeszcze, że identyczne metadane będą nadawane przez każdą stację z katalogu.

### Jak odczytujemy ID3?

1. Analiza 188-bajtowych pakietów **MPEG-TS**.
2. Odczyt tablicy **PAT**, aby znaleźć PID tablicy **PMT**.
3. Odczyt PMT i wyszukanie strumienia metadanych oznaczonego `stream_type = 0x15`.
4. Składanie pakietów **PES** z odpowiedniego PID (nie zakładamy na stałe `0x102`).
5. Odnalezienie sygnatury **`ID3`** w ładunku PES, również gdy nie zaczyna się ona w jego pierwszym bajcie.
6. Odczyt ramek tekstowych ID3v2, w tym **`TIT2`** (tytuł) i **`TPE1`** (wykonawca), z obsługą typowych kodowań znaków.
7. Przekazanie zmian do ekranu bez niepotrzebnego pełnego odświeżania.

Inne pola ID3v2, np. `TALB` (album), `TCON` (gatunek), `TXXX` (dane dodatkowe), są możliwymi kierunkami rozbudowy. **Nie należy zakładać, że OpenFM je nadaje**, dopóki nie zostanie to potwierdzone logami.

### Komunikat „Packet ignored for PID 0x102”

```text
[W] CodecMTS.h : 308 - -> Packet ignored for PID 0x102
```

Ten komunikat może być normalny: **dekoder audio** ignoruje PID z metadanymi, podczas gdy **osobny parser ID3** go analizuje. Jeśli dźwięk działa i pojawiają się wpisy `[ID3] Nowe metadane`, samo ostrzeżenie nie oznacza awarii.

## Katalog 200 stacji

Tablica `OpenFmCatalog.cpp` zawiera **200 pozycji z nazwami i slugami** z katalogu [GETRADIO — OpenFM](https://getradio.reconv.pl/openfm). Nie są to numery od 1 do 200 przypisane wprost do `OFM1..OFM200`.

- Każda pozycja ma **numer 001–200** używany przez ekran/pilota.
- Każda pozycja ma **nazwę** i **slug** (np. `90s-dance`).
- Wybrane pozycje mają znany `OFMxxx` zapisany w tabeli; dla pozostałych program próbuje odczytać go z odpowiedzi przekierowania HTTPS.
- Odkryty numer jest **zapamiętywany w pamięci RAM na czas bieżącego uruchomienia**, a nie trwale w NVS.
- Katalog może zawierać pozycje partnerskie lub przekierowania nieobsługiwane przez tę wersję odtwarzacza — w takim przypadku nie jest uruchamiany przypadkowy `OFMxxx`.

Przykładowe wpisy w kodzie:

| Nazwa | Slug katalogowy | Znane ID w tabeli |
|---|---|---|
| 90s Dance | `90s-dance` | `OFM168` |
| Impreza | `impreza` | `OFM57` |
| Trap PL | `trap-pl` | Wyszukiwane przy wyborze |
| Weekend Chill | `weekend-chill` | Wyszukiwane przy wyborze |

**To baza 200 pozycji, a nie deklaracja 200 przetestowanych źródeł dźwięku.** Działanie mechanizmu automatycznych przekierowań należy sprawdzać na rzeczywistym urządzeniu — serwis może zmienić format odpowiedzi lub mapowanie kanałów. Zwróć uwagę, że początkowa pozycja w tej wersji katalogu to **90s Dance**, a nie `OFM10` znany z wcześniejszego testu. Do porównania z poprzednimi logami użyj komendy `o OFM10`.

## Wyświetlacz i zegar

Ekran ILI9488 pracuje w rozdzielczości **480 × 320**. Program rysuje do bufora `GFXcanvas16` (RGB565), a następnie wysyła dane do sterownika LCD jako **RGB666**.

**Kluczowe elementy wydajności:**

- `SPISettings(40000000, MSBFIRST, SPI_MODE0)` — **40 MHz**.
- Wysyłanie tylko zmodyfikowanych prostokątów (`pushCanvasRegion`) zamiast każdorazowego transferu całego obrazu.
- Osobne odświeżanie nagłówka z nazwą stacji, panelu audio/metadanych i zegara.
- Uaktualnianie zegara po zmianie sekundy, z czcionką **`DS_DIGII35pt7b`** i stałym położeniem na ekranie.
- Czas pobierany przez NTP (`configTzTime`) ze strefą `CET-1CEST,M3.5.0,M10.5.0/3`, która uwzględnia polski czas letni i zimowy.

Na ekranie znajdują się m.in.:

```text
KANAL 001/200
90s Dance
Wykonawca: [dane ID3 albo --]
Tytul:     [dane ID3 albo --]
Stan: ODTWARZANIE
VOL: 14/21

TS: ... kb/s | ... Hz | PCM 16 bit
Segmenty: ...  | Bajty seg.: ...
Kanały: 2                        HH:MM:SS
```

Jest to **schemat informacyjny**, nie identyczny zrzut ekranu. Odczyty metadanych zależą od nadawanej stacji; nie przypisuj przykładowych tytułów do konkretnego kanału.

### Kolory tekstu

Kolory `GFXcanvas16` są podawane w **RGB565**:

```cpp
canvas.setTextColor(0xFFFF); // bialy
canvas.setTextColor(0xFD20); // pomaranczowy
canvas.setTextColor(0x07E0); // zielony
canvas.setTextColor(0xFFE0); // zolty
canvas.setTextColor(0x07FF); // cyjan
```

Zmiana koloru tekstu **nie wpływa** na dekoder AAC, bufor HLS ani częstotliwość I2S.

## Ważne: prawidłowe tempo audio i I2S

Podczas testów zauważono, że muzyka na ESP32-S3 brzmi **wolniej i niżej niż w przeglądarce**. Przyczyną była rozbieżność częstotliwości:

- Dekoder AAC zgłaszał dla testowanego `OFM10`: **48 000 Hz**.
- Wyjście I2S w poprzednim kodzie było inicjalizowane na **44 100 Hz**.
- Odtwarzanie próbek 48 kHz zegarem 44,1 kHz daje około **8,1% niższe tempo** i obniżoną wysokość dźwięku.

**Poprawka potwierdzona przez użytkownika:** w pliku `OpenFmPlayer.cpp`, w konfiguracji `audioI2S`, zmiana:

```cpp
cfg.sample_rate=44100;
```

na:

```cpp
cfg.sample_rate=48000;
```

przywróciła prawidłowe tempo muzyki na testowanym kanale.

> **Istotne dla repozytorium:** archiwum `ESP32S3_OpenFM_200_Stacji_ID3_WiFi_FIX.zip` utworzone przed tym testem **nadal zawierało `cfg.sample_rate=44100`**. Jeśli publikujesz tamtą paczkę, koniecznie nanieś i zapisz poprawkę `48000` także w kodzie na GitHubie. Ten README opisuje stan po ręcznej poprawce, którą potwierdzono na urządzeniu.

**Docelowe rozwiązanie:** odczytywać częstotliwość rzeczywistego strumienia AAC i **rekonfigurować I2S automatycznie**. Nie wszystkie stacje muszą nadawać 48 kHz. Stałe `48000` jest udanym rozwiązaniem dla testowanego strumienia, a nie uniwersalną gwarancją dla całego katalogu.

**Nie myl dwóch objawów:**

- muzyka wolniejsza / niższy ton → sprawdź **sample rate / zegar I2S**;
- muzyka we właściwym tempie, ale kilka sekund później niż w przeglądarce → zwykle **opóźnienie HLS i buforowanie**.

## Środowisko i biblioteki

Projekt rozwijano w **Arduino IDE** z pakietem **Espressif ESP32 3.3.12**. Użyte środowisko autora to **ESP32-S3 N16R8**. Wymagane biblioteki i komponenty:

| Biblioteka / komponent | Wykorzystanie |
|---|---|
| [Arduino Audio Tools](https://github.com/pschatzmann/arduino-audio-tools) / wariant DevLab | I2S, dekoder MPEG-TS, obiekty audio, buforowanie |
| [arduino-libhelix](https://github.com/pschatzmann/arduino-libhelix) | Dekoder AAC Helix |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | Odczyt JSON z API tokenu |
| [WiFiManager](https://github.com/tzapu/WiFiManager) | Konfiguracja dostępu Wi-Fi |
| [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) | `GFXcanvas16` i rysowanie tekstu |
| [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) | `WiFi.h`, `HTTPClient.h`, `WiFiClientSecure.h`, `SPI.h`, NTP, FreeRTOS |
| `DS_DIGII35pt7b.h` | Niestandardowa czcionka zegara; musi być dostępna w środowisku kompilacji |

Odbiornik pilota NEC jest w tej wersji obsługiwany **bezpośrednio z użyciem przerwań** i nie wymaga osobnej biblioteki IR.

> Nazwy nagłówków i API mogą różnić się pomiędzy wersjami/forkami Audio Tools i Helix. Użyj kompatybilnych wersji instalowanych do dotychczasowego działającego projektu. Samo dołączenie `WiFi.h` jest potrzebne również w pliku `OpenFmCatalog.cpp`, nie tylko w głównym `.ino`.

## Struktura projektu

```text
ESP32S3_OpenFM_200_Stacji_ID3/
├── ESP32S3_OpenFM_200_Stacji_ID3.ino
├── OpenFmCatalog.h
├── OpenFmCatalog.cpp
├── OpenFmResolver.h
├── OpenFmResolver.cpp
├── OpenFmPlayer.h
├── OpenFmPlayer.cpp
├── OpenFmMetadata.h
├── OpenFmMetadata.cpp
├── README.md
└── LICENSE                # jeśli umieszczony w repozytorium
```

**Wszystkie zgodne wersje `.ino`, `.h` i `.cpp` muszą znajdować się w tym samym katalogu szkicu.** Nie mieszaj plików z projektu 150 stacji i projektu 200 stacji. W przeciwnym razie mogą wystąpić błędy linkera `undefined reference to OpenFmPlayer::...` lub konflikty interfejsów.

## Instalacja i uruchomienie

1. Zainstaluj **Arduino IDE** i obsługę płytek **ESP32**.
2. Wybierz moduł/ustawienia ESP32-S3 zgodne z **N16R8**, włącz obsługę PSRAM zgodnie z posiadanym modułem.
3. Zainstaluj wymienione biblioteki audio, JSON, WiFiManager i Adafruit GFX; udostępnij czcionkę `DS_DIGII35pt7b.h`.
4. Skopiuj **cały katalog projektu** wraz z `.ino`, `.h` i `.cpp` do lokalnego folderu.
5. Otwórz `ESP32S3_OpenFM_200_Stacji_ID3.ino` w Arduino IDE.
6. **Sprawdź, czy `OpenFmPlayer.cpp` zawiera prawidłowe `cfg.sample_rate=48000` dla przetestowanego `OFM10`.** Docelowe przełączanie dynamiczne jest jeszcze w planach.
7. Sprawdź, czy `OpenFmCatalog.cpp` zawiera `#include <WiFi.h>`.
8. Wgraj program do ESP32-S3 i otwórz **Serial Monitor: 115200 bodów**.
9. Jeżeli Wi-Fi nie jest skonfigurowane, połącz się z portalem **`ESP32S3-OpenFM-TEST`** i podaj dane swojej sieci.
10. Po połączeniu radio automatycznie wybiera **pozycję 1 katalogu 200 stacji**. Do sprawdzonego kanału testowego przejdź poleceniem `o OFM10`.

## Sterowanie pilotem i przez Serial

### Pilot NEC — użyte kody komend

| Kod NEC (`command`) | Działanie |
|---|---|
| `0x26` — prawo | Następna pozycja |
| `0x22` — dół | Następna pozycja |
| `0x27` — lewo | Poprzednia pozycja |
| `0x30` — góra | Poprzednia pozycja |
| `0x28` | Głośniej |
| `0x24` | Ciszej |
| `0x33` | STOP |
| `0x25` | OK — ponowne uruchomienie wybranej stacji |
| `0x29` | MUTE / wyłączenie wyciszenia |
| `0x18` | Następna pozycja (FF+) |
| `0x19` | Poprzednia pozycja (FF−) |

Jeżeli używasz innego pilota NEC, zmień kody w funkcji `handleRemote()` szkicu `.ino`.

### Monitor portu szeregowego

| Polecenie | Działanie |
|---|---|
| `g 1` do `g 200` | Wybór wskazanej pozycji z katalogu |
| `1` do `9` | Szybki wybór jednej z pierwszych dziewięciu pozycji |
| `n` / `p` | Następna / poprzednia stacja |
| `o OFM10` | Uruchom kanał pod znanym ID, bez konieczności znajdowania go w katalogu |
| `s` | STOP |
| `r` | Uruchom ponownie wybraną stację |
| `+` / `-` | Zmień głośność, zakres `0..21` |
| `m` | Stan audio i diagnostyka metadanych, pamięci RAM/PSRAM |
| `meta Artysta\|Tytul` | **Tylko test ekranu** — ręczne wpisanie informacji, nie pobiera nic z OpenFM |
| `h` | Pomoc |

## Diagnostyka i typowe problemy

### 1. Błąd kompilacji: `WiFi` / `WL_CONNECTED` nieznane

Jeśli kompilator pokazuje:

```text
OpenFmCatalog.cpp: error: 'WiFi' was not declared in this scope
OpenFmCatalog.cpp: error: 'WL_CONNECTED' was not declared in this scope
```

na początku `OpenFmCatalog.cpp` dopisz:

```cpp
#include <WiFi.h>
```

W paczce oznaczonej `WiFi_FIX` poprawka była już uwzględniona.

### 2. Błąd linkera: `undefined reference to OpenFmPlayer::...`

Najczęściej `OpenFmPlayer.cpp` nie znajduje się w kompilowanym katalogu szkicu albo pliki `.h` i `.cpp` pochodzą z **różnych wersji projektu**. Skopiuj cały, spójny komplet plików. Nie twórz pustych funkcji tylko po to, by linker zakończył pracę.

### 3. Dźwięk jest spowolniony

Sprawdź log dekodera AAC oraz wartość `cfg.sample_rate` w `OpenFmPlayer.cpp`. Dla testowanego `OFM10` problem rozwiązała zmiana **44100 → 48000**. Zbyt niska częstotliwość wyjścia I2S spowalnia i obniża dźwięk.

### 4. Widzisz PID metadanych, ale nie ma wykonawcy ani tytułu

Parser może wykryć PAT/PMT przed pierwszym tagiem ID3. Na początku strumienia przez pewien czas pola mogą pozostać puste. Szukaj logów:

```text
[ID3] PMT: metadata PID=0x... (stream_type=0x15)
[ID3] Sygnatura ID3 wykryta (...)
[ID3] Nowe metadane: wykonawca=... | tytul=...
```

Polecenie `m` pokazuje liczniki diagnostyczne. Brak metadanych na konkretnym kanale nie musi oznaczać awarii audio. Nie przypisuj PID `0x102` na sztywno — może się zmieniać.

### 5. Nie działa część pozycji z listy 200

To możliwe: **katalog zawiera nazwy, nie gwarancję odtwarzania**. Pozycja może nie udostępniać OpenFM HLS, przekierowanie GETRADIO mogło się zmienić albo token/strumień być niedostępny. Dla pewnego testu użyj `o OFM10`, a następnie sprawdź komunikaty `[KATALOG]` i `[OPENFM]`.

### 6. Wolne odświeżanie TFT

Sprawdź, czy po `spi.begin(...)` nadal wywoływana jest konfiguracja `spi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0))`. Upewnij się, że kod korzysta z aktualizacji fragmentów canvasu (`pushCanvasRegion`), a nie pełnego ekranu przy każdej sekundzie.

### 7. Zegar jest pusty lub pokazuje złą godzinę

Sprawdź połączenie Wi-Fi, dostęp do NTP i strefę czasową `CET-1CEST,M3.5.0,M10.5.0/3`. Czcionka `DS_DIGII35pt7b.h` musi być dostępna podczas kompilacji.

### 8. STOP lub zmiana stacji trwa zbyt długo

Przerwanie aktywnego pobierania HTTPS bywa opóźnione przez sieć. Projekt stosuje asynchroniczne oczekiwanie na zakończenie sesji i krótkie czasy oczekiwania bufora, ale nie gwarantuje natychmiastowego STOP w każdej sytuacji.

## Bezpieczeństwo i ograniczenia

1. **Nieoficjalne API:** OpenFM może zmienić format tokenu, adresy CDN, wymagania dostępu i ofertę stacji.
2. **TLS:** kod rozwojowy używa `WiFiClientSecure::setInsecure()`, czyli **nie weryfikuje certyfikatu serwera**. Do zastosowań produkcyjnych należy wdrożyć zaufane CA/weryfikację certyfikatów.
3. **Tokeny:** nie publikuj pełnych aktywnych URL podpisanych tokenem, logów z takimi URL ani danych uwierzytelniających do Wi-Fi.
4. **Format HLS:** wspierana ścieżka obejmuje przede wszystkim **MPEG-TS + AAC**. Nie zakładaj działania HLS z DRM, szyfrowaniem `#EXT-X-KEY`, segmentami fMP4 ani wszystkimi kodekami.
5. **200 wpisów ≠ 200 przetestowanych stacji:** szczegóły w sekcji katalogu.
6. **Znaki i nazwy:** nietypowe znaki Unicode / bardzo długie teksty mogą wymagać dalszej obsługi fontów i przewijania na TFT.
7. **Licencje plików zewnętrznych:** przed publicznym udostępnieniem sprawdź prawa do niestandardowych czcionek i kodu pochodzącego z innych projektów.
8. **Trwałość działania:** długoterminowe testy sieciowe i wszystkich kanałów nie zostały ukończone.
9. **Warunki usług:** używaj strumieni i infrastruktury OpenFM zgodnie z obowiązującymi zasadami dostawcy.

## Plan rozwoju

- [x] Odtwarzanie OpenFM HLS z tokenem, playlistami MASTER/MEDIA i AAC.
- [x] Dźwięk I2S przez PCM5102A.
- [x] TFT ILI9488 z canvasem i SPI 40 MHz.
- [x] Zegar NTP z dużą czcionką.
- [x] Sterowanie pilotem NEC i przez Serial.
- [x] Parser ID3v2 i poprawny odczyt `TIT2` / `TPE1` na `OFM10`.
- [x] Dodanie listy 200 pozycji z nazwami katalogowymi.
- [x] Poprawienie częstotliwości I2S 44,1 → 48 kHz dla sprawdzonego strumienia.
- [ ] Zweryfikowanie wszystkich 200 pozycji i filtrowanie niedziałających wpisów.
- [ ] **Automatyczne dopasowywanie częstotliwości I2S do częstotliwości AAC.**
- [ ] Zapis katalogu i rozpoznanych `OFMxxx` w pamięci nieulotnej.
- [ ] Przewijanie długich nazw stacji, wykonawców i utworów.
- [ ] Opcjonalny odczyt albumu, gatunku i dodatkowych ramek ID3 — jeżeli źródła je nadają.
- [ ] Trwałe zapamiętywanie głośności, ulubionych i ostatniej stacji.
- [ ] Weryfikacja TLS, poprawa odporności na błędy sieci i dalsze testy stabilności.
- [ ] Dalsza rozbudowa dokumentacji, testów i kompatybilności bibliotek.

## Licencja

Kod własny udostępniany w tym repozytorium jest przeznaczony do publikacji jako **wolne oprogramowanie na licencji GNU General Public License v3.0 — GPL-3.0-only**, w zakresie, w jakim jego autorzy posiadają do niego prawa.

Możesz używać, studiować, zmieniać i rozpowszechniać kod na zasadach tej licencji. Przy redystrybucji obowiązują postanowienia GPL-3.0, w tym dotyczące udostępniania odpowiedniego kodu źródłowego oraz zachowywania informacji o prawach autorskich i licencjach. Pełna treść znajduje się w pliku [`LICENSE`](LICENSE), który należy umieścić obok README.

**Ważne:** licencja tego repozytorium **nie zastępuje** licencji bibliotek, plików czcionek ani fragmentów kodu innych autorów. Jeśli w projekcie umieszczono cudze pliki, zachowaj odpowiednie informacje o ich pochodzeniu i licencji. Wersje/forki zależności należy weryfikować indywidualnie.

Oprogramowanie jest udostępniane **bez gwarancji** w zakresie dozwolonym prawem.

## Podziękowania i źródła

Projekt powstał dzięki społeczności open source, dokumentacji bibliotek oraz wspólnym eksperymentom i testom sprzętowym. Szczególne podziękowania należą się:

| Projekt / osoba | Za co dziękujemy | Repozytorium / źródło |
|---|---|---|
| **Phil Schatzmann** i współtwórcy | Arduino Audio Tools, interfejsy odtwarzania, I2S | [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools) |
| **Phil Schatzmann** i współtwórcy | Dekoder Helix dla Arduino | [arduino-libhelix](https://github.com/pschatzmann/arduino-libhelix) |
| **UNIT Electronics / DevLab** i współtwórcy | Wariant Arduino Audio Tools wykorzystany podczas prac | [DevLab fork](https://github.com/UNIT-Electronics-MX/unit_devlab_fork_arduino_audio_tools) |
| **Benoît Blanchon** | ArduinoJson | [ArduinoJson](https://github.com/bblanchon/ArduinoJson) |
| **tzapu / tablatronix** i współtwórcy | WiFiManager | [WiFiManager](https://github.com/tzapu/WiFiManager) |
| **Limor „Ladyada” Fried**, Adafruit i społeczność | Adafruit GFX / canvas | [Adafruit-GFX-Library](https://github.com/adafruit/Adafruit-GFX-Library) |
| **Espressif Systems** i społeczność | ESP32 Arduino Core, Wi-Fi, FreeRTOS, sterowniki | [arduino-esp32](https://github.com/espressif/arduino-esp32) |
| **GETRADIO** | Informacje katalogowe łączące nazwy kanałów i adresy | [getradio.reconv.pl/openfm](https://getradio.reconv.pl/openfm) |
| **GorlikItsMe** | Publicznie opisany sposób uzyskiwania tokenu OpenFM — punkt odniesienia | [Gist OpenFM](https://gist.github.com/GorlikItsMe/4c90cba803c3ec097dbcb5ce44e65bef) |

**Osobne podziękowania dla autora rozwiązania opisanego na forum Elektroda**, który zwrócił uwagę na obecność **ID3v2 w osobnym PID kontenera MPEG-TS** i opisał drogę PAT/PMT → PES → ID3 → tytuł. To naprowadziło rozwój tego projektu na bezpośrednie odczytywanie metadanych w ESP32-S3 — bez dodatkowego konwertera HLS/ICY i bez przerabiania strumienia UART.

- [Wpis i opis rozwiązania HLS/ID3v2 na Elektrodzie](https://www.elektroda.pl/rtvforum/viewtopic.php?p=22000329#22000329)
- [Artykuł Phila Schatzmanna o HLS w Arduino Audio Tools](https://www.pschatzmann.ch/home/2025/04/09/http-live-streaming-hls-with-the-arduino-audio-tools/)

Dziękujemy również autorom standardów **HLS, MPEG-TS, AAC, ID3**, wszystkim współtwórcom wymienionych bibliotek, społeczności ESP32/Arduino i osobom pomagającym w testach. Kod odtwarzacza był rozwijany iteracyjnie z pomocą **ChatGPT**, przy weryfikacji rozwiązań na rzeczywistym sprzęcie.

### Znaki towarowe i brak powiązań z OpenFM

**OpenFM** i nazwy stacji należą do odpowiednich właścicieli. Projekt **nie jest afiliowany, sponsorowany ani zatwierdzony przez OpenFM**. Nazwy stacji i adresy serwisów zewnętrznych służą wyłącznie identyfikacji interoperacyjnych źródeł internetowych.

---

Jeżeli projekt jest przydatny, możesz oznaczyć repozytorium ⭐, zgłosić problem w **Issues** lub zaproponować poprawkę przez **Pull Request**. Mile widziane są szczególnie testy nowych stacji, logi metadanych ID3 i poprawki automatycznej konfiguracji sample rate.
