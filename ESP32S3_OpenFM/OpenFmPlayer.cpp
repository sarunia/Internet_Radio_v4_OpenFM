/*=============================================================================
  OpenFmPlayer.cpp - SILNIK ODTWARZANIA OPENFM HLS DLA ESP32-S3
  -----------------------------------------------------------------------------
  Rola modułu
    Samodzielny klient HLS dla kanałów OpenFM. Pobiera token dostępu, analizuje
    playlisty MASTER/MEDIA, pobiera segmenty MPEG-TS, przekazuje je do dekodera
    MTS/AAC Helix i wysyła zdekodowany dźwięk przez I2S do przetwornika PCM5102A.

  Przepływ danych
    OpenFM API/token -> HLS MASTER -> HLS MEDIA -> MPEG-TS -> MTSDecoder
    -> AACDecoderHelix -> VolumeStream -> I2SStream -> PCM5102A.
    Równolegle surowe bajty MPEG-TS trafiają do OpenFmMetadata::feed(), który
    odczytuje wykonawcę i tytuł z osobnego PID ID3v2. Parser metadanych nie
    modyfikuje danych przekazywanych do dekodera audio.

  Sprzęt i konfiguracja wynikająca z kodu
    ESP32-S3; I2S: BCLK=GPIO16, DATA/DOUT=GPIO17, LRCK/WS=GPIO18; I2S port 0.
    Bufor audio: 96 KiB (alokacja przez BufferRTOS; konfiguracja zależna od
    biblioteki AudioTools i dostępnej pamięci). Głośność: zakres 0..21.

  Zależności (wersje należy sprawdzić w Arduino IDE / Library Manager)
    - Arduino-ESP32 Core: w logach użytkownika występowała wersja 3.3.12.
    - Arduino Audio Tools (AudioTools.h, AudioTools/Concurrency/RTOS.h).
    - CodecHelix / AACDecoderHelix oraz CodecMTS / MTSDecoder z AudioTools.
    - Arduino-ESP32: WiFi, WiFiClientSecure, HTTPClient, FreeRTOS.
    - Moduły projektu: OpenFmPlayer.h, OpenFmResolver.h, OpenFmMetadata.h.
    Nie wpisano numerów wersji AudioTools/Helix, ponieważ nie wynikają z tego
    pliku źródłowego. Przed publikacją należy wpisać wersje faktycznie użyte
    i przetestowane w środowisku autora.

  Bezpieczeństwo
    tls.setInsecure() wyłącza weryfikację certyfikatu TLS. To ustawienie
    testowe: w wydaniu publicznym należy zastosować zaufany certyfikat CA lub
    inny właściwy mechanizm weryfikacji TLS. Tokeny HLS są poufne i nie wolno
    wypisywać pełnych adresów zawierających parametr t= do logu.

  Ważne ograniczenia implementacji
    - Obsługiwane są playlisty HLS z segmentami MPEG-TS i Content-Length.
    - Transfer chunked, szyfrowanie EXT-X-KEY oraz mapy fMP4 EXT-X-MAP są tu
      celowo odrzucane; wymagają osobnej obsługi.
    - Początkowa konfiguracja I2S to 44100 Hz; format docelowy jest następnie
      aktualizowany na podstawie AudioInfo odczytanego z dekodera AAC.
    - Moduł uruchamia osobne zadania FreeRTOS dla pobierania i dekodowania.

  Interfejs publiczny modułu znajduje się w OpenFmPlayer.h. Funkcje statystyk
  udostępniają wartości dla głównego pliku .ino i interfejsu TFT.
=============================================================================*/

#include "OpenFmPlayer.h"
#include "OpenFmResolver.h"
#include "OpenFmMetadata.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecHelix.h"
#include "AudioTools/AudioCodecs/CodecMTS.h"
#include "AudioTools/Concurrency/RTOS.h"

// ============================================================================
// PRZESTRZEŃ NAZW MODUŁU
// Elementy zewnętrzne (start/stop/stats) udostępnia OpenFmPlayer.h.
// Funkcje static oraz obiekty poniżej pozostają prywatne dla tego pliku.
// ============================================================================
namespace OpenFmPlayer {
// --- Parametry sprzętowe i stan współdzielony --------------------------------
// PCM5102A jest przetwornikiem I2S bez osobnej linii MCLK w tej konfiguracji.
// Częstotliwość I2S może zmieniać się podczas pracy zależnie od ramek AAC.
// Odczyty statystyk wykonywane są przez ekran, a zapisy przez zadania FreeRTOS.
// volatile sygnalizuje współdzielony stan, nie zastępuje pełnej synchronizacji.
static constexpr int BCLK=16, DATA=17, LRCK=18; // Piny magistrali I2S do PCM5102A.
static constexpr size_t BUFFER_BYTES=96*1024; // Bufor kolejki audio: 96 KiB.
static volatile bool running=false, stopRequested=false; // Flagi współdzielone między zadaniami.
static volatile int wantedVolume=14; // Ostatnia żądana głośność 0..21.
static volatile uint32_t playedBytes=0, receivedBytes=0, segmentCount=0; // Liczniki sesji.
static volatile uint32_t latestSegmentBytes=0; // Rozmiar ostatniego w pełni pobranego segmentu MPEG-TS (bajty).
static volatile uint32_t tsKbps=0, decodedHz=0;            // Zmierzona przepływność kontenera TS oraz częstotliwość PCM z dekodera.
static volatile uint8_t decodedChannels=0, decodedBits=0; // Liczba kanałów i głębia PCM zgłaszane przez dekoder Helix.
static uint32_t activeI2SHz=0;                            // Ostatnia częstotliwość przekazana do toru I2S (Hz).
// Pamiętamy aktywny format wyjściowy, aby unikać zbędnych rekonfiguracji
// dla każdej kolejnej porcji ramek AAC.
static uint8_t activeI2SChannels=0, activeI2SBits=0;
static volatile uint8_t statusCode=0; // Kody stanu: 0=STOP, 1=START, 2=ODTWARZANIE, 3=BŁĄD, 4=ZATRZYMYWANIE.
static String channelId;
// Uchwyty zadań: controlTask steruje siecią/HLS, playbackTask dekoduje audio.
// playbackAlive jest sprawdzane przed zwolnieniem współdzielonego toru audio.
static TaskHandle_t controlTask=nullptr, playbackTask=nullptr;
static volatile bool playbackAlive=false;

// --- Łańcuch przetwarzania audio ---------------------------------------------
// Sieć -> BufferRTOS/QueueStream -> MTSDecoder -> AACDecoderHelix
//      -> VolumeStream -> I2SStream -> PCM5102A.
// Jeden strumień pobierany jest przez HTTP, a niezależne zadanie dekoduje AAC.
// Dzięki kolejce czasowe opóźnienia sieci nie muszą blokować dekodera.
static I2SStream audioI2S; // Fizyczne wyjście I2S.
static VolumeStream volumeOutput(audioI2S); // Regulacja poziomu przed DAC.
static AACDecoderHelix aac; // Dekoder ramek AAC (Helix).
static MTSDecoder tsDecoder(aac); // Demultiplekser MPEG-TS -> AAC.
static EncodedAudioStream decoded(&volumeOutput,&tsDecoder); // Łańcuch dekodowania i wyjścia PCM.
static BufferRTOS<uint8_t> audioBuffer(0); // Bufor RTOS; rozmiar przydzielany w worker().
static QueueStream<uint8_t> byteQueue(audioBuffer); // Strumień bajtów między pobieraniem a dekoderem.
static StreamCopy audioCopy(decoded,byteQueue); // Kopiuje kolejkę do wejścia dekodera.

// Adresy URL mogą zawierać tokeny dostępu: nie wypisujemy ich do logów.
// absoluteUrl() rozwija URI względne z playlisty względem adresu źródłowego.
// Buduje pełny adres segmentu lub playlisty z URI bezwzględnego, //host/path
// albo względnego. Uwzględnia query tokenu w adresie nadrzędnym.
/**
 * @brief Rozwija odnośnik z playlisty HLS do pełnego adresu HTTP(S).
 * @param parent Adres playlisty nadrzędnej, służący za bazę URI.
 * @param child URI znaleziony w treści playlisty MASTER lub MEDIA.
 * @return Pełny adres lub pusty String, gdy nie można ustalić adresu bazowego.
 * @note Funkcja nie wykonuje żądań sieciowych i nie wypisuje tokenów.
 */
static String absoluteUrl(const String& parent, String child) {
  child.trim();
  if(child.startsWith("https://")||child.startsWith("http://")) return child;
  const int sep=parent.indexOf("://"); if(sep<0) return "";
  const int hostEnd=parent.indexOf('/',sep+3);
  String origin=hostEnd<0?parent:parent.substring(0,hostEnd);
  if(child.startsWith("//")) return parent.substring(0,sep+1)+child;
  if(child.startsWith("/")) return origin+child;
  String base=parent;
  int q=base.indexOf('?'); if(q>=0) base.remove(q);
  int slash=base.lastIndexOf('/'); if(slash<0) return "";
  return base.substring(0,slash+1)+child;
}

// Pobiera tekstową playlistę HLS. Ogranicza rozmiar odpowiedzi do 32 KiB,
// sprawdza kod HTTP i sygnaturę #EXTM3U. Nie loguje URL, który może zawierać token.
/**
 * @brief Pobiera niewielką tekstową playlistę HLS i kontroluje jej nagłówek.
 * @param url Adres playlisty, zwykle z czasowym tokenem dostępu.
 * @param payload Bufor wyjściowy przeznaczony na zawartość playlisty.
 * @param code Wyjściowy kod HTTP lub -1 przy niepowodzeniu begin().
 * @return true wyłącznie dla HTTP 200 i poprawnej sygnatury #EXTM3U.
 * @warning setInsecure() wyłącza uwierzytelnienie serwera TLS.
 * @note Ograniczenie rozmiaru chroni pamięć ESP32 przed dużą odpowiedzią.
 */
static bool getText(const String& url, String& payload, int& code) {
  WiFiClientSecure tls; tls.setInsecure(); // wyłącznie wersja testowa
  tls.setTimeout(6500);
  HTTPClient http; http.setConnectTimeout(4500); http.setTimeout(6500);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if(!http.begin(tls,url)) {code=-1;return false;}
  http.addHeader("Accept","application/vnd.apple.mpegurl,application/x-mpegURL,*/*");
  code=http.GET(); // Wykonanie żądania HTTP; kod odpowiedzi zwracany wywołującemu.
  if(code==200 && http.getSize()<=32768) payload=http.getString();
  http.end();
  return code==200 && payload.startsWith("#EXTM3U") && payload.length()<32769;
}

// Wynik parsowania playlisty MEDIA: maksymalnie 12 adresów segmentów,
// ich czasy EXTINF, numer sekwencji oraz flagi ENDLIST/nieobsługiwanej składni.
// MEDIA-LIST: kontener danych jednej odpowiedzi playlisty na żywo.
// sequence odpowiada EXT-X-MEDIA-SEQUENCE, a durationMs odpowiada EXTINF.
// Maksymalnie 12 segmentów; dodatkowych pozycji kod nie magazynuje.
struct MediaList {String urls[12]; uint32_t durationMs[12]={0}; int count=0; int64_t sequence=0; bool ended=false; bool unsupported=false;};
// Odczytuje playlistę MASTER i wybiera pierwszy wariant STREAM-INF.
// Zwraca pełny URL playlisty MEDIA, rozwijając URI względne.
/**
 * @brief Wyszukuje pierwszy wariant EXT-X-STREAM-INF w playliście MASTER.
 * @param source URL playlisty MASTER (baza dla ścieżek względnych).
 * @param body Tekst playlisty MASTER pobrany przez getText().
 * @param mediaUrl Wyznaczony pełny adres playlisty MEDIA.
 * @return true, gdy znaleziono i rozwinięto adres wariantu.
 * @note Funkcja nie ocenia jakości ani przepływności wielu wariantów.
 */
static bool parseMaster(const String& source, const String& body, String& mediaUrl) {
  bool expect=false;
  int p=0;
  while(p<(int)body.length()) {
    int end=body.indexOf('\n',p); if(end<0) end=body.length();
    String line=body.substring(p,end); line.trim(); p=end+1;
    if(line.startsWith("#EXT-X-STREAM-INF:")) {expect=true;continue;}
    if(expect && line.length() && line[0]!='#') { mediaUrl=absoluteUrl(source,line);return mediaUrl.length()>0; }
  }
  return false;
}
// Parsuje playlistę MEDIA: MEDIA-SEQUENCE, EXTINF, URI segmentów i ENDLIST.
// Dla bezpieczeństwa odrzuca szyfrowanie AES (EXT-X-KEY) i fMP4 (EXT-X-MAP),
// których ta wersja silnika nie dekoduje.
/**
 * @brief Przetwarza playlistę MEDIA zawierającą segmenty MPEG-TS.
 * @param mediaUrl URL playlisty MEDIA, baza dla URI segmentów.
 * @param body Treść m3u8 z numerem sekwencji i czasami EXTINF.
 * @param list Struktura wynikowa z adresami, czasami i flagami.
 * @return true, gdy rozpoznano co najmniej jeden segment.
 * @note Nie obsługuje szyfrowania EXT-X-KEY ani kontenera fMP4.
 */
static bool parseMedia(const String &mediaUrl, const String &body, MediaList &list) {
  if(body.indexOf("#EXT-X-KEY:")>=0 || body.indexOf("#EXT-X-MAP:")>=0) {
    list.unsupported=true; return false; // szyfrowanie / fMP4 poza tym testem
  }
  int p=0; bool expect=false; uint32_t nextDurationMs=0;
  while(p<(int)body.length()) {
    int end=body.indexOf('\n',p); if(end<0) end=body.length();
    String line=body.substring(p,end); line.trim(); p=end+1;
    if(line.startsWith("#EXT-X-MEDIA-SEQUENCE:")) list.sequence=atoll(line.substring(22).c_str());
    if(line.startsWith("#EXTINF:")) {
      // EXTINF określa czas segmentu; przepływność mierzymy z liczby bajtów TS i czasu.
      nextDurationMs=(uint32_t)(line.substring(8).toFloat()*1000.0f);
      expect=true;continue;
    }
    if(line=="#EXT-X-ENDLIST")list.ended=true;
    if(expect && line.length() && line[0]!='#') {
      if(list.count<12) {
        list.durationMs[list.count]=nextDurationMs;
        list.urls[list.count++]=absoluteUrl(mediaUrl,line);
      }
      expect=false;
    }
  }
  return list.count>0;
}

// Zadanie dekodujące na osobnym rdzeniu. Odczytuje MPEG-TS z kolejki,
// dekoduje AAC, steruje głośnością i odczytuje AudioInfo z Helix.
// Gdy zmieni się częstotliwość/kanały/głębia PCM, przekazuje nowy AudioInfo
// do VolumeStream, który propaguje parametry do I2SStream.
/**
 * @brief Zadanie FreeRTOS dekodujące MPEG-TS/AAC i wysyłające PCM do I2S.
 * @details Odczytuje porcje z kolejki, reguluje głośność i obserwuje
 * AudioInfo zgłaszane przez Helix. W razie zmiany częstotliwości, kanałów
 * lub rozdzielczości PCM aktualizuje VolumeStream/I2S.
 * @note Zadanie jest przypinane do rdzenia 1 i działa do zgłoszenia STOP.
 * @warning Zamykanie I2S musi poczekać na zakończenie tego zadania.
 */
static void decodingTask(void*) {
  playbackAlive=true;
  int lastVolume=-1;
  while(!stopRequested) {
    int v=wantedVolume;
    if(v!=lastVolume) {volumeOutput.setVolume(v/21.0f);lastVolume=v;}
    // Mały czas oczekiwania w QueueStream, żeby STOP pozostawał responsywny.
    size_t n=audioCopy.copy();
    if (n) {
      playedBytes += (uint32_t)n;
      // AudioInfo odczytujemy z dekodera AAC, nie z początkowej konfiguracji I2S.
      auto info = aac.audioInfo();
      if (info.sample_rate >= 8000 && info.sample_rate <= 96000 &&
          info.channels >= 1 && info.channels <= 2 &&
          (info.bits_per_sample == 16 || info.bits_per_sample == 24 || info.bits_per_sample == 32)) {
        decodedHz = (uint32_t)info.sample_rate;
        decodedChannels = info.channels;
        decodedBits = info.bits_per_sample;

        // Helix wykrywa format z nagłówka AAC. Aktualizujemy VolumeStream,
        // który przekazuje AudioInfo dalej do I2SStream (PCM5102A).
        // Zmiana następuje wyłącznie po wykryciu innego formatu, a nie przy każdej ramce.
        const bool formatChanged =
          activeI2SHz != (uint32_t)info.sample_rate ||
          activeI2SChannels != (uint8_t)info.channels ||
          activeI2SBits != (uint8_t)info.bits_per_sample;

        if (formatChanged) {
          const uint32_t oldRate = activeI2SHz;
          volumeOutput.setAudioInfo(info);
          activeI2SHz = (uint32_t)info.sample_rate;
          activeI2SChannels = (uint8_t)info.channels;
          activeI2SBits = (uint8_t)info.bits_per_sample;
          Serial.printf("[I2S] Automatyczna zmiana formatu: %lu -> %lu Hz, %u kan., %u bit\n",
            (unsigned long)oldRate, (unsigned long)activeI2SHz,
            (unsigned)activeI2SChannels, (unsigned)activeI2SBits);
        }
      }
    } else vTaskDelay(pdMS_TO_TICKS(5));
  }
  playbackAlive=false;
  playbackTask=nullptr;
  vTaskDelete(nullptr);
}

// HTTP segmentu wysyła MPEG-TS do bufora. Dekoder działa w drugim zadaniu.
// Pobiera jeden segment MPEG-TS strumieniowo, porcjami do lokalnego bloku.
// Każda porcja jest równolegle analizowana przez parser ID3 i zapisywana
// do kolejki dekodera. Pętle kontrolują stopRequested, aby pilot mógł przerwać
// pobieranie/zapis bez zwalniania zasobów używanych przez dekoder.
/**
 * @brief Pobiera pojedynczy segment MPEG-TS i umieszcza go w kolejce audio.
 * @param url Pełny adres segmentu; może zawierać wygasający token.
 * @return true tylko po pobraniu całego segmentu i braku żądania STOP.
 * @details Przetwarzanie danych odbywa się porcjami po maks. 1504 bajty.
 * Parser ID3 ogląda te same bajty równolegle; strumień AAC pozostaje bez zmian.
 * @note Odrzucane są odpowiedzi bez Content-Length lub z kodowaniem chunked.
 */
static bool downloadSegment(const String& url) {
  WiFiClientSecure tls; tls.setInsecure();tls.setTimeout(6000);
  HTTPClient http;http.setConnectTimeout(4500);http.setTimeout(6500);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  const char* wantedHeaders[]={"Transfer-Encoding","Content-Type"};
  http.collectHeaders(wantedHeaders,2);
  if(!http.begin(tls,url))return false;
  http.addHeader("Accept","video/MP2T,*/*");
  int code=http.GET(); // Wykonanie żądania HTTP; kod odpowiedzi zwracany wywołującemu.
  if(code!=200 && code!=206) {Serial.printf("[SEG] HTTP=%d\n",code);http.end();return false;}
  // Badane segmenty OpenFM zawierały Content-Length (nie transfer chunked).
  // Odrzucamy chunked, aby nie przekazać ramek HTTP do dekodera MPEG-TS.
  String encoding=http.header("Transfer-Encoding");
  if(encoding.indexOf("chunked")>=0) {
    Serial.println("[SEG] Chunked MPEG-TS wymaga innej sciezki; bezpiecznie pomijam.");
    http.end();return false;
  }
  WiFiClient* stream=http.getStreamPtr();
  int remaining=http.getSize();
  if(remaining<0) {Serial.println("[SEG] Brak Content-Length; pomijam segment.");http.end();return false;}
  uint8_t block[188*8];
  uint32_t lastData=millis();
  size_t received=0;
  while(!stopRequested && remaining>0) {
    int ready=stream->available();
    if(ready<=0) {
      if(!http.connected() || millis()-lastData>6500) break;
      vTaskDelay(pdMS_TO_TICKS(5));continue;
    }
    size_t want=(size_t)ready;
    if(want>sizeof(block))want=sizeof(block);
    if(want>(size_t)remaining)want=remaining;
    int n=stream->read(block,want);
    if(n<=0)break;
    remaining-=n;received+=n;lastData=millis();
    OpenFmMetadata::feed(block,(size_t)n); // Równoległy odczyt ID3 z MPEG-TS; dane audio pozostają niezmienione.
    // queue.write może chwilowo oczekiwać, aż dekoder zwolni miejsce.
    size_t pos=0;
    while(pos<(size_t)n && !stopRequested) {
      // Krótki zapis i limit oczekiwania w BufferRTOS umożliwiają szybki STOP.
      size_t chunk=min((size_t)376, (size_t)n-pos);
      size_t wrote=byteQueue.write(block+pos,chunk);
      if(!wrote) vTaskDelay(pdMS_TO_TICKS(5));
      else pos+=wrote;
    }
  }
  http.end();
  receivedBytes+=(uint32_t)received;
  if(remaining!=0 && !stopRequested)Serial.printf("[SEG] Niepelny segment (%u B pozostalo)\n",(unsigned)remaining);
  return !stopRequested && remaining==0 && received>0;
}

// Główne zadanie sesji HLS. Kolejność: token OpenFM -> MASTER -> MEDIA ->
// konfiguracja I2S i dekodera -> pobieranie kolejnych segmentów live.
// Po STOP czeka na zakończenie zadania dekodującego, dopiero potem zwalnia
// wspólne zasoby audio. Nie należy zwalniać I2S, gdy decodingTask nadal działa.
/**
 * @brief Nadzoruje całą sesję transmisji OpenFM HLS na rdzeniu 0.
 * @details Uzyskuje świeży token, pobiera playlistę MASTER, wybiera MEDIA,
 * inicjalizuje audio i śledzi EXT-X-MEDIA-SEQUENCE w transmisji na żywo.
 * Segmenty są kolejkowane do dekodera na drugim rdzeniu.
 * @note STOP jest asynchroniczny. Dopiero po zakończeniu decodingTask()
 * można bezpiecznie zakończyć dekoder i zwolnić interfejs I2S.
 * @warning Etykieta finish służy wyłącznie do wspólnego zakończenia sesji;
 * nie wolno przenosić deklaracji lokalnych w sposób naruszający zakresy C++.
 */
static void worker(void*) {
  statusCode=1;
  OpenFmResult token=resolveOpenFm(channelId);
  if(!token.success || stopRequested) {Serial.println("[OPENFM] Token/API: "+token.error);goto finish;}
  {
    String master,media,mediaUrl;
    int code=0;
    if(!getText(token.playlistUrl,master,code)) {
      Serial.printf("[OPENFM] MASTER HTTP=%d\n",code);goto finish;
    }
    if(!parseMaster(token.playlistUrl,master,mediaUrl)) {
      Serial.println("[OPENFM] Brak wariantu MASTER");goto finish;
    }
    Serial.println("[OPENFM] MASTER -> MEDIA poprawnie");
    // Audio najpierw: TS MTSDecoder(AAC) -> Volume -> PCM5102A
    {
      auto cfg=audioI2S.defaultConfig(TX_MODE);
      cfg.port_no=0; cfg.pin_bck=BCLK; cfg.pin_ws=LRCK; cfg.pin_data=DATA;
      cfg.sample_rate=44100;cfg.channels=2;cfg.bits_per_sample=16; // Format startowy; AAC może go później zmienić.
      // Uruchomienie sterownika I2S na porcie 0.
      if(!audioI2S.begin(cfg)){
        Serial.println("[I2S] Blad inicjalizacji");
        goto finish;
      }
      auto vc=volumeOutput.defaultConfig();vc.copyFrom(cfg);
      volumeOutput.begin(vc);
      if (!audioBuffer.resize(BUFFER_BYTES)) {
        Serial.println("[HLS] Nie mozna przydzielic bufora PSRAM");
        volumeOutput.end(); audioI2S.end(); goto finish;
      }
      // Domyślny portMAX_DELAY powodował zawieszanie STOP przy pełnym buforze.
      audioBuffer.setWriteMaxWait(pdMS_TO_TICKS(40));
      audioBuffer.setReadMaxWait(pdMS_TO_TICKS(40));
      audioBuffer.clear();
      byteQueue.begin(40);

      // Jawne połączenie powiadomień o zmianie formatu:
      // AAC/MTS -> EncodedAudioStream -> VolumeStream -> I2SStream.
      // Dzięki temu I2S może zmienić częstotliwość próbkowania, gdy Helix wykryje
      // np. 44100 Hz albo 48000 Hz. Dodatkowa kontrola w decodingTask()
      // stanowi zabezpieczenie, gdyby dana wersja MTSDecoder nie przekazywała
      // powiadomienia automatycznie.
      decoded.addNotifyAudioChange(volumeOutput); // Przekazuje zmianę AudioInfo z dekodera do wyjścia.
      activeI2SHz = (uint32_t)cfg.sample_rate;
      activeI2SChannels = (uint8_t)cfg.channels;
      activeI2SBits = (uint8_t)cfg.bits_per_sample;
      decoded.begin();
      if(xTaskCreatePinnedToCore(decodingTask,"OFM_DECODE",12288,nullptr,3,&playbackTask,1)!=pdPASS){ // Dekoder na rdzeniu 1.
        Serial.println("[AAC] Blad tworzenia zadania");
        decoded.end();volumeOutput.end();audioI2S.end();goto finish;
      }
      statusCode=2;
      int64_t lastSequence=-1;
      uint32_t lastRefresh=0;
      uint8_t errors=0;
      while(!stopRequested && WiFi.status()==WL_CONNECTED) {
        String body; int response=0;
        if(!getText(mediaUrl,body,response)) {
          Serial.printf("[MEDIA] HTTP=%d\n",response);
          if(++errors>=5)break;
          vTaskDelay(pdMS_TO_TICKS(1200));continue;
        }
        MediaList playlist;
        if(!parseMedia(mediaUrl,body,playlist)) {
          Serial.println("[MEDIA] Brak segmentow albo szyfrowanie/fMP4");break;
        }
        int beginIndex=0;
        if(lastSequence<0) beginIndex=max(0,playlist.count-2); // Rozpocznij od dwóch ostatnich segmentów, aby ograniczyć opóźnienie live.
        for(int i=beginIndex;i<playlist.count && !stopRequested;++i) {
          const int64_t seq=playlist.sequence+i;
          if(seq<=lastSequence)continue;
          uint32_t beforeBytes=receivedBytes;
          bool ok=downloadSegment(playlist.urls[i]);
          if(!ok) {
            Serial.printf("[MEDIA] Segment seq=%lld: blad\n",(long long)seq);
            if(++errors>=5)break;
            continue;
          }
          errors=0;lastSequence=seq;
          const uint32_t segmentBytes=receivedBytes-beforeBytes;
          latestSegmentBytes=segmentBytes; // Pełny rozmiar jednego ukończonego segmentu TS.
          segmentCount++;
          // Weryfikacja wartości ekranu: SEGMENT oznacza jeden plik TS,
          // a bytes w starszych komunikatach [AUDIO] to suma OD STARTU stacji.
          Serial.printf("[SEGMENT] nr=%lu bajty_segmentu=%lu B, suma_TS=%lu B\n",
            (unsigned long)segmentCount, (unsigned long)latestSegmentBytes,
            (unsigned long)receivedBytes);
          if (playlist.durationMs[i] >= 500) {
            uint32_t measured=(uint32_t)((uint64_t)segmentBytes*8ULL/playlist.durationMs[i]);
            if (measured>=8 && measured<1000) tsKbps=measured; // kb/s TS, nie samego AAC
          }
          Serial.printf("[AUDIO] seg=%lu bytes=%lu dekoder=%lu\n",
            (unsigned long)segmentCount,(unsigned long)receivedBytes,(unsigned long)playedBytes);
          if(segmentCount==1 || (segmentCount%5)==0) OpenFmMetadata::diagnostics();
        }
        if(errors>=5 || playlist.ended)break;
        // Playlista live: co około 2 sekundy szukamy nowszych numerów sekwencji.
        uint32_t pause=millis()-lastRefresh;
        if(pause<1800) vTaskDelay(pdMS_TO_TICKS(1800-pause));
        lastRefresh=millis();
      }
      stopRequested=true;
      // Dekoder może jeszcze obrabiać ostatni fragment.
      uint32_t waitStart=millis();
      while(playbackAlive && millis()-waitStart<4500) vTaskDelay(pdMS_TO_TICKS(30));
      if(playbackAlive) {
        // Nie zwalniamy I2S ani dekodera, dopóki zadanie nadal pracuje!
        // Nie zwalniamy współdzielonych zasobów, jeżeli dekoder nadal pracuje.
        // Utrzymujemy busy=true zamiast resetować urządzenie lub zwalniać I2S.
        Serial.println("[HLS] STOP oczekuje na dekoder. Zasoby pozostaja zajete.");
        while(playbackAlive) vTaskDelay(pdMS_TO_TICKS(50));
      }
      decoded.end();volumeOutput.end();audioI2S.end();audioBuffer.clear();
    }
  }
finish:
  statusCode=stopRequested?0:3;
  running=false;
  channelId="";
  controlTask=nullptr;
  Serial.println("[HLS] Sesja zakonczona; mozna zmienic stacje.");
  vTaskDelete(nullptr);
}

// Publiczny start odtwarzania. Waliduje kanał i Wi-Fi, zeruje statystyki
// oraz metadane, po czym uruchamia zadanie worker na rdzeniu 0.
// Zwraca false, jeśli radio już pracuje, brak Wi-Fi lub nie udało się utworzyć zadania.
/**
 * @brief Uruchamia asynchroniczne odtwarzanie wskazanego kanału OpenFM.
 * @param channel Identyfikator stacji w postaci OFMxxx (np. OFM149).
 * @param volume Żądany poziom głośności; ograniczany do zakresu 0..21.
 * @return true po pomyślnym utworzeniu zadania worker; nie gwarantuje
 * jeszcze poprawnego połączenia HLS, które odbywa się później.
 * @note Zeruje poprzednie liczniki i metadane nowej sesji.
 */
bool start(const String &channel,int volume) {
  if(running) return false;
  if(!WiFi.isConnected())return false;
  if(channel.length()<4 || !channel.startsWith("OFM"))return false;
  channelId=channel;wantedVolume=constrain(volume,0,21);
  OpenFmMetadata::reset();
  receivedBytes=playedBytes=segmentCount=latestSegmentBytes=0;
  tsKbps=decodedHz=0;decodedChannels=decodedBits=0;
  activeI2SHz=0;activeI2SChannels=activeI2SBits=0;
  stopRequested=false;running=true;statusCode=1;
  if(xTaskCreatePinnedToCore(worker,"OFM_HTTP",16384,nullptr,2,&controlTask,0)!=pdPASS) {
    running=false;statusCode=3;return false;
  }
  return true;
}
// Asynchroniczne żądanie zatrzymania. Nie czeka na HTTP ani dekoder;
// worker i decodingTask kończą pracę bezpiecznie w swoich pętlach.
/** @brief Zgłasza asynchroniczne żądanie zatrzymania strumienia.
 * @note Nie zwalnia zasobów natychmiast — kończą to zadania robocze.
 */
void stop(){if(running){stopRequested=true;statusCode=4;Serial.println("[HLS] STOP przyjety");}}
// Informuje, czy sesja worker nadal zajmuje zasoby odtwarzacza.
/** @brief Sprawdza, czy sesja nadal zajmuje zasoby odtwarzacza. */
bool busy(){return running;}
// Ustawia żądaną głośność w skali 0..21; faktyczną zmianę wykonuje decodingTask.
/** @brief Ustawia głośność 0..21; dekoder zastosuje ją w swoim zadaniu. */
void setVolume(int level){wantedVolume=constrain(level,0,21);}
// Tekstowy status sesji dla ekranu i diagnostyki.
/** @brief Zwraca krótki, polski opis bieżącego stanu odtwarzania. */
String state(){switch(statusCode){case 1:return "LACZENIE";case 2:return "ODTWARZANIE";case 3:return "BLAD";case 4:return "ZATRZYMYWANIE";default:return "STOP";}}
// --- Odczyt statystyk i parametrów na potrzeby interfejsu TFT ---------------
/** @brief Liczba bajtów pobranych z kolejki do dekodera w bieżącej sesji. */
uint32_t audioBytes(){return playedBytes;}
/** @brief Liczba ukończonych pobrań segmentów MPEG-TS. */
uint32_t segments(){return segmentCount;}
/** @brief Rozmiar ostatniego ukończonego segmentu (w bajtach). */
uint32_t lastSegmentBytes(){return latestSegmentBytes;}
/** @brief Orientacyjna przepływność całego kontenera MPEG-TS w kb/s.
 * @note Nie jest to zmierzony bitrate samej warstwy AAC.
 */
uint32_t bitrateKbps(){return tsKbps;}
/** @brief Częstotliwość próbkowania wykryta z AAC, w Hz. */
uint32_t sampleRate(){return decodedHz;}
/** @brief Liczba kanałów PCM zgłaszana przez dekoder. */
uint8_t channels(){return decodedChannels;}
/** @brief Głębia próbek wyjściowego PCM; nie bitowość stratnego AAC. */
uint8_t bitsPerSample(){return decodedBits;}
// Wypisuje diagnostykę parsera ID3, liczbę segmentów, transfer/dekoder
// oraz bieżące zasoby RAM/PSRAM. Nie ujawnia tokenów HLS.
/**
 * @brief Wypisuje zbiorczą diagnostykę sesji do portu szeregowego.
 * @details Raportuje liczniki, RAM/PSRAM i stan bez ujawniania URL z tokenem.
 */
void stats(){OpenFmMetadata::diagnostics();Serial.printf("[HLS] status=%s seg=%lu NET=%lu DECODE=%lu RAM=%u PSRAM=%u\n",
 state().c_str(),(unsigned long)segmentCount,(unsigned long)receivedBytes,(unsigned long)playedBytes,
 (unsigned)ESP.getFreeHeap(),(unsigned)ESP.getFreePsram());}
}
