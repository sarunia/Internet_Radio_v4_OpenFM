# OpenFM HLS Radio — ESP32-S3 + ILI9488 + PCM5102A

Eksperymentalny odtwarzacz internetowych kanałów **OpenFM** dla modułu **ESP32-S3 N16R8** (16 MB Flash, 8 MB PSRAM), z kolorowym wyświetlaczem **ILI9488 480×320**, wyjściem audio **I2S / PCM5102A** i sterowaniem pilotem **IR NEC**.

Projekt powstał jako samodzielny test odtwarzania OpenFM — **nie jest pełną wersją wcześniejszego radia z kartą SD, pogodą i kalendarzem**. W testach sprzętowych udało się uruchomić dźwięk z OpenFM, pobierać aktualne playlisty oraz przełączać co najmniej część kanałów. Kolejne rozszerzenia, zwłaszcza lista 150 pozycji, wymagają dalszej weryfikacji.

> **Stan projektu:** wersja rozwojowa / testowa. Nie jest to oficjalny projekt OpenFM ani jego autoryzowana integracja.

## Co już działa

- Pobieranie aktualnego adresu strumienia z nieoficjalnie używanego endpointu `https://open.fm/api/user/token`.
- Obsługa playlist **HLS** (`.m3u8`): przejście z playlisty **MASTER** do **MEDIA** i cykliczny odczyt segmentów.
- Odtwarzanie audio **AAC w kontenerze MPEG-TS** z użyciem Arduino Audio Tools, dekodera Helix i wyjścia I2S.
- Buforowanie danych (96 KB) i zadania **FreeRTOS** rozdzielające pobieranie danych od dekodowania.
- Wyświetlanie numeru kanału, stanu odtwarzania, głośności, parametrów audio i liczników segmentów.
- Wyświetlacz ILI9488 sterowany przez **GFXcanvas16**, z interfejsem SPI ustawionym na **40 MHz** i aktualizacją wybranych obszarów ekranu.
- Zegar NTP w prawym dolnym rogu; polska strefa czasu z automatycznym przełączaniem **CET/CEST**. Używana jest czcionka `DS_DIGII35pt7b`.
- Sterowanie pilotem IR oraz poleceniami z monitora portu szeregowego.
- Automatyczny wybór pozycji nr 1 po połączeniu z Wi-Fi.

## Ważna informacja o 150 kanałach

Projekt udostępnia **150 numerowanych pozycji na ekranie**, ale **nie zawiera zweryfikowanego katalogu 150 rzeczywistych kanałów OpenFM**.

Cztery pierwsze pozycje używają identyfikatorów przetestowanych podczas rozwoju:

| Pozycja | Identyfikator OpenFM |
|---:|---|
| 001 | `OFM10` |
| 002 | `OFM31` |
| 003 | `OFM3` |
| 004 | `OFM14` |

Pozostałe pozycje są mapowane na **kandydackie identyfikatory** `OFM1`–`OFM150`, z pominięciem identyfikatorów już wykorzystanych na czterech pierwszych pozycjach. **Numer pozycji na ekranie nie musi odpowiadać numerowi `OFM...`.** Nie każdy kandydacki identyfikator musi istnieć lub udostępniać działający strumień.

Dla pewnego, ręcznie wybranego ID użyj komendy np. `o OFM10`.

## Jak działa odtwarzanie OpenFM

```text
Wybór numeru kanału / identyfikatora OFM
                   |
                   v
OpenFmResolver: zapytanie do open.fm/api/user/token
                   |
                   v
Aktualny adres HLS z tokenem (JSON: pole "url")
                   |
                   v
Pobranie playlisty MASTER (.m3u8)
                   |
                   v
Wybór wariantu i pobranie playlisty MEDIA
                   |
                   v
Pobieranie kolejnych segmentów MPEG-TS
                   |
                   v
          Bufor 96 KB / FreeRTOS
                   |
                   v
        MPEG-TS -> AAC / Helix
                   |
                   v
        Regulacja głośności -> I2S
                   |
                   v
               PCM5102A
```

**Dlaczego własna obsługa HLS?** Wcześniejsze próby bezpośredniego otwarcia playlist OpenFM w `HLSStream` (DevLab / Arduino Audio Tools) powodowały awarię parsera na playliście MASTER. Ten projekt sam pobiera MASTER, MEDIA i segmenty, a do dekodowania przekazuje strumień MPEG-TS. Nie wymaga `ESP32-audioI2S` do odtwarzania OpenFM.

Adresy zawierają czasowe tokeny — **nie należy zapisywać ich jako stałych linków ani publikować w logach**. Program pobiera aktualny adres przy uruchomieniu kanału.

## Sprzęt i podłączenie

| ESP32-S3 | Urządzenie | Sygnał |
|---|---|---|
| GPIO 16 | PCM5102A | BCLK / BCK |
| GPIO 17 | PCM5102A | DIN / DATA |
| GPIO 18 | PCM5102A | LRCK / LCK / WS |
| GPIO 15 | Odbiornik IR | OUT |
| GPIO 11 | ILI9488 | SCLK |
| GPIO 12 | ILI9488 | MOSI |
| GPIO 13 | ILI9488 | MISO |
| GPIO 5 | ILI9488 | CS |
| GPIO 4 | ILI9488 | DC |

Połącz również **masę GND** modułów; zasilanie i pozostałe sygnały wyświetlacza dopasuj do swojego modułu. Rozpiska dotyczy przetestowanego układu autora — nie jest uniwersalnym pinoutem każdego wyświetlacza ILI9488.

## Środowisko i zależności

Projekt jest przygotowany dla **Arduino IDE** i platformy **Espressif ESP32**; podczas rozwoju używano pakietu ESP32 **3.3.12**. Potrzebne są:

- **Arduino Audio Tools** (wariant zawierający `AudioTools.h`, `CodecHelix.h`, `CodecMTS.h` i klasy FreeRTOS);
- **arduino-libhelix** — dekoder AAC;
- **ArduinoJson 7.x** — interpretacja JSON z tokenem;
- **WiFiManager** — konfiguracja Wi-Fi;
- **Adafruit GFX Library** — `GFXcanvas16`;
- **IR receiver**: w aktualnym szkicu dekodowanie NEC jest realizowane bezpośrednio przez przerwanie GPIO;
- **`DS_DIGII35pt7b.h`** — niestandardowa czcionka zegara, potrzebna w ścieżce include.

`WiFi.h`, `HTTPClient.h`, `WiFiClientSecure.h`, `SPI.h` i obsługa FreeRTOS pochodzą z platformy ESP32. Zwróć uwagę na zgodność wersji Arduino Audio Tools i libhelix — interfejsy tych bibliotek mogą się zmieniać.

## Struktura plików

```text
ESP32S3_OpenFM_150_Kanalow/
├── ESP32S3_OpenFM_150_Kanalow.ino  # Wi-Fi, pilot IR, menu, ekran, zegar
├── OpenFmResolver.h                # Interfejs pobierania adresu OpenFM
├── OpenFmResolver.cpp              # Zapytanie API i odczyt tokenu z JSON
├── OpenFmPlayer.h                  # Interfejs odtwarzacza i liczników
├── OpenFmPlayer.cpp                # HLS, MPEG-TS/AAC, bufor, I2S, FreeRTOS
└── README.md
```

**Wszystkie pliki `.ino`, `.cpp` i `.h` muszą znajdować się w tym samym folderze szkicu.** Samo skopiowanie `.ino` bez zgodnych plików `.cpp` powoduje błędy linkera (`undefined reference to OpenFmPlayer::...`).

## Instalacja i uruchomienie

1. Zainstaluj pakiet płytek ESP32 oraz wymienione biblioteki.
2. Sprawdź, czy dostępny jest plik czcionki `DS_DIGII35pt7b.h`.
3. Umieść wszystkie pliki projektu w jednym katalogu i otwórz plik `.ino` w Arduino IDE.
4. Wybierz odpowiednią płytkę ESP32-S3 oraz ustawienia pamięci zgodne z modułem N16R8, w tym PSRAM.
5. Skompiluj i wgraj program.
6. Otwórz **Serial Monitor: 115200 baud**.
7. Przy pierwszym uruchomieniu skonfiguruj Wi-Fi przez portal **`ESP32S3-OpenFM-TEST`**, jeżeli nie ma zapisanej sieci.
8. Po połączeniu z Wi-Fi radio automatycznie spróbuje uruchomić kanał na pozycji **001 (`OFM10`)**.

## Sterowanie

| Polecenie Serial | Działanie |
|---|---|
| `g 1` … `g 150` | Wybór numerowanej pozycji |
| `1` … `9` | Szybki wybór pierwszych dziewięciu pozycji |
| `n` / `p` | Następna / poprzednia pozycja |
| `o OFM10` | Wybór kanału przez ID, niezależnie od listy |
| `s` | STOP |
| `r` | Ponowne uruchomienie aktualnego kanału |
| `+` / `-` | Zmiana głośności |
| `m` | Stan odtwarzacza, RAM i PSRAM |
| `h` | Pomoc |
| `meta Queen\|Radio Ga Ga` | **Wyłącznie test ekranu**: ręczne ustawienie wykonawcy i tytułu |

Pilot IR (NEC): strzałki przełączają stacje, przyciski głośności zmieniają poziom, a OK / STOP / MUTE sterują odtwarzaniem zgodnie z aktualnym mapowaniem w szkicu. Jeżeli Twój pilot wysyła inne kody, trzeba zmienić mapowanie w pliku `.ino`.

## Co jest wyświetlane na ekranie?

- Numer pozycji w formacie **`001/150`**;
- aktualny identyfikator kanału `OFM...`;
- stan odtwarzania i poziom głośności;
- przepływność **TS w kb/s** oszacowaną na podstawie rozmiaru i czasu segmentu — **nie jest to gwarantowany bitrate samego AAC**;
- częstotliwość próbkowania, kanały i liczbę bitów PCM raportowane przez dekoder;
- licznik pobranych segmentów oraz rozmiar **ostatniego kompletnego segmentu MPEG-TS** w bajtach;
- zegar NTP z sekundami, w prawym dolnym rogu.

Canvas pozwala aktualizować zmienione obszary ekranu bez niepotrzebnego przerysowywania całości. W wersji testowej kluczowe dla szybkości odświeżania okazało się ustawienie **SPI 40 MHz**.

## Wykonawca i tytuł utworu

Pola **Wykonawca** oraz **Tytuł** są przygotowane na ekranie, ale **nie są jeszcze automatycznie pobierane z OpenFM**. Sprawdzony endpoint `api/user/token` zwraca adres odtwarzania; nie potwierdzono w nim metadanych utworu. Komenda `meta` służy tylko do testowania wyglądu UI.

Możliwe kierunki rozwoju to osobne, wiarygodne źródło aktualnie odtwarzanego utworu OpenFM lub analiza ewentualnych metadanych w strumieniu MPEG-TS. Nie zostało to jeszcze zaimplementowane.

## Znane ograniczenia i bezpieczeństwo

- **150 pozycji ≠ 150 zweryfikowanych stacji.** Lista w aktualnym szkicu zawiera identyfikatory testowe i kandydackie.
- **API OpenFM nie jest udokumentowanym, stabilnym kontraktem dla tego projektu.** Może zmienić odpowiedź, wymagania dostępu lub zasady użycia.
- Kod testowy używa `WiFiClientSecure::setInsecure()` — **nie sprawdza certyfikatu TLS**. Przed produkcyjnym użyciem należy zastosować prawidłową weryfikację certyfikatu / CA.
- Odtwarzacz jest przygotowany przede wszystkim dla nieszyfrowanych segmentów **MPEG-TS z AAC**. Playlisty wymagające `#EXT-X-KEY`, `#EXT-X-MAP`, fMP4 albo innego kodeka nie są obsługiwane przez tę ścieżkę.
- Nie wszystkie serwery/odpowiedzi HTTP są wspierane. Dla segmentów bez `Content-Length` lub z przesyłaniem `chunked` obecny kod może odrzucić transfer.
- Zmiana kanału wymaga zakończenia bieżącej sesji; przy zablokowanym połączeniu HTTPS STOP może być opóźniony.
- Działanie na sprzęcie zostało potwierdzone podczas testów, ale stabilność długoterminowa i zgodność wszystkich kanałów nie są zagwarantowane.
- Przed publikacją repozytorium usuń własne hasła, klucze, zrzuty adresów z aktywnymi tokenami i inne dane prywatne.

## Plan dalszego rozwoju

- [ ] Pozyskanie i weryfikacja faktycznego katalogu kanałów OpenFM.
- [ ] Automatyczne metadane: wykonawca i tytuł utworu.
- [ ] Dalsze testy stabilności i responsywności przełączania stacji.
- [ ] Obsługa większej liczby formatów segmentów i odpowiedzi HTTP.
- [ ] Weryfikacja certyfikatów TLS.
- [ ] Trwały zapis ulubionych kanałów / ostatniej stacji.

## Licencja open source — GPL-3.0

**Ten projekt jest udostępniany jako wolne oprogramowanie na warunkach GNU General Public License, wersja 3.0 (GPL-3.0-only)** — w zakresie kodu, do którego autorzy repozytorium posiadają prawa autorskie i mogą go licencjonować.

Możesz bezpłatnie **używać, analizować, modyfikować i rozpowszechniać** kod na warunkach GPL-3.0. W przypadku rozpowszechniania zmienionych wersji należy m.in. udostępnić odpowiedni kod źródłowy na zasadach tej licencji, zachować informacje o prawach autorskich i licencji oraz wskazać istotne zmiany. Pełna treść licencji znajduje się w pliku [`LICENSE`](LICENSE).

Projekt udostępniany jest **bez gwarancji**, w zakresie dopuszczonym przez obowiązujące prawo. Jest to niezależny projekt hobbystyczny, nieoficjalny produkt OpenFM.

> **Ważne:** licencja tego repozytorium nie zmienia licencji bibliotek zewnętrznych. Kod i zasoby innych autorów (w tym ewentualnie dołączone czcionki) pozostają objęte ich oryginalnymi warunkami. Przed publikacją upewnij się, że masz prawo rozpowszechniać wszystkie pliki znajdujące się w repozytorium. Jeżeli wykorzystujesz kod przejęty z innego projektu, zachowaj jego oryginalne informacje o prawach autorskich i licencji.

### Podziękowania dla autorów i społeczności open source

Projekt powstał dzięki pracy autorów bibliotek i narzędzi, bez których realizacja odtwarzacza na ESP32-S3 byłaby znacznie trudniejsza. Szczególne podziękowania dla:

| Projekt | Autorzy / opiekunowie | Wykorzystanie | Licencja biblioteki |
|---|---|---|---|
| [Arduino Audio Tools](https://github.com/pschatzmann/arduino-audio-tools) | **Phil Schatzmann** i współtwórcy | Interfejsy audio, I2S, dekodowanie i buforowanie | GPL-3.0 |
| [arduino-libhelix](https://github.com/pschatzmann/arduino-libhelix) | **Phil Schatzmann** i współtwórcy | Dekodowanie AAC/MP3 | Zweryfikuj licencję zainstalowanej wersji/forka; wariant DevLab: GPL-3.0 |
| [DevLab Audio Tools](https://github.com/UNIT-Electronics-MX/unit_devlab_fork_arduino_audio_tools) | **UNIT Electronics**, Cesar Bautista, Adrián Rabadán i współtwórcy | Wariant Audio Tools używany podczas testów | GPL-3.0 |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | **Benoît Blanchon** | Odczyt JSON z API OpenFM | MIT |
| [WiFiManager](https://github.com/tzapu/WiFiManager) | **tzapu**, **tablatronix** i współtwórcy | Konfiguracja sieci Wi-Fi | MIT |
| [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) | **Limor „Ladyada” Fried**, Adafruit Industries i współtwórcy | Rysowanie grafiki i `GFXcanvas16` | BSD |
| [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) | **Espressif Systems** i społeczność | Wi-Fi, SPI, FreeRTOS, NTP, GPIO | Zgodnie z licencjami komponentów platformy |

Dodatkowe podziękowania dla **Phila Schatzmanna** za artykuł [*HTTP Live Streaming (HLS) with the Arduino Audio Tools*](https://www.pschatzmann.ch/home/2025/04/09/http-live-streaming-hls-with-the-arduino-audio-tools/), który był ważną inspiracją podczas rozwoju obsługi HLS.

Dziękuję również społeczności Arduino i ESP32 za dokumentację, przykłady oraz publiczne projekty umożliwiające eksperymentowanie i dzielenie się rozwiązaniami.

### Zastrzeżenia dotyczące OpenFM

**OpenFM** oraz powiązane nazwy i znaki towarowe należą do ich właścicieli. Ten projekt **nie jest powiązany, sponsorowany ani zatwierdzony przez OpenFM**. Dostępność strumieni, tokenów i metadanych zależy od usługodawcy; korzystaj z nich zgodnie z obowiązującymi warunkami.

---

**Jeżeli projekt Ci się przydał, możesz oznaczyć repozytorium gwiazdką ⭐, zgłosić błąd lub zaproponować poprawkę. Współpraca i rozwój na otwartym kodzie są mile widziane!**
