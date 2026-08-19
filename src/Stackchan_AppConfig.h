// UART通知(USE_UARTNOTIFY)用のアプリ設定(/app/SCBluetooth/SC_AppConfig.xml)を読み込みます。
#pragma once

#include <Arduino.h>
#include <FS.h>

struct UartNotifyConfig {
  int8_t rx_pin = -1;
  int8_t tx_pin = -1;
  uint32_t baudrate = 115200;
};

// xml中から <tag>...</tag> の中身を取り出します。(入れ子のない単純なXML専用の簡易実装です。)
inline String Stackchan_getXmlTagValue(const String &xml, const char *tag) {
  String open_tag = String("<") + tag + ">";
  String close_tag = String("</") + tag + ">";
  int start = xml.indexOf(open_tag);
  if (start < 0) {
    return String();
  }
  start += open_tag.length();
  int end = xml.indexOf(close_tag, start);
  if (end < 0) {
    return String();
  }
  String value = xml.substring(start, end);
  value.trim();
  return value;
}

inline bool Stackchan_loadUartNotifyConfig(fs::FS &fs, const char *path, UartNotifyConfig &config) {
  File file = fs.open(path);
  if (!file) {
    Serial.printf("Failed to open %s\n", path);
    return false;
  }
  String xml = file.readString();
  file.close();

  String rx_pin = Stackchan_getXmlTagValue(xml, "RxPin");
  String tx_pin = Stackchan_getXmlTagValue(xml, "TxPin");
  String baudrate = Stackchan_getXmlTagValue(xml, "Baudrate");

  if (rx_pin.length() == 0 || tx_pin.length() == 0) {
    Serial.printf("RxPin/TxPin not found in %s\n", path);
    return false;
  }
  config.rx_pin = (int8_t)rx_pin.toInt();
  config.tx_pin = (int8_t)tx_pin.toInt();
  if (baudrate.length() > 0) {
    config.baudrate = (uint32_t)baudrate.toInt();
  }
  return true;
}
