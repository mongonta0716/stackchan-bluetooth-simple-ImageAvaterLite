// スタックチャンをBluetoothスピーカーとして使うサンプルアプリです。
// https://github.com/m5stack/M5Unified のBluetooth_with_ESP32A2DP.inoをベースにサーボの動きを追加しています。
// Copyright (c) 2022 Takao Akaki

#include <Arduino.h>

//#include <Ticker.h>
#include <SD.h>
#include <M5Unified.h>
#ifdef USE_TCPNOTIFY
#include <WiFi.h>
#endif
#ifdef USE_UARTNOTIFY
#include "Stackchan_AppConfig.h"
#endif
//#include <WiFiUdp.h>
//#include <esp_wifi.h>
#include "Stackchan_servo.h"
#include "BluetoothA2DPSink_M5Speaker.hpp"
#include "M5ImageAvatarLite.h"
#include "ImageAvatarSystemConfig.h"
//#include "Stackchan_Takao_Base.hpp"

using namespace m5imageavatar;
M5GFX &gfx(M5.Lcd);
StackchanSERVO servo;

#include "Stackchan_system_config.h"

// LEDレベルメーターを使わない場合は下記の1行をコメントアウトしてください。
// USE_TCPNOTIFY(WiFi+TCP通知)を有効にした場合、M5Stack Core2ではIRAM不足でビルドできないため
// USE_LEDは自動的に無効化されます。(platformio.iniのbuild_flagsでUSE_TCPNOTIFYを指定してください。)
// USE_UARTNOTIFY(UART通知)はWiFi/Bluetoothの共存コードを使わないためIRAMをほとんど消費せず、
// USE_LEDと併用できます。
#if !defined(USE_TCPNOTIFY)
#define USE_LED
#endif
// Port.B(GPIO26)に接続したNekomimi LED（左右9灯、計18灯）を使用します。
// M5Go Bottomの内蔵LEDを使う場合はコメントアウトしてください。
#define USE_NECOMIMI_LED
//#define USE_LED_OUT

#ifdef USE_LED
  #include <FastLED.h>
#ifdef USE_NECOMIMI_LED
  #define NUM_LEDS 18
  #define LED_PIN 26
  static constexpr uint8_t LEDS_PER_EAR = NUM_LEDS / 2;
  CRGB leds[NUM_LEDS];
  const CRGB led_table[LEDS_PER_EAR] = {
    CRGB::Purple, CRGB::MediumPurple, CRGB::Blue,
    CRGB::Green, CRGB::LimeGreen, CRGB::Yellow,
    CRGB::DarkOrange, CRGB::OrangeRed, CRGB::Red
  };
#else
  #define NUM_LEDS 10
  #define NUM_LED_OUT 55
#if defined(ARDUINO_M5STACK_FIRE) || defined(ARDUINO_M5Stack_Core_ESP32)
  // M5Core1 + M5GoBottom1の組み合わせ
  #define LED_PIN 15
#else
  // M5Core2 + M5GoBottom2の組み合わせ
  #define LED_PIN 25
#endif
  CRGB leds[NUM_LEDS];
  #ifdef USE_LED_OUT
  CRGB leds_out[NUM_LED_OUT];
  #endif

  CHSV red (0, 255, 255);
  CHSV green (95, 255, 255);
  CHSV blue (160, 255, 255);
  CHSV magenta (210, 255, 255);
  CHSV yellow (45, 255, 255);
  CHSV hsv_table[5] = { blue, green, yellow, magenta, red };
  CHSV hsv_table_out[5] = { blue, green, yellow, magenta, red }; //red, magenta, yellow, green, blue };
#endif

  void turn_off_led() {
    // Now turn the LED off, then pause
    for(int i=0;i<NUM_LEDS;i++) leds[i] = CRGB::Black;
    FastLED.show();  
  }

  void clear_led_buff() {
    // Now turn the LED off, then pause
    for(int i=0;i<NUM_LEDS;i++) leds[i] =  CRGB::Black;
  }

  void level_led(int level1, int level2) {  
#ifdef USE_NECOMIMI_LED
    level1 = constrain(level1, 0, LEDS_PER_EAR);
    level2 = constrain(level2, 0, LEDS_PER_EAR);

    clear_led_buff();
    // 各耳は両端から中央へ向かって対称に点灯します。
    for (uint8_t i = 0; i < level1; ++i) {
      leds[i] = led_table[i];
      leds[LEDS_PER_EAR - 1 - i] = led_table[i];
    }
    for (uint8_t i = 0; i < level2; ++i) {
      leds[LEDS_PER_EAR + i] = led_table[i];
      leds[NUM_LEDS - 1 - i] = led_table[i];
    }
#else
  if(level1 > 5) level1 = 5;
  if(level2 > 5) level2 = 5;
    
    clear_led_buff(); 
    for(int i=0;i<level1;i++){
      fill_gradient(leds, 0, hsv_table[i], 4, hsv_table[0] );
      #ifdef USE_LED_OUT
      fill_gradient(leds_out, 0, hsv_table_out[0], 18, hsv_table_out[i] );
      #endif
    }
    for(int i=0;i<level2;i++){
      fill_gradient(leds, 5, hsv_table[0], 9, hsv_table[i] );
      #ifdef USE_LED_OUT
      fill_gradient(leds_out, 19, hsv_table_out[i], 36, hsv_table_out[0] );
      #endif
    }
#endif
    FastLED.show();
  }
#endif



fs::FS json_fs = SD; // JSONファイルの収納場所(SPIFFS or SD)
fs::FS avatar_yaml_fs = SD;
fs::FS avatar_bmp_fs = SD;
StackchanSystemConfig system_config;
ImageAvatarSystemConfig avatar_system_config;
const char* avatar_system_yaml = "/yaml/M5AvatarLiteSystem.yaml";
uint8_t avatar_count = 0;
ImageAvatarLite avatar(avatar_yaml_fs, avatar_bmp_fs);
// ImageAvatarLiteは渡されたファイル名ポインタを表情変更時にも再利用するため、
// setup()終了後もc_str()の参照先が有効なグローバルStringで保持します。
String current_avatar_filename;
String avatar_config_error;
portMUX_TYPE avatar_event_mux = portMUX_INITIALIZER_UNLOCKED;
int8_t pending_avatar_expression = -1;

// 付属のslimeアバターでは normal / sad / angry の順に表情を定義しています。
static constexpr uint8_t IMAGE_EXPRESSION_NORMAL = 0;
static constexpr uint8_t IMAGE_EXPRESSION_SAD = 1;
static constexpr uint8_t IMAGE_EXPRESSION_ANGRY = 2;

// Bluetoothや通知のコールバックからsetExpression()を直接呼ぶと、
// ImageAvatarLiteが描画タスクを停止した状態でSD/YAML処理を行ってしまいます。
// 非同期タスクからは要求だけを保存し、loop()で安全に反映します。
void requestAvatarExpression(uint8_t expression) {
  portENTER_CRITICAL(&avatar_event_mux);
  pending_avatar_expression = expression;
  portEXIT_CRITICAL(&avatar_event_mux);
}

void processPendingAvatarExpression() {
  int8_t expression = -1;
  portENTER_CRITICAL(&avatar_event_mux);
  expression = pending_avatar_expression;
  pending_avatar_expression = -1;
  portEXIT_CRITICAL(&avatar_event_mux);

  if (expression >= 0) {
    avatar.setExpression(static_cast<uint8_t>(expression));
  }
}

void haltWithAvatarConfigError(const String& message) {
  Serial.printf("Image avatar configuration error: %s\n", message.c_str());
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(8, 16);
  M5.Lcd.println("Image avatar config error");
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.println(message);
  for (;;) {
    M5.update();
    delay(100);
  }
}

bool validateAvatarConfig(fs::FS& fs, const char* filename) {
  File file = fs.open(filename);
  if (!file) {
    avatar_config_error = String("File not found: ") + filename;
    return false;
  }

  // M5StackImageAvatarLib also uses a 4096-byte DynamicJsonDocument.
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeYml(doc, file);
  file.close();
  if (error || doc.overflowed()) {
    avatar_config_error = String("YAML parse failed: ") + filename;
    if (error) {
      avatar_config_error += String(" (") + error.c_str() + ")";
    }
    return false;
  }

  const char* skin = doc["color_info"]["skin"];
  const char* eye_white = doc["color_info"]["eye_white"];
  const char* transparent = doc["color_info"]["transparent"];
  JsonArray expressions = doc["expression"];
  if (!skin || !eye_white || !transparent || expressions.size() < 3
      || doc["fixed_parts"][0]["filename"].isNull()
      || doc["mouth"].size() < expressions.size()
      || doc["eye"].size() < expressions.size()
      || doc["init_param"].size() < expressions.size()) {
    avatar_config_error = String("Required fields are missing: ") + filename;
    return false;
  }
  return true;
}

#ifdef USE_TCPNOTIFY
// --------------------
// TCP通知(ピンポン)関連の設定
const uint16_t TCP_NOTIFY_PORT = 8080;         // 通知を待ち受けるTCPポート
const char* TCP_NOTIFY_MESSAGE = "notify";     // このメッセージを受信したときだけピンポン音を鳴らす
const uint32_t WIFI_CONNECT_TIMEOUT = 15000;   // WiFi接続を待つ時間(msec)

WiFiServer tcp_notify_server(TCP_NOTIFY_PORT);
bool wifi_connected = false;
// TCP通知関連の設定 end
// --------------------
#endif // USE_TCPNOTIFY

#ifdef USE_UARTNOTIFY
// --------------------
// UART通知(ピンポン)関連の設定
const char* UART_APPCONFIG_PATH = "/app/SCBluetooth/SC_AppConfig.xml"; // RxPin/TxPin/Baudrateの設定ファイル
const char* UART_NOTIFY_MESSAGE = "notify"; // このメッセージを受信したときだけピンポン音を鳴らす

UartNotifyConfig uart_notify_config;
// UART通知関連の設定 end
// --------------------
#endif // USE_UARTNOTIFY

const unsigned long powericon_interval = 3000;  // バッテリーアイコンを更新する間隔(msec)
unsigned long last_powericon_millis = 0;

bool bluetooth_mode = false;

// --------------------
// Avatar関連の初期設定
#define LIPSYNC_LEVEL_MAX 10.0f
static float lipsync_level_max = LIPSYNC_LEVEL_MAX; // リップシンクの上限初期値
float mouth_ratio = 0.0f;
// Avatar関連の設定 end
// --------------------

uint32_t last_discharge_time = 0;  // USB給電が止まったときの時間(msec)

/// set M5Speaker virtual channel (0-7)
static constexpr uint8_t m5spk_virtual_channel = 0;

static BluetoothA2DPSink_M5Speaker a2dp_sink = { &M5.Speaker, m5spk_virtual_channel };
void hvt_event_callback(int avatar_expression, const char* text);
void avrc_metadata_callback(uint8_t data1, const uint8_t *data2);
void startBluetooth() {
  a2dp_sink.set_avrc_metadata_callback(avrc_metadata_callback);
  a2dp_sink.setHvtEventCallback(hvt_event_callback);
  a2dp_sink.start(system_config.getBluetoothSetting()->device_name.c_str(),
                  true);
  avatar.setExpression(IMAGE_EXPRESSION_SAD);
  avatar.setSpeechText("Bluetooth Mode");
  bluetooth_mode = true;
}
static fft_t fft;
static constexpr size_t WAVE_SIZE = 320;
static int16_t raw_data[WAVE_SIZE * 2];
static constexpr long SERVO_IDLE_RANDOM_ANGLE = 15;
// 歌うモードでX軸をスイングさせる段階数と、正面からの最大振れ幅(±°)。
static constexpr long SERVO_SINGING_SWING_STEPS = 5;
static constexpr long SERVO_SINGING_SWING_MAX_ANGLE = 15;

void servoLoop(void *args) {
  DriveContext *ctx = (DriveContext *)args;
  ImageAvatarLite *avatar = ctx->getAvatar();
  long move_time = 0;
  long interval_time = 0;
  long move_x = 0;
  long move_y = 0;
  bool sing_mode = false;
  for (;;) {
    if (mouth_ratio == 0.0f) {
      // 待機時の動き
      interval_time = random(system_config.getServoInterval(AvatarMode::NORMAL)->interval_min
                           , system_config.getServoInterval(AvatarMode::NORMAL)->interval_max);
      move_time = random(system_config.getServoInterval(AvatarMode::NORMAL)->move_min
                       , system_config.getServoInterval(AvatarMode::NORMAL)->move_max);
      lipsync_level_max = LIPSYNC_LEVEL_MAX; // リップシンク上限の初期化
      sing_mode = false;

    } else {
      // 歌うモードの動き
      interval_time = random(system_config.getServoInterval(AvatarMode::SINGING)->interval_min
                           , system_config.getServoInterval(AvatarMode::SINGING)->interval_max);
      move_time = random(system_config.getServoInterval(AvatarMode::SINGING)->move_min
                       , system_config.getServoInterval(AvatarMode::SINGING)->move_max);
      sing_mode = true;
    } 
    // ImageAvatarLiteには視線座標がないため、口の開きに合わせて動かします。
    const long start_x = system_config.getServoInfo(AXIS_X)->start_degree;
    // Y軸は90°から上にスイング（最大35°）
    move_y = system_config.getServoInfo(AXIS_Y)->start_degree - mouth_ratio * 10;
    if (!sing_mode) {
      // 待機中はX/Yとも初期位置を中心に±15°の範囲でランダムに動きます。
      move_x = start_x + random(-SERVO_IDLE_RANDOM_ANGLE, SERVO_IDLE_RANDOM_ANGLE + 1);
      move_y += random(-SERVO_IDLE_RANDOM_ANGLE, SERVO_IDLE_RANDOM_ANGLE + 1);
    } else {
      // X軸は音量(mouth_ratio)を5段階に量子化した振幅で、正面のstart_degreeから
      // 最大±15°の範囲を、毎回ランダムな向きにスイングさせます。
      const long swing_level =
          constrain(static_cast<long>(ceilf(mouth_ratio / 1.2f * SERVO_SINGING_SWING_STEPS)),
                    1L, SERVO_SINGING_SWING_STEPS);
      const long swing_amplitude =
          swing_level * SERVO_SINGING_SWING_MAX_ANGLE / SERVO_SINGING_SWING_STEPS;
      const long swing_dir = (random(2) == 0) ? -1 : 1;
      move_x = start_x + swing_dir * swing_amplitude;
    }
    move_x = constrain(move_x,
                       static_cast<long>(system_config.getServoInfo(AXIS_X)->lower_limit),
                       static_cast<long>(system_config.getServoInfo(AXIS_X)->upper_limit));
    move_y = constrain(move_y,
                       static_cast<long>(system_config.getServoInfo(AXIS_Y)->lower_limit),
                       static_cast<long>(system_config.getServoInfo(AXIS_Y)->upper_limit));
    servo.moveXY(move_x, move_y, move_time);
    if (!bluetooth_mode) {
      int lyric_no = random(system_config.getLyrics_num());
      int exp2 = random(2);
      avatar->setMouthOpenRatio(1.0f);
      avatar->setSpeechText((const char*)system_config.getLyric(lyric_no)->c_str());
      if (exp2 % 2) {
        requestAvatarExpression(IMAGE_EXPRESSION_NORMAL);
      } else {
        requestAvatarExpression(IMAGE_EXPRESSION_NORMAL);
      }
      avatar->setMouthOpenRatio(0.5f);
    }
    //avatar.setExpression((Expression)exp);
    if (sing_mode) {
      // 歌っているときはうなずく
      servo.moveXY(move_x, move_y + 10, 400);
    }
    vTaskDelay(interval_time/portTICK_PERIOD_MS);
  }
  vTaskDelete(NULL);
}


void lipSync(void *args)
{
  DriveContext *ctx = (DriveContext *)args;
  ImageAvatarLite *avatar = ctx->getAvatar();
  for (;;)
  {
    uint64_t level = 0;
    auto buf = a2dp_sink.getBuffer();
    if (buf) {
      // Bluetooth側が更新するバッファを先にスナップショットし、FFTとLEDで共用します。
      memcpy(raw_data, buf, WAVE_SIZE * 2 * sizeof(int16_t));
#ifdef USE_LED
      uint32_t peak_left = 0;
      uint32_t peak_right = 0;
      for (size_t i = 0; i < WAVE_SIZE; ++i) {
        peak_left = max(peak_left, static_cast<uint32_t>(abs(static_cast<int32_t>(raw_data[i * 2]))));
        peak_right = max(peak_right, static_cast<uint32_t>(abs(static_cast<int32_t>(raw_data[i * 2 + 1]))));
      }
      // 約50%の振幅で全点灯する感度。level_led側で灯数を上限に丸めます。
      const int led_level_left = peak_left * NUM_LEDS / INT16_MAX;
      const int led_level_right = peak_right * NUM_LEDS / INT16_MAX;
      switch(system_config.getLedLR()) {
        case 1: // Left Only
          level_led(led_level_left, led_level_left);
          break;
        case 2: // Right Only
          level_led(led_level_right, led_level_right);
          break;
        default: // Stereo
          level_led(led_level_right, led_level_left);
          break;
      }
#endif

      fft.exec(raw_data);
      for (size_t bx = 5; bx <= 60; ++bx) { // リップシンクで抽出する範囲はここで指定(低音)0〜64（高音）
        int32_t f = fft.get(bx);
        level += abs(f);
        //Serial.printf("bx:%d, f:%d\n", bx, f) ;
      }
      //Serial.printf("level:%d\n", level >> 16);
    }

    // スレッド内でログを出そうとすると不具合が起きる場合があります。
    //Serial.printf("data=%d\n\r", level >> 16);
    mouth_ratio = (float)(level >> 16)/lipsync_level_max;
    if (mouth_ratio > 1.2f) {
      if (mouth_ratio > 1.5f) {
        lipsync_level_max += 10.0f; // リップシンク上限を大幅に超えるごとに上限を上げていく。
      }
      mouth_ratio = 1.2f;
    }
    avatar->setMouthOpenRatio(mouth_ratio);
    vTaskDelay(30/portTICK_PERIOD_MS);
  }
  vTaskDelete(NULL);
}

void hvt_event_callback(int avatar_expression, const char* text) {
  // ESP32-A2DP側はM5Stack-Avatar互換の表情番号を通知するため、
  // 付属画像で表現できる normal / sad / angry に変換します。
  uint8_t image_expression = IMAGE_EXPRESSION_NORMAL;
  if (avatar_expression == 1) {
    image_expression = IMAGE_EXPRESSION_ANGRY;
  } else if (avatar_expression == 2) {
    image_expression = IMAGE_EXPRESSION_SAD;
  }
  requestAvatarExpression(image_expression);
}

void avrc_metadata_callback(uint8_t data1, const uint8_t *data2)
{
  Serial.printf("AVRC metadata rsp: attribute id 0x%x, %s\n", data1, data2);
  requestAvatarExpression(IMAGE_EXPRESSION_NORMAL);
}

#if defined(USE_TCPNOTIFY) || defined(USE_UARTNOTIFY)
// ドアチャイム風の「ピンポン」音を鳴らします。
void playPinPon() {
  M5.Speaker.tone(880, 250);  // Pin
  delay(280);
  M5.Speaker.tone(659, 400);  // Pon
  delay(400);
}

// 通知を受信したときの処理(音とアバターの反応)
void handleNotify() {
  Serial.println("Notify received. Play PinPon.");
  playPinPon();
  requestAvatarExpression(IMAGE_EXPRESSION_NORMAL);
  avatar.setSpeechText("ピンポーン");
  delay(2000);
  avatar.setSpeechText("");
  requestAvatarExpression(IMAGE_EXPRESSION_NORMAL);
}
#endif // USE_TCPNOTIFY || USE_UARTNOTIFY

#ifdef USE_TCPNOTIFY
// SC_SecConfig.yamlに書かれたWiFi情報を使って接続します。
bool connectWiFi() {
  wifi_s* wifi_info = system_config.getWiFiSetting();
  if (wifi_info->ssid.length() == 0) {
    Serial.println("WiFi SSID is not set in SC_SecConfig.yaml. Skip WiFi connection.");
    return false;
  }
  Serial.printf("Connecting to WiFi SSID:%s\n", wifi_info->ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_info->ssid.c_str(), wifi_info->password.c_str());
  uint32_t start_millis = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if ((millis() - start_millis) > WIFI_CONNECT_TIMEOUT) {
      Serial.println("WiFi connection timed out.");
      return false;
    }
    delay(200);
    Serial.print(".");
  }
  Serial.printf("\nWiFi connected. IP:%s\n", WiFi.localIP().toString().c_str());
  return true;
}

// TCPサーバーとして通知を待ち受けるタスク
void tcpNotifyTask(void *args) {
  tcp_notify_server.begin();
  Serial.printf("TCP notify server listening on port:%d\n", TCP_NOTIFY_PORT);
  for (;;) {
    WiFiClient client = tcp_notify_server.available();
    if (client) {
      while (client.connected()) {
        if (client.available()) {
          String line = client.readStringUntil('\n');
          line.trim();
          Serial.printf("TCP received:%s\n", line.c_str());
          if (line == TCP_NOTIFY_MESSAGE) {
            handleNotify();
          }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
      }
      client.stop();
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  vTaskDelete(NULL);
}
#endif // USE_TCPNOTIFY

#ifdef USE_UARTNOTIFY
// Serial1で通知を待ち受けるタスク
void uartNotifyTask(void *args) {
  Serial.printf("UART notify listening on Serial1 (RxPin:%d TxPin:%d Baudrate:%u)\n",
                uart_notify_config.rx_pin, uart_notify_config.tx_pin, uart_notify_config.baudrate);
  for (;;) {
    if (Serial1.available()) {
      String line = Serial1.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) {
        Serial.printf("UART received:%s\n", line.c_str());
        if (line == UART_NOTIFY_MESSAGE) {
          handleNotify();
        }
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  vTaskDelete(NULL);
}
#endif // USE_UARTNOTIFY



void setup(void)
{
  auto cfg = M5.config();
#ifndef ARDUINO_M5STACK_Core2
  cfg.output_power = true;
#endif
//cfg.external_spk = true;    /// use external speaker (SPK HAT / ATOMIC SPK)
//cfg.external_spk_detail.omit_atomic_spk = true; // exclude ATOMIC SPK
//cfg.external_spk_detail.omit_spk_hat    = true; // exclude SPK HAT

  M5.begin(cfg);

  { /// custom setting
    auto spk_cfg = M5.Speaker.config();
    /// Increasing the sample_rate will improve the sound quality instead of increasing the CPU load.
#ifndef ARDUINO_M5Stack_Core_ESP32
    // M5Stack Fire/Core2/AWS 向けPSRAM搭載機種のパラメータ
    spk_cfg.sample_rate = 96000; // default:64000 (64kHz)  e.g. 48000 , 50000 , 80000 , 96000 , 100000 , 128000 , 144000 , 192000 , 200000
    spk_cfg.task_pinned_core = APP_CPU_NUM;
    // spk_cfg.task_priority = configMAX_PRIORITIES - 2;
    spk_cfg.dma_buf_count = 20;
    //spk_cfg.stereo = true;
    spk_cfg.dma_buf_len = 256;
#else
    // M5Stack Basic/Gray/Goのパラメータ
    // 音が途切れる場合はdma_buf_countとdma_buf_lenを増やすと改善できる場合あり。
    // 顔が表示されなかったり、点滅するようであれば増やし過ぎなので減らしてください。
    spk_cfg.sample_rate = 64000; // default:64000 (64kHz)  e.g. 48000 , 50000 , 80000 , 96000 , 100000 , 128000 , 144000 , 192000 , 200000
    spk_cfg.task_pinned_core = APP_CPU_NUM;
    // spk_cfg.task_priority = configMAX_PRIORITIES - 2;
    spk_cfg.dma_buf_count = 10;
    //spk_cfg.stereo = true;
    spk_cfg.dma_buf_len = 192;
#endif
    M5.Speaker.config(spk_cfg);
  }


  M5.Speaker.begin();

  // BASICとFIREのV2.6で25MHzだと読み込めないため15MHzまで下げています。
  SD.begin(GPIO_NUM_4, SPI, 15000000);
  
  delay(1000);
  // SC_BasicConfig.yamlを基本設定として読み込み、旧SC_Config.yamlは使用しません。
  // TCP通知を使う場合だけ個人設定SC_SecConfig.yamlも読み込みます。
#ifdef USE_TCPNOTIFY
  system_config.loadConfig(json_fs, "", 0,
                           "/yaml/SC_SecConfig.yaml", 2048);
#else
  system_config.loadConfig(json_fs, "", 0, "", 0);
#endif

#ifdef USE_TCPNOTIFY
  wifi_connected = connectWiFi();
  if (wifi_connected) {
    xTaskCreatePinnedToCore(tcpNotifyTask, "tcpNotifyTask", 4096, NULL, 1, NULL, APP_CPU_NUM);
  }
#endif // USE_TCPNOTIFY

#ifdef USE_UARTNOTIFY
  if (Stackchan_loadUartNotifyConfig(json_fs, UART_APPCONFIG_PATH, uart_notify_config)) {
    Serial1.begin(uart_notify_config.baudrate, SERIAL_8N1, uart_notify_config.rx_pin, uart_notify_config.tx_pin);
    xTaskCreatePinnedToCore(uartNotifyTask, "uartNotifyTask", 4096, NULL, 1, NULL, APP_CPU_NUM);
  } else {
    Serial.printf("Failed to load %s. UART notify disabled.\n", UART_APPCONFIG_PATH);
  }
#endif // USE_UARTNOTIFY

  M5.Speaker.setVolume(system_config.getBluetoothSetting()->start_volume);
  M5.Speaker.setChannelVolume(system_config.getBluetoothSetting()->start_volume, m5spk_virtual_channel);

  if (system_config.getUseTakaoBase()) {
    M5.Power.setExtOutput(false);  // TakaoBaseの後ろ給電を使用する。
  } else {
    M5.Power.setExtOutput(true);  // M5Stackの横から給電を使用する。
  }

  // starting_state true:Bluetoothモードで起動 false:Bluetoothを起動しない
  bluetooth_mode = system_config.getBluetoothSetting()->starting_state;
  Serial.printf("Bluetooth_mode:%s\n", bluetooth_mode ? "true" : "false");

  if ((system_config.getServoInfo(AXIS_X)->pin == 21)
     || (system_config.getServoInfo(AXIS_X)->pin == 22)) {
    // Port.Aを利用する場合は、I2Cが使えないのでアイコンは表示しない。
    if (M5.getBoard() == m5::board_t::board_M5Stack) {
      M5.In_I2C.release();
    }
  }
  
  // Y軸の起動位置と待機動作の中心を、設定された可動範囲の中央にします。
  servo_initial_param_s* servo_y = system_config.getServoInfo(AXIS_Y);
  servo_y->start_degree = servo_y->lower_limit
                        + (servo_y->upper_limit - servo_y->lower_limit) / 2;
  Serial.printf("Servo Y center:%d (lower:%d upper:%d)\n",
                servo_y->start_degree, servo_y->lower_limit, servo_y->upper_limit);

  servo.begin(system_config.getServoInfo(AXIS_X)->pin, system_config.getServoInfo(AXIS_X)->start_degree,
              system_config.getServoInfo(AXIS_X)->offset,
              servo_y->pin, servo_y->start_degree,
              servo_y->offset,
              (ServoType)system_config.getServoType());
  delay(2000);

  if (!avatar_yaml_fs.exists(avatar_system_yaml)) {
    haltWithAvatarConfigError(String("File not found: ") + avatar_system_yaml);
  }
  avatar_system_config.loadConfig(avatar_yaml_fs, avatar_system_yaml);
  M5.Lcd.setBrightness(avatar_system_config.getLcdBrightness());
  current_avatar_filename = avatar_system_config.getAvatarYamlFilename(avatar_count);
  if (current_avatar_filename.isEmpty()) {
    haltWithAvatarConfigError("avatar_yaml is empty");
  }
  if (!validateAvatarConfig(avatar_yaml_fs, current_avatar_filename.c_str())) {
    haltWithAvatarConfigError(avatar_config_error);
  }
  avatar.init(&gfx, current_avatar_filename.c_str(), false, IMAGE_EXPRESSION_NORMAL);
  avatar.start();
  last_powericon_millis = millis();

#ifdef USE_LED
#ifdef USE_NECOMIMI_LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);  // 先頭LEDの起動時誤点灯を防止
#endif
  FastLED.addLeds<SK6812, LED_PIN, GRB>(leds, NUM_LEDS);  // GRB ordering is typical
#ifdef USE_NECOMIMI_LED
  FastLED.setBrightness(10);
  level_led(LEDS_PER_EAR, LEDS_PER_EAR);
#else
  FastLED.setBrightness(32);
  level_led(5, 5);
#endif
  delay(1000);
  turn_off_led();
#endif

  avatar.addTask(lipSync, "lipSync");
  avatar.addTask(servoLoop, "servoLoop", 1, 4096U);
  avatar.setExpression(IMAGE_EXPRESSION_NORMAL);
  avatar.setSpeechFont(system_config.getFont());

  if (bluetooth_mode) {
    startBluetooth();
  } else {
    avatar.setSpeechText("Normal Mode");
  }

}

void loop(void)
{

  M5.update();
  processPendingAvatarExpression();
  if (M5.BtnA.wasDecideClickCount())
  {
    switch (M5.BtnA.getClickCount())
    {
    case 1:
      if (!bluetooth_mode) {
        startBluetooth();
        M5.Speaker.tone(1000, 100);
      }
      break;

    case 2:
      if (bluetooth_mode) {
        avatar.setExpression(IMAGE_EXPRESSION_NORMAL);
        avatar.setSpeechText("Normal Mode");
        M5.Speaker.tone(800, 100);
        a2dp_sink.stop();
        a2dp_sink.end(false);
        delay(1000);
        avatar.setSpeechText("");
        bluetooth_mode = false;
      }
      break;
    }
  }
  if (M5.BtnB.wasPressed()) {
    uint8_t volume = M5.Speaker.getChannelVolume(m5spk_virtual_channel);
    volume = volume - 10;
    M5.Speaker.setVolume(volume);
    M5.Speaker.setChannelVolume(m5spk_virtual_channel, volume);
    M5.Speaker.tone(2000, 100);
    delay(200);
    M5.Speaker.tone(1000, 100);
  }
  if (M5.BtnC.wasPressed()) {
    uint8_t volume = M5.Speaker.getChannelVolume(m5spk_virtual_channel);
    volume = volume + 10;
    M5.Speaker.setVolume(volume);
    M5.Speaker.setChannelVolume(m5spk_virtual_channel, volume);
    M5.Speaker.tone(1000, 100);
    delay(200);
    M5.Speaker.tone(2000, 100);
  }
  if ((millis() - last_powericon_millis)> powericon_interval) {
/*
  Serial.printf("esp_get_free_heap_size()                              : %6d\n", esp_get_free_heap_size() );
  Serial.printf("esp_get_minimum_free_heap_size()                      : %6d\n", esp_get_minimum_free_heap_size() );
  //xPortGetFreeHeapSize()（データメモリ）ヒープの空きバイト数を返すFreeRTOS関数です。これはを呼び出すのと同じheap_caps_get_free_size(MALLOC_CAP_8BIT)です。
  Serial.printf("xPortGetFreeHeapSize()                                : %6d\n", xPortGetFreeHeapSize() );
  //xPortGetMinimumEverFreeHeapSize()また、関連heap_caps_get_minimum_free_size()するものを使用して、ブート以降のヒープの「最低水準点」を追跡できます。
  Serial.printf("xPortGetMinimumEverFreeHeapSize()                     : %6d\n", xPortGetMinimumEverFreeHeapSize() );
  //heap_caps_get_free_size() さまざまなメモリ機能の現在の空きメモリを返すためにも使用できます。
  Serial.printf("heap_caps_get_free_size(MALLOC_CAP_EXEC)              : %6d\n", heap_caps_get_free_size(MALLOC_CAP_EXEC) );
  Serial.printf("heap_caps_get_free_size(MALLOC_CAP_32BIT)             : %6d\n", heap_caps_get_free_size(MALLOC_CAP_32BIT) );
  Serial.printf("heap_caps_get_free_size(MALLOC_CAP_8BIT)              : %6d\n", heap_caps_get_free_size(MALLOC_CAP_8BIT) );
  Serial.printf("heap_caps_get_free_size(MALLOC_CAP_DMA)               : %6d\n", heap_caps_get_free_size(MALLOC_CAP_DMA) );
  Serial.printf("heap_caps_get_free_size(MALLOC_CAP_PID2)              : %6d\n", heap_caps_get_free_size(MALLOC_CAP_PID2) );
  Serial.printf("heap_caps_get_free_size(MALLOC_CAP_PID3)              : %6d\n", heap_caps_get_free_size(MALLOC_CAP_PID3) );
  Serial.printf("heap_caps_get_free_size(MALLOC_CAP_PID3)              : %6d\n", heap_caps_get_free_size(MALLOC_CAP_PID4) );
  Serial.printf("heap_caps_get_free_size(MALLOC_CAP_PID4)              : %6d\n", heap_caps_get_free_size(MALLOC_CAP_PID5) );
  Serial.printf("heap_caps_get_free_size(MALLOC_CAP_PID5)              : %6d\n", heap_caps_get_free_size(MALLOC_CAP_PID6) );
  Serial.printf("heap_caps_get_free_size(MALLOC_CAP_PID6)              : %6d\n", heap_caps_get_free_size(MALLOC_CAP_PID7) );
  Serial.printf("heap_caps_get_free_size(MALLOC_CAP_PID7)              : %6d\n", heap_caps_get_free_size(MALLOC_CAP_PID3) );
  Serial.printf("heap_caps_get_free_size(MALLOC_CAP_SPIRAM)            : %6d\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) );
  Serial.printf("heap_caps_get_free_size(MALLOC_CAP_INTERNAL)          : %6d\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) );
  Serial.printf("heap_caps_get_free_size(MALLOC_CAP_DEFAULT)           : %6d\n", heap_caps_get_free_size(MALLOC_CAP_DEFAULT) );
  //Serial.printf("heap_caps_get_free_size(MALLOC_CAP_IRAM_8BIT)         : %6d\n", heap_caps_get_free_size(MALLOC_CAP_IRAM_8BIT) );
  Serial.printf("heap_caps_get_free_size(MALLOC_CAP_INVALID)           : %6d\n", heap_caps_get_free_size(MALLOC_CAP_INVALID) );
*/
  Serial.printf("free_block DMA: %6d\n", heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
#if !defined(ARDUINO_M5Stack_Core_ESP32) && !defined(ARDUINO_M5STACK_FIRE)
  if (M5.getBoard() == m5::board_t::board_M5StackCore2) {
    // Stack-chan_Takao_Baseを使わない場合
    if ((M5.Power.Axp192.getACINVoltage() < 3.0f) && (M5.Power.Ina3221[0].getBusVoltage(2) < 3.0f)) {
      // USBからの給電が停止したとき
      // Serial.println("USBPowerUnPlugged.");
      M5.Power.setLed(0);
      if ((system_config.getAutoPowerOffTime() > 0) and (last_discharge_time == 0)) {
        M5.Speaker.tone(500, 100);
        last_discharge_time = millis();
      } else if ((system_config.getAutoPowerOffTime() > 0) 
                and ((millis() - last_discharge_time) > system_config.getAutoPowerOffTime())) {
        M5.Power.setExtOutput(false);
        M5.Speaker.tone(2000, 100);
        delay(200);
        M5.Speaker.tone(1000, 100);
        delay(200);
        M5.Speaker.tone(500, 100);
        delay(1000);
        M5.Power.powerOff();
      }
    } else {
      //Serial.println("USBPowerPlugged.");
      M5.Power.setLed(80);
      if (last_discharge_time > 0) {
        last_discharge_time = 0;
      }
    }
  }
#endif
    if ((system_config.getServoInfo(AXIS_X)->pin != 21)
      && (system_config.getServoInfo(AXIS_X)->pin != 22)) {
      // Port.Aを利用する場合は、I2Cが使えないのでアイコンは表示しない。
      last_powericon_millis = millis();
    }
  }

}

#if !defined ( ARDUINO )
extern "C" {
  void loopTask(void*)
  {
    setup();
    for (;;) {
      loop();
    }
    vTaskDelete(NULL);
  }

  void app_main()
  {
    xTaskCreatePinnedToCore(loopTask, "loopTask", 8192, NULL, 1, NULL, 1);
  }
}
#endif
