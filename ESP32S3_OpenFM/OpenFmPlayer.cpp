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

namespace OpenFmPlayer {
static constexpr int BCLK=16, DATA=17, LRCK=18;
static constexpr size_t BUFFER_BYTES=96*1024;
static volatile bool running=false, stopRequested=false;
static volatile int wantedVolume=14;
static volatile uint32_t playedBytes=0, receivedBytes=0, segmentCount=0;
static volatile uint32_t latestSegmentBytes=0; // rozmiar ostatniego kompletnego segmentu
static volatile uint32_t tsKbps=0, decodedHz=0;            // parametry raportowane ekranowi
static volatile uint8_t decodedChannels=0, decodedBits=0; // odczyt z dekodera Helix
static volatile uint8_t statusCode=0; // 0=STOP,1=START,2=GRA,3=ERROR,4=STOPPING
static String channelId;
static TaskHandle_t controlTask=nullptr, playbackTask=nullptr;
static volatile bool playbackAlive=false;

static I2SStream audioI2S;
static VolumeStream volumeOutput(audioI2S);
static AACDecoderHelix aac;
static MTSDecoder tsDecoder(aac);
static EncodedAudioStream decoded(&volumeOutput,&tsDecoder);
static BufferRTOS<uint8_t> audioBuffer(0);
static QueueStream<uint8_t> byteQueue(audioBuffer);
static StreamCopy audioCopy(decoded,byteQueue);

// URL zawieraja tokeny. Nigdy ich nie wypisujemy w logach.
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

static bool getText(const String& url, String& payload, int& code) {
  WiFiClientSecure tls; tls.setInsecure(); // wyłącznie wersja testowa
  tls.setTimeout(6500);
  HTTPClient http; http.setConnectTimeout(4500); http.setTimeout(6500);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if(!http.begin(tls,url)) {code=-1;return false;}
  http.addHeader("Accept","application/vnd.apple.mpegurl,application/x-mpegURL,*/*");
  code=http.GET();
  if(code==200 && http.getSize()<=32768) payload=http.getString();
  http.end();
  return code==200 && payload.startsWith("#EXTM3U") && payload.length()<32769;
}

struct MediaList {String urls[12]; uint32_t durationMs[12]={0}; int count=0; int64_t sequence=0; bool ended=false; bool unsupported=false;};
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
      // EXTINF podaje dlugosc segmentu. Bitrate mierzymy z bajtow TS / ten czas.
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
      // AudioInfo pochodzi z faktycznego dekodera AAC, a nie z ustawien I2S.
      auto info = aac.audioInfo();
      if (info.sample_rate >= 8000 && info.sample_rate <= 192000) {
        decodedHz = (uint32_t)info.sample_rate;
        decodedChannels = info.channels;
        decodedBits = info.bits_per_sample;
      }
    } else vTaskDelay(pdMS_TO_TICKS(5));
  }
  playbackAlive=false;
  playbackTask=nullptr;
  vTaskDelete(nullptr);
}

// HTTP segmentu wysyła MPEG-TS do bufora. Dekoder działa w drugim zadaniu.
static bool downloadSegment(const String& url) {
  WiFiClientSecure tls; tls.setInsecure();tls.setTimeout(6000);
  HTTPClient http;http.setConnectTimeout(4500);http.setTimeout(6500);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  const char* wantedHeaders[]={"Transfer-Encoding","Content-Type"};
  http.collectHeaders(wantedHeaders,2);
  if(!http.begin(tls,url))return false;
  http.addHeader("Accept","video/MP2T,*/*");
  int code=http.GET();
  if(code!=200 && code!=206) {Serial.printf("[SEG] HTTP=%d\n",code);http.end();return false;}
  // Segmenty OpenFM w probie mialy Content-Length (nie chunked).
  // Przy chunked odrzucamy transfer, zamiast przeslac ramki HTTP do dekodera MPEG-TS.
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
    OpenFmMetadata::feed(block,(size_t)n); // niezalezny od dekodera odczyt ID3 z PID MPEG-TS
    // queue.write może chwilowo oczekiwać, aż dekoder zwolni miejsce.
    size_t pos=0;
    while(pos<(size_t)n && !stopRequested) {
      // Krotki zapis + limit oczekiwania w BufferRTOS umozliwia szybki STOP.
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
      cfg.sample_rate=44100;cfg.channels=2;cfg.bits_per_sample=16;
      if(!audioI2S.begin(cfg)){Serial.println("[I2S] Blad inicjalizacji");goto finish;}
      auto vc=volumeOutput.defaultConfig();vc.copyFrom(cfg);
      volumeOutput.begin(vc);
      if (!audioBuffer.resize(BUFFER_BYTES)) {
        Serial.println("[HLS] Nie mozna przydzielic bufora PSRAM");
        volumeOutput.end(); audioI2S.end(); goto finish;
      }
      // Domyslny portMAX_DELAY powodowal zawieszanie STOP przy pelnym buforze.
      audioBuffer.setWriteMaxWait(pdMS_TO_TICKS(40));
      audioBuffer.setReadMaxWait(pdMS_TO_TICKS(40));
      audioBuffer.clear();
      byteQueue.begin(40);
      decoded.begin();
      if(xTaskCreatePinnedToCore(decodingTask,"OFM_DECODE",12288,nullptr,3,&playbackTask,1)!=pdPASS){
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
        if(lastSequence<0) beginIndex=max(0,playlist.count-2); // start od 2 ostatnich segmentow
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
          latestSegmentBytes=segmentBytes; // pelny rozmiar JEDNEGO segmentu TS
          segmentCount++;
          // Weryfikacja wartosci ekranu: SEGMENT = jeden plik TS,
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
        // Playlista live; co ~2 s szukamy nowszych numerow sekwencji.
        uint32_t pause=millis()-lastRefresh;
        if(pause<1800) vTaskDelay(pdMS_TO_TICKS(1800-pause));
        lastRefresh=millis();
      }
      stopRequested=true;
      // Dekoder może jeszcze obrabiać ostatni fragment.
      uint32_t waitStart=millis();
      while(playbackAlive && millis()-waitStart<4500) vTaskDelay(pdMS_TO_TICKS(30));
      if(playbackAlive) {
        // Nie zwalniamy I2S/dekodera przy wciaz dzialajacym zadaniu!
        // Nie zwalniamy wspoldzielonych zasobow, jezeli dekoder nadal pracuje.
        // Podtrzymujemy busy=true, zamiast resetowac urzadzenie albo zwalniac I2S.
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

bool start(const String &channel,int volume) {
  if(running) return false;
  if(!WiFi.isConnected())return false;
  if(channel.length()<4 || !channel.startsWith("OFM"))return false;
  channelId=channel;wantedVolume=constrain(volume,0,21);
  OpenFmMetadata::reset();
  receivedBytes=playedBytes=segmentCount=latestSegmentBytes=0;
  tsKbps=decodedHz=0;decodedChannels=decodedBits=0;
  stopRequested=false;running=true;statusCode=1;
  if(xTaskCreatePinnedToCore(worker,"OFM_HTTP",16384,nullptr,2,&controlTask,0)!=pdPASS) {
    running=false;statusCode=3;return false;
  }
  return true;
}
void stop(){if(running){stopRequested=true;statusCode=4;Serial.println("[HLS] STOP przyjety");}}
bool busy(){return running;}
void setVolume(int level){wantedVolume=constrain(level,0,21);}
String state(){switch(statusCode){case 1:return "LACZENIE";case 2:return "ODTWARZANIE";case 3:return "BLAD";case 4:return "ZATRZYMYWANIE";default:return "STOP";}}
uint32_t audioBytes(){return playedBytes;}
uint32_t segments(){return segmentCount;}
uint32_t lastSegmentBytes(){return latestSegmentBytes;}
uint32_t bitrateKbps(){return tsKbps;}
uint32_t sampleRate(){return decodedHz;}
uint8_t channels(){return decodedChannels;}
uint8_t bitsPerSample(){return decodedBits;}
void stats(){OpenFmMetadata::diagnostics();Serial.printf("[HLS] status=%s seg=%lu NET=%lu DECODE=%lu RAM=%u PSRAM=%u\n",
 state().c_str(),(unsigned long)segmentCount,(unsigned long)receivedBytes,(unsigned long)playedBytes,
 (unsigned)ESP.getFreeHeap(),(unsigned)ESP.getFreePsram());}
}
