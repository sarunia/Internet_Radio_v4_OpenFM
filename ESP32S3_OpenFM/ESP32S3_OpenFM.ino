/*=============================================================================
  PROJEKT : OPENFM HLS INTERNET RADIO - ESP32-S3
  PLIK    : ESP32S3_OpenFM.ino
  ROLA    : Główny szkic Arduino - inicjalizacja, sterowanie, pilot i ekran TFT

  OPIS PROJEKTU
  -----------------------------------------------------------------------------
  Odtwarzacz radia internetowego OpenFM dla modułu ESP32-S3 N16R8. Szkic
  odpowiada za uruchomienie urządzenia, konfigurację Wi-Fi, odbiór pilota NEC,
  obsługę poleceń z portu szeregowego oraz prezentację informacji na ekranie
  ILI9488 480x320. Właściwy silnik pobierania HLS, dekodowanie AAC i wyjście
  I2S znajdują się w osobnym module OpenFmPlayer. Metadane utworów ID3v2
  analizuje OpenFmMetadata, a nazwy i identyfikatory kanałów udostępnia
  OpenFmCatalog.

  TOR AUDIO (moduł OpenFmPlayer, nie ten plik)
  -----------------------------------------------------------------------------
  OpenFM API / token -> playlista HLS MASTER -> MEDIA -> segmenty MPEG-TS
  -> MTSDecoder + AACDecoderHelix -> VolumeStream -> I2S -> PCM5102A.
  Częstotliwość próbkowania jest rozpoznawana na podstawie danych dekodera AAC;
  konfiguracja I2S powinna odpowiadać rzeczywistemu sample rate dekodera.

  SPRZĘT I POŁĄCZENIA
  -----------------------------------------------------------------------------
  Moduł       : ESP32-S3 N16R8 (16 MB Flash, 8 MB PSRAM - zgodnie z konfiguracją)
  Wyświetlacz : ILI9488 IPS, 480 x 320, magistrala SPI / format RGB666 na TFT
  Pilot       : odbiornik IR NEC, GPIO15 (dekodowanie programowe przez przerwanie)
  DAC         : PCM5102A, wyjście I2S (piny BCLK/LRCK/DATA konfigurowane w OpenFmPlayer)
  SPI TFT     : SCK=GPIO11, MOSI=GPIO12, MISO=GPIO13, CS=GPIO5, DC=GPIO4
  Zegar       : DS_DIGII35pt7b - niestandardowa czcionka dostarczona osobno

  ŚRODOWISKO I BIBLIOTEKI
  -----------------------------------------------------------------------------
  Platforma   : Arduino IDE + pakiet płytek ESP32 firmy Espressif
  ESP32 core  : 3.3.12 (wersja widoczna w przekazanym logu kompilacji)
  Arduino IDE : numer wersji nie jest zapisany w szkicu - uzupełnić wg instalacji
  WiFiManager : biblioteka WiFiManager (wersja nieustalona na podstawie załączonych plików)
  Adafruit GFX: biblioteka Adafruit GFX Library (wersja nieustalona)
  SPI         : biblioteka SPI dołączana z pakietem ESP32 Arduino core
  Font        : DS_DIGII35pt7b.h - plik projektu, nie standardowa czcionka GFX
  Moduły      : OpenFmPlayer, OpenFmResolver, OpenFmMetadata, OpenFmCatalog
  AudioTools / CodecHelix / CodecMTS są wykorzystywane w OpenFmPlayer.cpp.
  Ich dokładnych wersji nie da się ustalić z samego pliku INO. Przed publikacją
  należy wpisać numery wersji z Arduino IDE -> Library Manager / plików biblioteki.

  PAMIĘĆ I WYDAJNOŚĆ
  -----------------------------------------------------------------------------
  GFXcanvas16 przechowuje obraz w formacie RGB565 (16 bit/piksel). Dla 480x320
  bufor wymaga ok. 307 200 bajtów. Wysyłanie fragmentów ekranu ogranicza czas
  zajęcia SPI i pozwala pętli głównej szybko obsługiwać pilot oraz Serial.
  Wysyłanie do ILI9488 odbywa się z zegarem SPI 40 MHz.

  OBSŁUGA UŻYTKOWNIKA
  -----------------------------------------------------------------------------
  Port szeregowy : 115200 bit/s
  Komendy        : g 1..200, o OFM10, n, p, s, r, +, -, m, h
  Test ekranu    : meta wykonawca|tytuł (dane ręczne, tylko do testu)
  Pilot NEC      : góra/dół lista, OK zatwierdza, lewo/prawo zmienia stację, VOL, STOP, MUTE

  BEZPIECZEŃSTWO I OGRANICZENIA
  -----------------------------------------------------------------------------
  W modułach sieciowych występuje WiFiClientSecure::setInsecure(). Oznacza to,
  że certyfikat TLS serwera nie jest weryfikowany. To ustawienie ułatwia testy,
  ale nie jest zalecane w urządzeniu produkcyjnym. Tokenów HLS nie należy
  wypisywać do logów ani umieszczać na stałe w kodzie lub repozytorium.

  WAŻNE
  -----------------------------------------------------------------------------
  Ten plik jest głównym szkicem i korzysta z pozostałych plików projektu. Nie
  jest samodzielny: do kompilacji muszą być obecne odpowiednie pliki .h/.cpp,
  w tym OpenFmPlayer, OpenFmResolver, OpenFmMetadata i OpenFmCatalog oraz font.

  Wersja dokumentacji: 1.0
  Data opracowania     : 2026-11-08
=============================================================================*/
// Podstawowe API Arduino: GPIO, czas, Serial, typy i funkcje pomocnicze.
#include <Arduino.h>
// Sterownik Wi-Fi ESP32; wymagany do połączenia z siecią lokalną.
#include <WiFi.h>
// Portal konfiguracyjny Wi-Fi - zapis i ponowne wykorzystanie danych sieci.
#include <WiFiManager.h>
// Sprzętowa magistrala SPI używana do komunikacji z kontrolerem ILI9488.
#include <SPI.h>
// Biblioteka grafiki: GFXcanvas16, tekst, linie i prostokaty.
#include <Adafruit_GFX.h>
#include "OpenFmPolishText.h"  // Własne polskie glify i dekoder UTF-8.
// Własna czcionka cyfrowa zegara - musi być dostępna jako osobny plik nagłówkowy.
#include <DS_DIGII35pt7b.h>
// Funkcje systemowe ESP-IDF, m.in. identyfikacja przyczyny resetu.
#include <esp_system.h>
// Interfejs silnika HLS/AAC/I2S - implementacja znajduje się w OpenFmPlayer.cpp.
#include "OpenFmPlayer.h"
// Interfejs parsera ID3v2 wydzielającego wykonawcę i tytuł z MPEG-TS.
#include "OpenFmMetadata.h"
// Interfejs katalogu nazw stacji, slugów i identyfikatorów OFM.
#include "OpenFmCatalog.h"

// ============================================================================
// KODY PRZYCISKOW PILOTA IR NEC
// ============================================================================
#define rcCmdVolumeUp     0x0028   // VOL+
#define rcCmdVolumeDown   0x0024   // VOL-
#define rcCmdArrowRight   0x0026   // W prawo - nastepna stacja
#define rcCmdArrowLeft    0x0027   // W lewo - poprzednia stacja
#define rcCmdArrowUp      0x0030   // W gore - lista stacji, pozycja wyzej
#define rcCmdArrowDown    0x0022   // W dol - lista stacji, pozycja nizej
#define rcCmdOk           0x0025   // OK - zatwierdzenie
#define rcCmdMode         0x0020   // SOURCE
#define rcCmdMute         0x0029   // MUTE
#define rcCmdKey0         0x0012   // 0
#define rcCmdKey1         0x0015   // 1
#define rcCmdKey2         0x0014   // 2
#define rcCmdKey3         0x0008   // 3
#define rcCmdKey4         0x0011   // 4
#define rcCmdKey5         0x0010   // 5
#define rcCmdKey6         0x0009   // 6
#define rcCmdKey7         0x0007   // 7
#define rcCmdKey8         0x0006   // 8
#define rcCmdKey9         0x0005   // 9
#define rcCmdBankUp       0x0018   // FF+
#define rcCmdBankDown     0x0019   // FF-
#define rcCmdPauseResume  0x0032   // PLAY/PAUSE
#define rcCmdFolderList   0x0036   // GOTO
#define rcCmdStop         0x0033   // STOP
#define rcCmdProg         0x0023   // PROG
#define rcCmdMemory       0x0016   // MEMORY
#define rcCmdRandom       0x001A   // RANDOM
#define rcCmdSearch       0x0013   // SEARCH

// ============================================================================
// PALETA KOLORÓW RGB565 — ILI9488 / Adafruit GFX
// ============================================================================
// Zmieniaj wartości po prawej stronie, aby zmienić wygląd całego radia.
// RGB565: 5 bitów czerwieni, 6 zieleni i 5 niebieskiego.
// Stałe dotyczą wyłącznie kolorów grafiki; kody pilota i polecenia TFT są osobne.
namespace Kolory {
  static constexpr uint16_t CZARNY                    = 0x0000;
  static constexpr uint16_t BIALY                     = 0xFFFF;
  static constexpr uint16_t CYJAN                     = 0x07FF;
  static constexpr uint16_t ZIELONY                   = 0x07E0;
  static constexpr uint16_t ZOLTY                     = 0xFFE0;
  static constexpr uint16_t CZERWONY                  = 0xF800;
  static constexpr uint16_t MAGENTA                   = 0xF81F;
  static constexpr uint16_t ZLOTY                     = 0xFEA0;
  static constexpr uint16_t POMARANCZOWY_JASNY        = 0xFD20;
  static constexpr uint16_t JASNY_SZARONIEBIESKI      = 0xBDF7;
  static constexpr uint16_t SZARY_CIEMNY              = 0x39E7;
  static constexpr uint16_t TLO_ZAZNACZENIA           = 0x18C3;
  static constexpr uint16_t ZIELONO_TURKUSOWY         = 0x07EF;

  // Dodatkowe kolory do wykorzystania w kolejnych zmianach wyglądu:
  static constexpr uint16_t NIEBIESKI                 = 0x001F;
  static constexpr uint16_t GRANATOWY                 = 0x000F;
  static constexpr uint16_t NIEBIESKI_JASNY           = 0x05FF;
  static constexpr uint16_t BLEKITNY                  = 0xAEDC;
  static constexpr uint16_t TURKUSOWY_JASNY           = 0x87FF;
  static constexpr uint16_t MIETOWY                   = 0x87F0;
  static constexpr uint16_t ZIELONY_JASNY             = 0x9FE5;
  static constexpr uint16_t ZIELONY_CIEMNY            = 0x03E0;
  static constexpr uint16_t LIMONKOWY                 = 0xAFE5;
  static constexpr uint16_t ZOLTY_JASNY               = 0xFFEF;
  static constexpr uint16_t ZOLTY_PASTELOWY           = 0xFFB3;
  static constexpr uint16_t POMARANCZOWY              = 0xFC00;
  static constexpr uint16_t BURSZTYNOWY               = 0xFD00;
  static constexpr uint16_t KORALOWY                  = 0xFD10;
  static constexpr uint16_t LOSOSIOWY                 = 0xFC0E;
  static constexpr uint16_t ROZOWY                    = 0xFB56;
  static constexpr uint16_t ROZOWY_JASNY              = 0xFC1F;
  static constexpr uint16_t FUKSJA                    = 0xF81F;
  static constexpr uint16_t FIOLETOWY                 = 0x801F;
  static constexpr uint16_t FIOLETOWY_JASNY           = 0xD5BF;
  static constexpr uint16_t LAWENDOWY                 = 0xC59F;
  static constexpr uint16_t PURPUROWY                 = 0x780F;
  static constexpr uint16_t CYJAN_JASNY               = 0xA7FF;
  static constexpr uint16_t BLEKIT_LODOWY             = 0xE7FF;
  static constexpr uint16_t SZARY                     = 0x8410;
  static constexpr uint16_t SZARY_JASNY               = 0xC618;
  static constexpr uint16_t SZARY_BARDZO_JASNY        = 0xE71C;
  static constexpr uint16_t BIALY_CIEPLY              = 0xFFDE;
  static constexpr uint16_t BIALY_ZIMNY               = 0xEFFF;
  static constexpr uint16_t MORSKI                    = 0x04B0;
  static constexpr uint16_t OLIWKOWY                  = 0x8400;
  static constexpr uint16_t BORDOWY                   = 0x8000;
  static constexpr uint16_t BRAZOWY                   = 0xA145;
  static constexpr uint16_t PIASKOWY                  = 0xD6B3;
  static constexpr uint16_t KREMOWY                   = 0xFFDC;
  static constexpr uint16_t MIETOWA_BIEL              = 0xF7FE;
}
// ============================================================================


// ---------------------------------------------------------------------------
// KONFIGURACJA SPRZĘTOWA - piny zgodne z bazowym projektem radia.
// Zmiana połączeń wymaga aktualizacji poniższych stałych oraz konfiguracji I2S
// w OpenFmPlayer.cpp. Nie zmieniać pinów bez sprawdzenia połączeń płytki.
// ---------------------------------------------------------------------------
static constexpr int PIN_TFT_CS=5, PIN_TFT_DC=4;             // CS i D/C wyświetlacza.
static constexpr int PIN_TFT_SCK=11, PIN_TFT_MISO=13, PIN_TFT_MOSI=12; // Linie SPI TFT.
static constexpr int PIN_IR=15;                              // Wyjście odbiornika podczerwieni.
static constexpr uint16_t WIDTH=480, HEIGHT=320;            // Rozdzielczość panelu ILI9488.
SPIClass spi(HSPI);                                       // Osobny obiekt magistrali SPI ESP32.
GFXcanvas16 canvas(WIDTH,HEIGHT);                         // Bufor obrazu RGB565 w pamięci RAM/PSRAM.

// Wysyła pojedynczy bajt polecenia kontrolera ILI9488 (D/C=LOW).
static void cmd(uint8_t c) {digitalWrite(PIN_TFT_DC,LOW);digitalWrite(PIN_TFT_CS,LOW);spi.transfer(c);digitalWrite(PIN_TFT_CS,HIGH);}
// Wysyła pojedynczy bajt parametru polecenia ILI9488 (D/C=HIGH).
static void dataByte(uint8_t d) {digitalWrite(PIN_TFT_DC,HIGH);digitalWrite(PIN_TFT_CS,LOW);spi.transfer(d);digitalWrite(PIN_TFT_CS,HIGH);}
// Ustawia prostokąt pamięci GRAM kontrolera: kolumny, wiersze i start zapisu RAM.
static void window(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1) {
  cmd(0x2a); digitalWrite(PIN_TFT_DC,HIGH);digitalWrite(PIN_TFT_CS,LOW);
  spi.transfer(x0>>8);spi.transfer(x0);spi.transfer(x1>>8);spi.transfer(x1);digitalWrite(PIN_TFT_CS,HIGH);
  cmd(0x2b);digitalWrite(PIN_TFT_DC,HIGH);digitalWrite(PIN_TFT_CS,LOW);
  spi.transfer(y0>>8);spi.transfer(y0);spi.transfer(y1>>8);spi.transfer(y1);digitalWrite(PIN_TFT_CS,HIGH);
  cmd(0x2c);
}
// Inicjalizacja GPIO, SPI 40 MHz i rejestrów kontrolera ILI9488.
// Sekwencja wartości rejestrów jest dopasowana do konkretnego modułu IPS.
static void tftBegin(){
  pinMode(PIN_TFT_CS,OUTPUT);pinMode(PIN_TFT_DC,OUTPUT);
  digitalWrite(PIN_TFT_CS,HIGH);
  spi.begin(PIN_TFT_SCK,PIN_TFT_MISO,PIN_TFT_MOSI,-1);
  // Oryginalne radio ustawiało 40 MHz. Bez beginTransaction() SPI działa
  // że znacznie niższym zegarem i odświeżanie canvasu jest bardzo wolne.
  spi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
  cmd(0x01);delay(120);
  cmd(0xc0);dataByte(0x17);dataByte(0x15);
  cmd(0xc1);dataByte(0x41);
  cmd(0xc5);dataByte(0x00);dataByte(0x12);dataByte(0x80);
  cmd(0x36);dataByte(0x28);
  cmd(0x21); // IPS inversion - jak w dotychczasowym projekcie
  cmd(0x3a);dataByte(0x66); // RGB666
  cmd(0xb0);dataByte(0x00);
  cmd(0xb1);dataByte(0x50);
  cmd(0xb4);dataByte(0x02);
  cmd(0xb6);dataByte(0x02);dataByte(0x02);dataByte(0x3b);
  cmd(0xb7);dataByte(0xc6);
  cmd(0xf7);dataByte(0xa9);dataByte(0x51);dataByte(0x2c);dataByte(0x82);
  cmd(0x11);delay(120);cmd(0x13);cmd(0x29);delay(100);
  if(!canvas.getBuffer()) Serial.println("[TFT] Uwaga: brak RAM canvas");
}
// Pelne odświeżenie ekranu z bufora canvas. Uzywane glownie przy starcie.
// GFX przechowuje RGB565, a ILI9488 jest tutaj konfigurowany na 18-bit RGB666,
// dlatego każdy piksel jest rozbijany na trzy bajty przed wyslaniem po SPI.
static void drawTft(){
  if(!canvas.getBuffer())return;
  window(0,0,WIDTH-1,HEIGHT-1);
  digitalWrite(PIN_TFT_DC,HIGH);digitalWrite(PIN_TFT_CS,LOW);
  static uint8_t line[WIDTH*3];
  for(int y=0;y<HEIGHT;y++) {
    const uint16_t* pix=canvas.getBuffer()+y*WIDTH;
    for(int x=0;x<WIDTH;x++) {
      uint16_t c=pix[x];line[x*3]=(c>>8)&0xF8;
      line[x*3+1]=(c>>3)&0xFC;line[x*3+2]=(c<<3)&0xF8;
    }
    spi.writeBytes(line,sizeof(line));
  }
  digitalWrite(PIN_TFT_CS,HIGH);
}

// ---------------------------------------------------------------------------
// Wysyłanie tylko zmienionego prostokata z CANVAS do ILI9488.
// CAŁY canvas jest nadal w PSRAM, lecz zamiast 460 KB wysyłamy zwykle kilka KB.
// Ten sposob znacznie skraca czas blokowania głównej pętli i obsługi pilota.
// ---------------------------------------------------------------------------
// Odświeża tylko wskazany fragment canvasu. Współrzędne są przycinane do
// granic ekranu, aby nie wyslac danych poza obszar 480x320.
static void pushCanvasRegion(int x,int y,int w,int h) {
  if(!canvas.getBuffer())return;
  x=constrain(x,0,(int)WIDTH-1); y=constrain(y,0,(int)HEIGHT-1);
  w=min(w,(int)WIDTH-x); h=min(h,(int)HEIGHT-y);
  if(w<=0||h<=0)return;
  window(x,y,x+w-1,y+h-1);
  digitalWrite(PIN_TFT_DC,HIGH); digitalWrite(PIN_TFT_CS,LOW);
  static uint8_t line[WIDTH*3];
  for(int row=0;row<h;row++){
    const uint16_t* pix=canvas.getBuffer()+(y+row)*WIDTH+x;
    for(int px=0;px<w;px++){
      uint16_t c=pix[px];
      line[px*3]=(c>>8)&0xF8;
      line[px*3+1]=(c>>3)&0xFC;
      line[px*3+2]=(c<<3)&0xF8;
    }
    spi.writeBytes(line,w*3);
  }
  digitalWrite(PIN_TFT_CS,HIGH);
}

// ---------------------------------------------------------------------------
// ODBIOR PILOTA NEC - dekodowanie czasow impulsow bez dodatkowej biblioteki.
// Przerwanie jedynie sklada 32-bitowa ramke; interpretacja przycisku odbywa się
// pozniej w handleRemote(), juz poza ISR.
// ---------------------------------------------------------------------------
static volatile uint32_t rawBits=0;             // Skladana 32-bitowa ramka NEC (LSB first).
static volatile uint8_t bitIndex=0;             // Numer kolejnego bitu ramki NEC: 0..31.
static volatile uint8_t irState=0; // 0=start, 1=lead space, 2=data
static volatile uint32_t lastTransition=0;      // Znacznik czasu ostatniego zbocza IR w mikrosekundach.
static volatile uint32_t pendingIr=0;           // Ostatnia kompletna ramka przekazana do loop().
static volatile bool irReady=false;             // Flaga synchronizujaca ISR z pętla główna.
// ISR: rejestruje czas pomiedzy zboczami sygnalu IR i buduje kod NEC.
// IRAM_ATTR umieszcza funkcje w pamięci dostepnej podczas obsługi przerwan.
static void IRAM_ATTR irChange(){
  uint32_t now=micros(),dt=now-lastTransition;lastTransition=now;
  bool level=digitalRead(PIN_IR);
  if(level) { // koniec stanu LOW
    if(dt>8200 && dt<10500) {irState=1;bitIndex=0;rawBits=0;}
  } else { // koniec stanu HIGH (space)
    if(irState==1){if(dt>3500&&dt<5700) irState=2; else irState=0;}
    else if(irState==2) {
      if(dt>350&&dt<950) {} // 0
      else if(dt>1250&&dt<2150) rawBits|=(1UL<<bitIndex);
      else {irState=0;return;}
      if(++bitIndex==32){pendingIr=rawBits;irReady=true;irState=0;}
    }
  }
}

// ---------------------------------------------------------------------------
// KATALOG STACJI I STAN APLIKACJI
// ---------------------------------------------------------------------------
// Numer pozycji (1..200) jest indeksem katalogu, a nie numerem OFM.
// To OpenFmCatalog decyduje, czy ID jest juz znane, czy wymaga rozwiazania.
// W wariancie katalogu statycznego znaneId() zwraca ID lokalnie bez HTTP.
// ---------------------------------------------------------------------------
// 200 pozycji z nazwami i slugami katalogu.
// ID nie jest rowne numerowi pozycji. OpenFmCatalog udostępnia identyfikatory
// potwierdzone w katalogu; brakujace pozycję nie moga być uzupelniane zgadywaniem.
static constexpr int COUNT=200;

// LISTA STACJI (jak w poprzednim radiu): wybór jest niezależny od odtwarzania.
// Góra/dół przesuwaja podświetlenie, OK uruchamia wybrana stacje.
// Po 12 s bez przyciskow wracamy na ekran radia bez zmiany odtwarzania.
static constexpr int STATION_LIST_ROWS = 8;       // Liczba widocznych wierszy na TFT 480x320.
static constexpr uint32_t STATION_LIST_TIMEOUT_MS = 12000;
static bool stationListActive = false;            // Czy widoczna jest lista zamiast ekranu radia.
static bool stationListDirty = false;             // Przerysowanie tylko przy zmianie zaznaczenia.
static bool stationListPainted = false;           // Czy ekran listy zostal juz wysłany do TFT.
static int stationListSelection = 0;              // Zaznaczony indeks 0..199 (nie zmienia station).
static int stationListFirst = 0;                  // Pierwszy widoczny indeks okna listy.
static uint32_t stationListLastAction = 0;        // Czas ostatniego ruchu/otwarcia menu.

// Numer wpisywany z pilota. Nie zmienia stacji, dopoki nie przejdziemy
// przez dwa zatwierdzenia: OK otwiera liste, nastepne OK uruchamia kanał.
static String stationNumberInput;
static bool stationNumberDirty = false;
static uint32_t stationNumberLastAction = 0;
static constexpr uint32_t STATION_NUMBER_TIMEOUT_MS = 12000;

// Zwraca znany identyfikator OFM dla pozycji; nie uruchamia odtwarzania.
static String channelIdForSlot(int slot) {return OpenFmCatalog::knownId(slot);}

static String currentArtist;                         // Ostatni wykonawca odczytany z ID3v2.
static String currentTitle;                          // Ostatni tytuł odczytany z ID3v2.
static uint32_t shownMetadataVersion=0;         // Ostatnia wersja ID3 juz pokazana na ekranie.

static int station=0,volume=14;                      // Bieżący indeks katalogu (od 0) i głośność 0..21.
static bool pendingPlay=false;                      // Odtwarzanie nowej stacji czeka na zakonczenie STOP.
static bool pendingStop=false;                      // Czy wyslano juz zadanie zatrzymania starej sesji. // sygnalizujemy STOP tylko przy pierwszej zmianie
static bool muted=false;                             // MUTE zachowuje poprzednia wartosc volume.
static String ownId;                                // Reczny identyfikator OFM z komendy Serial.
static String lastCommand;
static String serialLine;                            // Bufor pojedynczej komendy z monitora portu szeregowego.
static uint32_t lastInfoDraw=0,lastClockPoll=0,lastStopReport=0,lastIr=0; // Znaczniki czasu z millis().
static uint32_t switchRequestedAt=0;             // Czas rozpoczecia zmiany stacji - diagnostyka oczekiwania na STOP.
static bool screenDirty=true;                       // Flaga: Wymuś ponowne narysowanie panelu tekstowego.              // Wymuszenie przebudowania tekstów po zmianie kanału/komendy.
static bool clockDrawn=false;                       // Informuje, czy zegar zostal juz narysowany po starcie.               // Po starcie zawsze rysujemy zegar.
static String lastClockText;                // Porównujemy HH:MM:SS, nie liczbę milisekund.
static String lastHeadlineSignature;         // Zmiana nazwy/statusu uruchamia tylko górny obszar.
static String lastAudioSignature;            // Parametry odświeżamy wyłącznie po zmianie danych.

// Zamknij stary strumien asynchronicznie; nowy uruchamia się dopiero po busy()==false.
// Nie blokujemy odczytu przyciskow, gdy HTTPS pobiera jeszcze segment.
// Zleca zmiane stacji bez blokowania pętli głównej. Najpierw zatrzymuje
// aktualny strumien; nowy kanał wystartuje dopiero po OpenFmPlayer::busy()==false.
static void selectStation(int idx);  // Potrzebne dla zatwierdzenia wyboru z listy.

// Otwiera przewijana liste i zaznacza aktualnie grajaca stacje.
// Nie zatrzymuje HLS; metadane i odtwarzanie pracuja w tle.
static void openStationList() {
  if (stationListActive) return;
  stationListActive = true;
  stationListSelection = station;
  stationListFirst = max(0, stationListSelection - STATION_LIST_ROWS / 2);
  stationListFirst = min(stationListFirst, COUNT - STATION_LIST_ROWS);
  stationListLastAction = millis();
  stationListDirty = true;
  stationListPainted = false; // Po otwarciu trzeba narysowac cała liste.
  Serial.printf("[LISTA] Otwarta, wybrano %d/%d\n", stationListSelection + 1, COUNT);
}

// Otwiera liste na wskazanej pozycji (1..200) bez przelaczania dzwieku.
// Wybór jest wycentrowany w 8-wierszowym oknie, o ile pozwala na to zakres.
static void openStationListAtNumber(int number) {
  if (number < 1 || number > COUNT) return;
  stationListActive = true;
  stationListSelection = number - 1;
  stationListFirst = constrain(stationListSelection - STATION_LIST_ROWS / 2,
                               0, COUNT - STATION_LIST_ROWS);
  stationListLastAction = millis();
  stationListDirty = true;
  stationListPainted = false; // Numer otwiera liste z nowym zaznaczeniem.
  Serial.printf("[LISTA] Numer %03d -> zaznaczono %s\n", number,
                OpenFmCatalog::entry(stationListSelection).name);
}

// Zamyka liste i wymusza jednokrotne odtworzenie wszystkich paneli TFT.
static void closeStationList() {
  if (!stationListActive) return;
  stationListActive = false;
  stationListDirty = false;
  stationListPainted = false;
  screenDirty = true;
  clockDrawn = false;
  lastHeadlineSignature = "";
  lastAudioSignature = "";
  lastClockText = "";
}

// Przewija zaznaczenie z zawijaniem końca listy do poczatku (i odwrotnie).
// Nie zmienia aktualnie odtwarzanej stacji az do potwierdzenia OK.
static void scrollStationList(int direction) {
  if (!stationListActive) openStationList();
  stationListSelection = (stationListSelection + direction + COUNT) % COUNT;
  // Po dojściu do krawędzi przenosimy okno o pół strony, nie o 1 wiersz.
  // Dzięki temu pełny transfer ośmiu wierszy nie występuje przy każdym
  // kolejnym naciśnięciu przycisku na brzegu widocznej listy.
  if (stationListSelection < stationListFirst)
    stationListFirst = stationListSelection - (STATION_LIST_ROWS / 2 - 1);
  else if (stationListSelection >= stationListFirst + STATION_LIST_ROWS)
    stationListFirst = stationListSelection - STATION_LIST_ROWS / 2;
  stationListFirst = constrain(stationListFirst, 0, COUNT - STATION_LIST_ROWS);
  stationListLastAction = millis();
  stationListDirty = true;
  Serial.printf("[LISTA] Zaznaczono %d/%d: %s\n", stationListSelection + 1,
                COUNT, OpenFmCatalog::entry(stationListSelection).name);
}

// Dopisuje cyfre 0..9. Wpis jest pokazywany w gornej linii, do 3 cyfr.
// Klawisze numeryczne nie zmieniaja aktualnie grajacej stacji.
static void handleDigitInput(int digit) {
  if (digit < 0 || digit > 9) return;
  if (stationListActive) closeStationList();
  if (stationNumberInput.length() >= 3) stationNumberInput = "";
  stationNumberInput += String(digit);
  stationNumberLastAction = millis();
  stationNumberDirty = true;
  Serial.printf("[NUMER] Wpisano %s\n", stationNumberInput.c_str());
}

// Pierwsze OK po wpisaniu cyfr: pokaz liste z zaznaczona pozycja.
// Nie zatrzymuje ani nie uruchamia jeszcze strumienia HLS.
static void confirmEnteredNumber() {
  if (stationNumberInput.isEmpty()) return;
  const int number = stationNumberInput.toInt();
  stationNumberInput = "";
  stationNumberDirty = false;
  if (number >= 1 && number <= COUNT) {
    openStationListAtNumber(number);
  } else {
    Serial.printf("[NUMER] Nieprawidlowy numer %d; zakres 1..%d\n", number, COUNT);
    screenDirty = true;
    lastHeadlineSignature = "";
  }
}

// Wyświetla wpisywany numer w gornej linii, nie ruszajac dolnych paneli.
// Wysylamy region SPI tylko po kolejnej cyfrze albo wymuszeniu ekranu.
static void renderNumberInput(bool force = false) {
  if (stationNumberInput.isEmpty() || (!stationNumberDirty && !force) ||
      !canvas.getBuffer()) return;
  stationNumberDirty = false;
  canvas.fillRect(0, 0, WIDTH, 45, Kolory::CZARNY);
  canvas.setFont(nullptr);
  canvas.setTextWrap(false);
  canvas.setTextSize(2);
  canvas.setTextColor(Kolory::ZOLTY); // żółty napis (RGB565).
  canvas.setCursor(10, 10);
  canvas.print("WYBIERZ STACJE:");
  canvas.setTextColor(Kolory::CYJAN); // cyan: wpisywane cyfry.
  canvas.setCursor(264, 10);
  canvas.print(stationNumberInput);
  //canvas.drawFastHLine(8, 43, WIDTH-16, Kolory::SZARY_CIEMNY);
  pushCanvasRegion(0, 0, WIDTH, 45);
}

// Zatwierdza podświetlony kanał. Ponowny wybór grajacej stacji nie restartuje HLS.
static void confirmStationList() {
  if (!stationListActive) return;
  const int selected = stationListSelection;
  closeStationList();
  if (selected != station || ownId.length()) selectStation(selected);
  else Serial.println("[LISTA] Wybrano obecnie grajaca stacje, bez restartu.");
}

// Wspolne rysowanie jednego wiersza listy, bez wysylania calego ekranu.
// Kolory, współrzędne, czcionka UTF-8 i oznaczenie ? pozostaja jak w oryginale.
static void drawStationListRow(int row) {
  if (row < 0 || row >= STATION_LIST_ROWS) return;
  const int index = stationListFirst + row;
  if (index >= COUNT) return;
  const int y = 42 + row * 31;
  const bool selected = (index == stationListSelection);

  // Kasujemy tylko obszar aktualnego wiersza, razem z poprzednim podswietleniem.
  canvas.fillRect(0, y - 3, WIDTH, 31, Kolory::CZARNY);
  canvas.setFont(nullptr);
  canvas.setTextWrap(false);
  canvas.setTextSize(2);
  if (selected) canvas.fillRoundRect(4, y - 3, WIDTH - 8, 29, 4, Kolory::TLO_ZAZNACZENIA);
  canvas.setTextColor(selected ? Kolory::CYJAN : Kolory::ZIELONY); // Numer: cyan / zielony.
  canvas.setCursor(10, y + 3);
  canvas.printf("%03d ", index + 1);
  const String name = String(OpenFmCatalog::entry(index).name);
  const uint16_t nameColor = selected ? Kolory::BIALY : Kolory::ZIELONY; // Jednolity kolor ASCII i polskich glifów.
  canvas.setTextColor(nameColor); // Adafruit GFX używa tej barwy dla zwykłych liter.
  OpenFmPolishText::print(canvas, name, 438, nameColor, 2); // Własne polskie glify używają tej samej barwy.
  if (OpenFmCatalog::knownId(index).isEmpty()) {
    canvas.setTextColor(Kolory::POMARANCZOWY_JASNY);
    canvas.setCursor(449, y + 3);
    canvas.print("?");
  }
}

// Rysuje naglowek z numerem zaznaczonej pozycji.
static void drawStationListHeader() {
  canvas.fillRect(0, 0, WIDTH, 38, Kolory::CZARNY);
  canvas.setFont(nullptr);
  canvas.setTextWrap(false);
  canvas.setTextSize(2);
  canvas.setTextColor(Kolory::ZLOTY);
  canvas.setCursor(10, 7);
  canvas.printf("STACJE OPEN FM  %03d/%03d", stationListSelection + 1, COUNT);
  canvas.drawFastHLine(8, 34, WIDTH - 16, Kolory::SZARY_CIEMNY);
}

// Pierwsze otwarcie: pełny ekran. Podczas przewijania w widocznym oknie:
// tylko dwa wiersze (stare i nowe zaznaczenie) oraz niewielki naglowek.
// Gdy lista przesuwa okno 8 pozycji, wysyłamy sam obszar wierszy.
// Nie dotykamy kodu odtwarzacza ani taktowania I2S.
static void drawStationList() {
  if (!stationListActive || !stationListDirty || !canvas.getBuffer()) return;
  stationListDirty = false;

  // Zapamietane poprzednie polozenie jest potrzebne tylko do roznicowego rysowania.
  static int drawnSelection = -1;
  static int drawnFirst = -1;
  const bool fullDraw = !stationListPainted;
  const bool windowMoved = !fullDraw && (drawnFirst != stationListFirst);
  const int previousRow = drawnSelection - stationListFirst;
  const int currentRow = stationListSelection - stationListFirst;

  if (fullDraw) {
    canvas.fillRect(0, 0, WIDTH, HEIGHT, Kolory::CZARNY);
  }
  drawStationListHeader();
  if (fullDraw || windowMoved) {
    // Przy zmianie widocznego zakresu przerysowujemy wszystkie osiem pozycji.
    for (int row = 0; row < STATION_LIST_ROWS; ++row) drawStationListRow(row);
  } else {
    // W tym samym oknie odświeżamy tylko wiersze, które zmieniły stan.
    if (previousRow >= 0 && previousRow < STATION_LIST_ROWS)
      drawStationListRow(previousRow);
    if (currentRow >= 0 && currentRow < STATION_LIST_ROWS && currentRow != previousRow)
      drawStationListRow(currentRow);
  }

  if (fullDraw) {
    canvas.setFont(nullptr);
    canvas.setTextWrap(false);
    canvas.setTextSize(1);
    canvas.setTextColor(Kolory::BIALY);
    canvas.setCursor(9, 298);
    canvas.print("GORA/DOL: wybor   OK: graj   STOP: zamknij");
    pushCanvasRegion(0, 0, WIDTH, HEIGHT); // Tylko przy pierwszym otwarciu.
  } else {
    // W nagłówku zmieniają się wyłącznie cyfry zaznaczonej pozycji.
    // Prefiks "STACJE OPEN FM" i linia pod nim pozostają niezmienne.
    // Przy czcionce GFX 2x cyfry zaczynają się w okolicy X=202.
    pushCanvasRegion(194, 0, 125, 34); // Zamiast całego nagłówka 480x38.
    if (windowMoved) {
      pushCanvasRegion(0, 39, WIDTH, STATION_LIST_ROWS * 31);
    } else {
      if (previousRow >= 0 && previousRow < STATION_LIST_ROWS)
        pushCanvasRegion(0, 39 + previousRow * 31, WIDTH, 31);
      if (currentRow >= 0 && currentRow < STATION_LIST_ROWS && currentRow != previousRow)
        pushCanvasRegion(0, 39 + currentRow * 31, WIDTH, 31);
    }
  }
  drawnSelection = stationListSelection;
  drawnFirst = stationListFirst;
  stationListPainted = true;
}

static void requestSwitch(){
  pendingPlay=true;
  screenDirty=true;                      // Nowa stacja musi pojawić się od razu na TFT.
  switchRequestedAt=millis();
  if(OpenFmPlayer::busy()) {
    if(!pendingStop) {
      Serial.println("[STEROWANIE] Zatrzymuję poprzedni strumień HLS...");
      OpenFmPlayer::stop();
      pendingStop=true;
    }
  } else {
    pendingStop=false;
  }
}
// Wybiera pozycję katalogu z zawijaniem: po 200 wraca do 1, przed 1 do 200.
// Czysci metadane poprzedniego utworu i zleca przełączenie odtwarzacza.
static void selectStation(int idx){
  stationNumberInput = "";
  stationNumberDirty = false;
  closeStationList();
  station=(idx+COUNT)%COUNT;
  ownId="";
  currentArtist="";currentTitle="";
  Serial.printf("[STACJA] Wybrano %d/%d: %s (%s)\n",station+1,COUNT,
                OpenFmCatalog::entry(station).name,channelIdForSlot(station).c_str());
  requestSwitch();
}
// Umozliwia uruchomienie dowolnego kanału OFM wpisanego recznie w Serial.
// Akceptuje zapis OFM10 oraz skrocony zapis 10.
static void customStation(String id){
  closeStationList();
  id.trim();id.toUpperCase();if(!id.startsWith("OFM"))id="OFM"+id;
  if(id.length()<4)return;
  ownId=id;
  currentArtist="";currentTitle="";
  Serial.println("[STACJA] Własny kanał: "+id);
  requestSwitch();
}
// Uruchamia aktualnie wybrana pozycję: pobiera jej OFM ID z katalogu,
// a nastepnie przekazuje je do asynchronicznego silnika HLS.
static void playCurrent(){
  String id=ownId.length()?ownId:OpenFmCatalog::resolveId(station);
  if(id.isEmpty()) {
    Serial.printf("[STACJA] Brak zgodnego strumienia OpenFM dla pozycji %d (%s). Uzyj n/p.\n",
                  station+1,OpenFmCatalog::entry(station).name);
    return;
  }
  screenDirty=true; // teraz mozemy pokazac odkryte OFMxxx
  Serial.println("[TEST] Uruchamiam "+id);
  if(!OpenFmPlayer::start(id,muted?0:volume))Serial.println("[TEST] Nie udalo sie wystartowac HLS");
}
// Wyswietla liste poleceń dostepnych w monitorze portu szeregowego.
static void printHelp(){
 Serial.println("\n====== OPENFM HLS PLAYER ======");
 Serial.println("g 1..200 - wybierz pozycje; 1..9 - szybki wybor");
 Serial.println("Katalog 200 nazw; odtwarzane sa pozycje z potwierdzonym ID OFM. Nie wszystkie pozycje musza byc HLS.");
 Serial.println("o OFM10 - dowolny identyfikator OFM");
 Serial.println("n/p - nastepny/poprzedni kanal");
 Serial.println("meta wykonawca|tytul - PODGLAD TESTOWY metadanych (nie dane OpenFM)");
 Serial.println("s - stop, r - start, + / - - glosnosc, m - pamiec, h - pomoc");
 Serial.println("Pilot: GORA/DOL = lista; OK = wybor; LEWO/PRAWO = natychmiastowa zmiana");
 Serial.println("Na liscie: STOP = zamknij; brak ruchu przez 12 s = powrot.");
 Serial.println("VOL+/VOL-, MUTE, STOP i FF+/- jak dotychczas.");
 Serial.println("Nie wypisujemy adresow z tokenami do Serial.");
 Serial.println("================================\n");
}
// Parser poleceń tekstowych. Wywolanie nastepuje po odebraniu znaku CR/LF.
// Polecenia stacji trafiaja do selectStation(), a głośność do OpenFmPlayer.
static void commandLine(String input){
  input.trim();if(!input.length())return;
  lastCommand=input;
  if(input.startsWith("g ") || input.startsWith("G ")){
    String n=input.substring(2);n.trim();int v=n.toInt();
    if(v>=1&&v<=COUNT)selectStation(v-1);
    else Serial.println("[STACJA] Dozwolone numery: 1..200");
  }
  else if(input.startsWith("meta ")){
    String line=input.substring(5);int divider=line.indexOf('|');
    if(divider>=0){
      currentArtist=line.substring(0,divider);currentTitle=line.substring(divider+1);
      currentArtist.trim();currentTitle.trim();screenDirty=true;
      Serial.println("[META] Dane reczne tylko do testu wyswietlacza.");
    } else Serial.println("[META] Uzyj: meta wykonawca|tytul");
  }
  else if(input.length()==1 && input[0]>='1' && input[0]<='9') selectStation(input[0]-'1');
  else if(input.startsWith("o ")||input.startsWith("O "))customStation(input.substring(2));
  else if(input=="n")selectStation(station+1);
  else if(input=="p")selectStation(station-1);
  else if(input=="s"){pendingPlay=false;pendingStop=OpenFmPlayer::busy();OpenFmPlayer::stop();}
  else if(input=="r")requestSwitch();
  else if(input=="+"){volume=min(21,volume+1);muted=false;OpenFmPlayer::setVolume(volume);}
  else if(input=="-"){volume=max(0,volume-1);muted=false;OpenFmPlayer::setVolume(volume);}
  else if(input=="m")OpenFmPlayer::stats();
  else if(input=="h")printHelp();
}
// Odbiera gotowa ramke NEC przygotowana przez ISR i mapuje kod przycisku
// na zmiane stacji, głośności, STOP, restart lub MUTE.
static void handleRemote(){
 if(!irReady)return;
 noInterrupts();uint32_t ir=pendingIr;irReady=false;interrupts();
 uint8_t address=ir&255,invAddr=(ir>>8)&255,key=(ir>>16)&255,invKey=(ir>>24)&255; // Bajty adresu/komendy NEC.
 if((uint8_t)(key^invKey)!=0xff) {Serial.println("[IR] Niepoprawna ramka NEC");return;}
 // Adres NEC bywa rozszerzony; w pilocie rozpoznajemy kod komendy.
 if(millis()-lastIr<120)return;lastIr=millis();
 Serial.printf("[IR] addr=0x%02X addr2=0x%02X cmd=0x%02X\n",address,invAddr,key);
 // Kody przycisków numerycznych pilota.
 int digit = -1;
 switch (key) {
   case rcCmdKey0: digit=0; break;
   case rcCmdKey1: digit=1; break;
   case rcCmdKey2: digit=2; break;
   case rcCmdKey3: digit=3; break;
   case rcCmdKey4: digit=4; break;
   case rcCmdKey5: digit=5; break;
   case rcCmdKey6: digit=6; break;
   case rcCmdKey7: digit=7; break;
   case rcCmdKey8: digit=8; break;
   case rcCmdKey9: digit=9; break;
   default: break;
 }
 if (digit >= 0) { handleDigitInput(digit); return; }
 switch(key){
  case rcCmdArrowRight: selectStation(station+1); break; // PRAWO: od razu następna stacja.
  case rcCmdArrowLeft: selectStation(station-1); break; // LEWO: od razu poprzednia stacja.
  case rcCmdArrowDown: scrollStationList(+1); break;  // DOL: następna pozycja na liscie.
  case rcCmdArrowUp: scrollStationList(-1); break;  // GORA: poprzednia pozycja na liscie.
  case rcCmdVolumeUp: volume=min(21,volume+1);muted=false;OpenFmPlayer::setVolume(volume);break;
  case rcCmdVolumeDown: volume=max(0,volume-1);muted=false;OpenFmPlayer::setVolume(volume);break;
  case rcCmdStop: // STOP anuluje cyfry/liste; poza nimi zatrzymuje audio.
    if (!stationNumberInput.isEmpty()) {
      stationNumberInput = ""; stationNumberDirty = false;
      screenDirty = true; lastHeadlineSignature = "";
    } else if(stationListActive) closeStationList();
    else {pendingPlay=false;pendingStop=OpenFmPlayer::busy();OpenFmPlayer::stop();}
    break;
  case rcCmdOk: // OK: numer -> lista -> odtwarzanie; poza nimi restart.
    if (!stationNumberInput.isEmpty()) confirmEnteredNumber();
    else if(stationListActive) confirmStationList();
    else requestSwitch();
    break;
  case rcCmdMute: muted=!muted;OpenFmPlayer::setVolume(muted?0:volume);break;
  case rcCmdBankUp: selectStation(station+1);break; // FF+ jak dotychczas.
  case rcCmdBankDown: selectStation(station-1);break; // FF- jak dotychczas.
  default:break;
 }
}
// ============================================================================
// EKRAN ILI9488: CANVAS - UKŁAD Z ORYGINALNEGO RADIA
// ============================================================================
// Oryginalny projekt rysował całość do GFXcanvas16 i następnie wysyłał bufor
// do ILI9488. Zachowujemy tę metodę (brak migotania), lecz dla responsywności
// rozdzielamy ekran na 3 małe obszary. Pełnych 480x320 nie wysyłamy co sekundę.
// Każda funkcja rysuje najpierw w pamięci, a potem wysyła TYLKO swój prostokąt.
//
//   0..208 px : nazwa stacji, wykonawca/tytuł po dwa wiersze i stan.
// 208..260 px : parametry TS, Hz i PCM w jednej linii.
// 260..320 px : liczniki i głośność po lewej, zegar po prawej.
//
// Dzięki temu praca sieciowa i dekoder AAC nie czekają na wielokrotne wysłanie
// setek kilobajtów obrazu po SPI.
// ============================================================================

// Górny panel: nagłówek, nazwa stacji, dwa wiersze wykonawcy i tytułu, stan.
// Pozostaje GFXcanvas16, polskie glify UTF-8 i odświeżanie wyłącznie regionu.
// Przeznaczamy obszar Y=0..207 na tekst, parametry TS/Hz/PCM są niżej.
//
// Drukuje tekst bezpośrednio za etykietą (po jednej spacji). Gdy w bieżącym
// wierszu zabraknie miejsca, pozostały tekst trafia do drugiego wiersza,
// zaczynającego się od lewego marginesu. Granice są liczone w znakach UTF-8,
// nie w bajtach, aby polskie litery nie były rozdzielane na pół.
// firstTextX - pozycja pierwszego znaku za etykietą i spacją.
static void printTwoLineUtf8(const String& source, int x, int firstY,
                             int firstTextX, uint16_t color, uint8_t size = 2) {
  const String text = source.isEmpty() ? String("--") : source;
  const int charWidth = 6 * (int)size;  // Identyczny krok dla ASCII i polskich glifów.
  const int firstCapacity = (WIDTH - 8 - firstTextX) / charWidth;
  if (firstCapacity <= 0) return;

  int pos = 0;
  int lastSpace = -1;
  int count = 0;
  // Liczymy pełne punkty kodowe UTF-8, zapisując granicę ostatniego słowa.
  while (pos < (int)text.length() && count < firstCapacity) {
    const unsigned char c = (unsigned char)text[pos];
    if (c == ' ') lastSpace = pos;
    int bytes = 1;
    if ((c & 0xE0) == 0xC0) bytes = 2;
    else if ((c & 0xF0) == 0xE0) bytes = 3;
    else if ((c & 0xF8) == 0xF0) bytes = 4;
    pos = min(pos + bytes, (int)text.length());
    ++count;
  }

  int split = pos;
  // Jeżeli mieści się całe słowo, łamiemy w ostatniej spacji.
  // Gdy słowo jest zbyt długie, łamiemy po pełnym znaku UTF-8.
  if (pos < (int)text.length() && lastSpace > 0) split = lastSpace;
  String first = text.substring(0, split);
  first.trim();
  canvas.setCursor(firstTextX, firstY);
  OpenFmPolishText::print(canvas, first, WIDTH - 8, color, size);

  if (split < (int)text.length()) {
    String second = text.substring(split);
    second.trim();
    canvas.setCursor(x, firstY + 21);
    OpenFmPolishText::print(canvas, second, WIDTH - 8, color, size);
  }
}

// Wypełnia Y=0..207; głośność przeniesiono na dół, obok liczby kanałów.
static void renderHeadline(bool force=false) {
  if(!canvas.getBuffer()) return;
  const String status = pendingPlay ? "ZMIANA..." : OpenFmPlayer::state();
  const String id = ownId.length() ? ownId : channelIdForSlot(station);
  const String stationName = ownId.length() ? "Reczny kanal OpenFM" : String(OpenFmCatalog::entry(station).name);
  const String signature = String(station)+"|"+id+"|"+stationName+"|"+status+"|"+
                           currentArtist+"|"+currentTitle;
  if(!force && signature==lastHeadlineSignature)return;
  lastHeadlineSignature=signature;

  canvas.fillRect(0,0,WIDTH,208,Kolory::CZARNY);
  canvas.setFont(nullptr);
  canvas.setTextWrap(false);
  canvas.setTextSize(3);
  canvas.setTextColor(Kolory::ZIELONO_TURKUSOWY);
  canvas.setCursor(45,10);
  canvas.printf("Open FM stacja %03d/%03d",station+1,COUNT);
  // Zrezygnowano z poziomej linii pod nagłówkiem, aby odzyskać miejsce.

  canvas.setTextSize(3);
  canvas.setTextColor(Kolory::ZLOTY);
  canvas.setCursor(12,45);  // Nazwa stacji: skala 3x, taka sama jak nagłówek.
  OpenFmPolishText::print(canvas, stationName, WIDTH - 8, Kolory::ZLOTY, 3);
  canvas.setTextSize(2);  // Pozostałe etykiety i metadane zachowują skalę 2x.

  // Etykiety mają wyróżniający kolor cyan; właściwe metadane pozostają jasne.
  // 6 px * skala 2 = 12 px na znak; doliczamy jedną spację po dwukropku.
  canvas.setTextColor(Kolory::CYJAN);
  canvas.setCursor(12,80);
  canvas.print("Wykonawca: ");
  const int artistX = canvas.getCursorX();
  canvas.setTextColor(Kolory::BIALY);
  printTwoLineUtf8(currentArtist, 12, 80, artistX, Kolory::BIALY, 2);

  canvas.setTextColor(Kolory::CYJAN);
  canvas.setCursor(12,134);
  canvas.print("Tytul: ");
  const int titleX = canvas.getCursorX();
  canvas.setTextColor(Kolory::BIALY);
  printTwoLineUtf8(currentTitle, 12, 134, titleX, Kolory::BIALY, 2);

  canvas.setTextColor(Kolory::POMARANCZOWY_JASNY);
  canvas.setCursor(12,190);  // Status poniżej dwóch wierszy tytułu; mieści się w panelu 208 px.
  canvas.print("Stan: ");canvas.print(status);
  pushCanvasRegion(0,0,WIDTH,208);
}

// Jedna linia TS / Hz / PCM w Y=224; dolny pasek zachowuje zegar bez zmian.
// Parametry i głośność odświeżają się tylko po zmianie odpowiednich wartości.
static void renderAudioInfo(bool force=false) {
  if (!canvas.getBuffer()) return;
  const uint32_t kbps = OpenFmPlayer::bitrateKbps();
  const uint32_t hz = OpenFmPlayer::sampleRate();
  const uint8_t bits = OpenFmPlayer::bitsPerSample();
  const uint8_t channels = OpenFmPlayer::channels();
  const uint32_t segments = OpenFmPlayer::segments();
  const uint32_t segmentBytes = OpenFmPlayer::lastSegmentBytes();

  const String signature = String(kbps) + "|" + String(hz) + "|" + String(bits)
                          + "|" + String(channels) + "|" + String(segments)
                          + "|" + String(segmentBytes) + "|" + String(muted ? 0 : volume);
  if (!force && signature == lastAudioSignature) return;
  lastAudioSignature = signature;

  canvas.fillRect(0, 208, WIDTH, 52, Kolory::CZARNY);
  canvas.fillRect(0, 260, 245, 60, Kolory::CZARNY);
  canvas.setFont(nullptr);
  canvas.setTextWrap(false);
  canvas.setTextSize(2);
  canvas.setTextColor(Kolory::MAGENTA);

  // Zwięzły zapis pozwala zmieścić wszystkie trzy parametry przy skali GFX 2x.
  canvas.setCursor(12, 224);
  canvas.print("TS:");
  if (kbps) canvas.print(kbps); else canvas.print("--");
  canvas.print("kb/s    Hz:");
  if (hz) canvas.print(hz); else canvas.print("--");
  canvas.print("    PCM:");
  if (bits) canvas.print(bits); else canvas.print("--");
  canvas.print("bit");

  canvas.setTextColor(Kolory::JASNY_SZARONIEBIESKI);
  canvas.setCursor(12, 262);
  canvas.print("Segmenty: ");
  canvas.print(segments);
  canvas.setCursor(12, 282);
  canvas.print("Bajty seg.: ");
  canvas.print(segmentBytes);
  canvas.setCursor(12, 302);
  canvas.print("Kanaly: ");
  if (channels) canvas.print(channels); else canvas.print("-");
  canvas.setTextColor(Kolory::ZIELONY_JASNY);
  canvas.print("   VOL:");
  canvas.print(muted ? 0 : volume);

  pushCanvasRegion(0, 208, WIDTH, 52);
  pushCanvasRegion(0, 260, 245, 60);
}

// ============================================================================
// ZEGAR - DOSŁOWNIE TA SAMA CZCIONKA I BAZA PISANIA CO W ORYGINALE.
// Pozycja bazowa jest STAŁA: 245,310. Nie wyrównujemy do prawej strony!
// Nie ma też awaryjnej zamiany na zwykłą czcionkę GFX przy szerszej cyfrze.
// Kadr zegara jest wysyłany przez SPI 40 MHz tylko przy zmianie sekundy.
// ============================================================================
// Rysuje zegar z synchronizowanego czasu systemowego (NTP). Pozycja bazowa
// czcionki DS_DIGII35pt7b jest stała, aby szerokość cyfr nie powodowała skakania.
static void renderClock(bool force=false) {
  if (!canvas.getBuffer()) return;
  time_t now = time(nullptr);
  if (now < 1600000000) return;
  struct tm t;
  localtime_r(&now, &t);
  char timeText[12];
  snprintf(timeText, sizeof(timeText), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
  if (!force && lastClockText == timeText) return;
  lastClockText = timeText;

  // Wierna pozycja z pierwotnego drawClock() w radio_v3_ILI9488_canvas.ino.
  // Rysowanie jest stałe lewostronne niezależnie od szerokości cyfr.
  static constexpr int CLOCK_X = 245;
  static constexpr int CLOCK_Y = 260;
  static constexpr int CLOCK_W = WIDTH - CLOCK_X;
  static constexpr int CLOCK_H = HEIGHT - CLOCK_Y;
  canvas.fillRect(CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H, Kolory::CZARNY);
  canvas.setFont(&DS_DIGII35pt7b);
  canvas.setTextSize(1);
  canvas.setTextColor(Kolory::CYJAN);  // Kolor RGB565: cyjan dla zegara.
  canvas.setTextWrap(false);
  canvas.setCursor(245, 310);  // Oryginalna stała baza, bez przeliczania szerokości.
  canvas.print(timeText);
  canvas.setFont(nullptr);
  canvas.setTextSize(1);
  pushCanvasRegion(CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H);
  clockDrawn = true;
}

// Oddzielne zadanie odczytuje ID3 z MPEG-TS; ekran pobiera tylko gotowy wynik.
// Bez HTTPS, dekodowania ani operacji blokujących w pętli TFT.
// Pobiera atomowy snapshot metadanych z modułu OpenFmMetadata.
// Numer wersji pozwala odświeżyć ekran tylko wtedy, gdy zmienił się tag ID3.
static void updateOpenFmMetadata() {
  String artist,title;
  uint32_t version=0;
  OpenFmMetadata::snapshot(artist,title,version);
  if(version==shownMetadataVersion) return;
  shownMetadataVersion=version;
  currentArtist=artist;
  currentTitle=title;
  screenDirty=true;
}

// Nieblokująca obsługa obrazu: metadane, panel tekstowy, parametry audio i zegar.
// Zegar jest sprawdzany co 100 ms, ale wysyłany na TFT dopiero po zmianie sekundy.
static void screenLoop() {
  updateOpenFmMetadata();
  if (!stationNumberInput.isEmpty() &&
      millis() - stationNumberLastAction >= STATION_NUMBER_TIMEOUT_MS) {
    stationNumberInput = "";
    stationNumberDirty = false;
    screenDirty = true;
    lastHeadlineSignature = "";
  }
  if (stationListActive) {
    // Tak jak w starym radiu: po 12 s bezczynnosci przywracamy ekran grania.
    if (millis() - stationListLastAction >= STATION_LIST_TIMEOUT_MS)
      closeStationList();
    else { drawStationList(); return; }
  }
  // Reakcja na pilota nie musi czekać na kolejny cykl zegara.
  const bool force = screenDirty;
  if (force) screenDirty = false;
  renderHeadline(force);
  renderNumberInput(force);

  // Odpytywanie parametrów częściej niż co sekundę nie poprawia czytelności,
  // bo same segmenty HLS są pobierane w odstępach kilku sekund.
  if (force || millis() - lastInfoDraw >= 1200) {
    lastInfoDraw = millis();
    renderAudioInfo(force);
  }

  // Czas odczytujemy około 10 razy na sekundę, lecz do SPI trafia nowy obraz
  // dokładnie przy zmianie sekundy (zwykle raz na 1000 ms).
  if (!clockDrawn || millis() - lastClockPoll >= 100) {
    lastClockPoll = millis();
    renderClock(force);
  }
}

// ---------------------------------------------------------------------------
// SETUP - jednorazowa inicjalizacja urządzenia po włączeniu lub resecie.
// Kolejność: Serial -> TFT -> IR -> Wi-Fi -> NTP -> wybór pierwszej stacji.
// ---------------------------------------------------------------------------
void setup(){
 Serial.begin(115200);delay(400);                         // Uruchomienie diagnostyki Serial.
 Serial.printf("[BOOT] reset=%d heap=%u psram=%u\n",(int)esp_reset_reason(),(unsigned)ESP.getFreeHeap(),(unsigned)ESP.getFreePsram()); // Stan pamięci i przyczyna resetu.
 tftBegin();                                             // Inicjalizacja wyświetlacza i canvasu.
 pinMode(PIN_IR,INPUT_PULLUP);                           // Odbiornik IR w stanie spoczynkowym HIGH.
 lastTransition=micros();attachInterrupt(digitalPinToInterrupt(PIN_IR),irChange,CHANGE); // Start dekodowania NEC.
 WiFi.mode(WIFI_STA);                                   // ESP32 pracuje jako klient sieci (Station).
 WiFiManager wm;                                       // Obiekt zarządzający portalem konfiguracji Wi-Fi.
 wm.setConfigPortalTimeout(150);                       // Portal konfiguracyjny zamyka się po 150 s.
 if(!wm.autoConnect("ESP32S3-OpenFM-TEST")){            // Łączy z zapisaną siecią lub uruchamia portal.
    Serial.println("[WiFi] Nie polaczono. Restart za 3s.");delay(3000);ESP.restart();
 }
 Serial.printf("[WiFi] IP=%s RSSI=%d\n",WiFi.localIP().toString().c_str(),WiFi.RSSI());
 // Polska strefa czasowa, automatyczna zmiana CET/CEST jak w głównym radiu.
 configTzTime("CET-1CEST,M3.5.0,M10.5.0/3","pool.ntp.org","time.google.com"); // Polska strefa CET/CEST + synchronizacja NTP.
 printHelp();
 Serial.println("[AUTO] Wi-Fi gotowe. Uruchamiam pierwszą stację listy.");
 selectStation(0);                                     // Automatyczny start od pierwszej pozycji katalogu.
 // Jednorazowe wyczyszczenie całości usuwa także pozostałości z poprzedniego
 // programu oraz niepożądane szare tło dolnego paska.
 canvas.fillScreen(Kolory::CZARNY);                             // Czyszczenie calego bufora RGB565.
 drawTft();                                             // Jednorazowe wyslanie czarnego ekranu do TFT.
 screenDirty = true;                                   // Wymus pierwsze narysowanie paneli.
 screenLoop();
}
// ---------------------------------------------------------------------------
// LOOP - główna pętla sterująca. Nie wykonuje tu pobierania HLS ani dekodowania;
// te zadania pracuja w module OpenFmPlayer na osobnych zadaniach FreeRTOS.
// Pętla odbiera Serial/pilot, realizuje odłożone przełączenie i odświeża ekran.
// ---------------------------------------------------------------------------
void loop(){
 while(Serial.available()){                              // Odbior znaków komendy bez blokowania pętli.
   char c=Serial.read();
   if(c=='\n'||c=='\r') {if(serialLine.length()){commandLine(serialLine);serialLine="";}}
   else if(serialLine.length()<350)serialLine+=c;
 }
 handleRemote();                                       // Obsługa kompletnej ramki pilota NEC.
 if(pendingPlay && !OpenFmPlayer::busy()){              // Nowa stacja dopiero po zakończeniu poprzedniej sesji.
   pendingPlay=false;pendingStop=false;playCurrent();
 }
 if(pendingStop && !OpenFmPlayer::busy())pendingStop=false; // Zwolnienie flagi po zakończeniu STOP.
 // Gdy HTTPS faktycznie utknie, nie podmieniamy zasobow na sile.
 // Serial wyjaśni dlaczego pilot czeka, a kolejne strzałki wybierają ostatni kanał.
 if(pendingPlay && OpenFmPlayer::busy() && millis()-switchRequestedAt>6500 &&
    millis()-lastStopReport>5000){
   lastStopReport=millis();
   Serial.printf("[STEROWANIE] Oczekiwanie na STOP %lu ms; %s\n",
       (unsigned long)(millis()-switchRequestedAt),OpenFmPlayer::state().c_str());
 }
 screenLoop();                                         // Aktualizacja paneli i zegara bez pelnego odswiezania.
 delay(3);                                              // Krótkie oddanie czasu innym zadaniom FreeRTOS.
}
