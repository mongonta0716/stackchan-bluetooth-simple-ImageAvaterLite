/*
  This source is extracted from M5Unified's example(Bluetooth_with_ESP32A2DP.ino).
  https://github.com/m5stack/M5Unified
 */
/// need ESP32-A2DP library. ( URL : https://github.com/pschatzmann/ESP32-A2DP/ )
#include <BluetoothA2DPSink.h>
#include <M5Unified.h>



typedef void (*hvtEventCallback)(int expression, const char* text);

class BluetoothA2DPSink_M5Speaker : public BluetoothA2DPSink
{
public:
  BluetoothA2DPSink_M5Speaker(m5::Speaker_Class* m5sound, uint8_t virtual_channel = 0)
  : BluetoothA2DPSink()
  {
    is_i2s_output = false; // I2S control by BluetoothA2DPSink is not required.
    _virtual_channel = virtual_channel;
  }

  // get rawdata buffer for FFT.
  const int16_t* getBuffer(void) const { return _tri_buf[_export_index]; }

  const char* getMetaData(size_t id) { _meta_bits &= ~(1<<id); return (id < metatext_num) ? _meta_text[id] : nullptr; }
  

  uint8_t getMetaUpdateInfo(void) const { return _meta_bits; }

  void setHvtEventCallback(hvtEventCallback cb) { _hvt_evt_cb = cb; }

  // ESP32-A2DP v1.7.3 clears the saved peer in end(), so preserve it when
  // temporarily leaving Bluetooth mode.  release_memory must stay false to
  // allow start() to be called again.
  void endPreservingLastConnection()
  {
    esp_bd_addr_t saved_peer;
    memcpy(saved_peer, *get_last_peer_address(), ESP_BD_ADDR_LEN);

    // ペアリング待受中にBluetoothモードを終了しても、コントローラ側へ
    // connectable設定を残さないよう、プロファイル終了前に明示的に閉じます。
    if (_stack_ready) {
      BluetoothA2DPSink::set_scan_mode_connectable(false);
    }

    if (is_connected()) {
      disconnect();
      while (is_connected()) { delay(10); }
    }

    BluetoothA2DPSink::end(false);
    _stack_ready = false;
    _incoming_connections_ready = false;
    _bond_reset_pending = false;
    _bond_removal_requested = false;
    set_last_connection(saved_peer);
  }

  // Explicitly forget the locked peer before entering pairing mode.
  void forgetLastConnection() { clean_last_connection(); }

  void rememberLastConnection(esp_bd_addr_t peer)
  {
    set_last_connection(peer);
  }

  void setLockedPeer(const esp_bd_addr_t peer)
  {
    memcpy(_locked_peer, peer, ESP_BD_ADDR_LEN);
    _has_locked_peer = hasAddress(peer);
  }

  void clearLockedPeer()
  {
    memset(_locked_peer, 0, ESP_BD_ADDR_LEN);
    _has_locked_peer = false;
    _locked_peer_authentication_failed = false;
  }

  // 明示的な再ペアリングや初回登録では、Bluetoothスタック起動後に旧ボンドを
  // 全消去してから接続受付を開始します。削除完了前はpage scanを許可しません。
  void requestFreshPairing()
  {
    _clear_bonds_on_stack_up = true;
    _incoming_connections_ready = false;
    _bond_reset_pending = false;
    _bond_removal_requested = false;
    _locked_peer_authentication_failed = false;
  }

  void clearAuthenticationFailure()
  {
    _locked_peer_authentication_failed = false;
  }

  bool lockedPeerAuthenticationFailed() const
  {
    return _locked_peer_authentication_failed;
  }

  // ボンド削除APIは非同期なので、loop()から完了を確認してから受付を開きます。
  void processPairingMode()
  {
    if (!_stack_ready || !_pairing_mode || !_bond_reset_pending) {
      return;
    }

    const int bond_count = esp_bt_gap_get_bond_device_num();
    if (bond_count <= 0) {
      _bond_reset_pending = false;
      _bond_removal_requested = false;
      _incoming_connections_ready = true;
      ESP_LOGI("StackchanBT", "Bluetooth bond reset complete; pairing is now open");
      BluetoothA2DPSink::set_scan_mode_connectable(true);
      return;
    }

    const uint32_t now = millis();
    if (_bond_removal_requested && now - _last_bond_removal_attempt_ms < 2000) {
      return;
    }

    esp_bd_addr_t* bonds =
        static_cast<esp_bd_addr_t*>(malloc(sizeof(esp_bd_addr_t) * bond_count));
    if (bonds == nullptr) {
      ESP_LOGE("StackchanBT", "Unable to allocate Bluetooth bond list");
      return;
    }

    int list_count = bond_count;
    if (esp_bt_gap_get_bond_device_list(&list_count, bonds) != ESP_OK) {
      ESP_LOGE("StackchanBT", "Unable to read Bluetooth bond list");
      free(bonds);
      return;
    }

    _bond_removal_requested = true;
    _last_bond_removal_attempt_ms = now;
    for (int i = 0; i < list_count; ++i) {
      ESP_LOGI("StackchanBT",
               "Removing Bluetooth bond before pairing: %02x:%02x:%02x:%02x:%02x:%02x",
               bonds[i][0], bonds[i][1], bonds[i][2],
               bonds[i][3], bonds[i][4], bonds[i][5]);
      const esp_err_t result = esp_bt_gap_remove_bond_device(bonds[i]);
      if (result != ESP_OK) {
        ESP_LOGE("StackchanBT", "Bluetooth bond removal request failed: %d", result);
      }
    }
    free(bonds);
  }

  // ペアリング中だけ外部からの検出・接続を許可します。通常時は保存済み
  // 接続先へESP32側から接続するため、page scanを停止します。
  void setPairingMode(bool enabled, bool make_discoverable = true)
  {
    _pairing_mode = enabled;
    if (enabled) {
      _pairing_discoverable = make_discoverable;
    }
#ifdef ESP_IDF_4
    // set_discoverability(false)は内部で一度CONNECTABLEへ切り替えるため使いません。
    // discoverabilityは保持だけ行い、実際のscan modeは下で直接設定します。
    discoverability = enabled && _pairing_discoverable
                    ? ESP_BT_GENERAL_DISCOVERABLE
                    : ESP_BT_NON_DISCOVERABLE;
#endif
    if (!_stack_ready) {
      return;
    }

    if (!enabled) {
      _incoming_connections_ready = false;
      _bond_reset_pending = false;
      _bond_removal_requested = false;
      BluetoothA2DPSink::set_scan_mode_connectable(false);
      return;
    }

    if (_clear_bonds_on_stack_up) {
      _clear_bonds_on_stack_up = false;
      _incoming_connections_ready = false;
      _bond_reset_pending = true;
      _bond_removal_requested = false;
      BluetoothA2DPSink::set_scan_mode_connectable(false);
      ESP_LOGI("StackchanBT", "Pairing held closed until old Bluetooth bonds are removed");
      return;
    }

    _incoming_connections_ready = true;
    BluetoothA2DPSink::set_scan_mode_connectable(true);
  }

  bool isPairingMode(void) const { return _pairing_mode; }
  bool isStackReady(void) const { return _stack_ready; }

  // connect_to()は接続前に100msだけpage scanを有効にするため使用しません。
  // 非接続受付のまま、保存済みPCへ発信接続します。
  bool connectToLockedPeer(const esp_bd_addr_t peer)
  {
    if (_pairing_mode || !_stack_ready
        || get_connection_state() != ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
      return false;
    }

    memcpy(peer_bd_addr, peer, ESP_BD_ADDR_LEN);
    const esp_err_t result = esp_a2d_connect(peer_bd_addr);
    set_scan_mode_connectable(false);
    ESP_LOGI("StackchanBT",
             "Outgoing A2DP connection to %02x:%02x:%02x:%02x:%02x:%02x result=%d",
             peer[0], peer[1], peer[2], peer[3], peer[4], peer[5], result);
    return result == ESP_OK;
  }

  void clear(void)
  {
    for (int i = 0; i < 3; ++i)
    {
      if (_tri_buf[i]) { memset(_tri_buf[i], 0, _tri_buf_size[i]); }
    }
  }

  static constexpr size_t metatext_size = 80;
  static constexpr size_t metatext_num = 3;

protected:
  int16_t* _tri_buf[3] = { nullptr, nullptr, nullptr };
  size_t _tri_buf_size[3] = { 0, 0, 0 };
  size_t _tri_index = 0;
  size_t _export_index = 0;
  char _meta_text[metatext_num][metatext_size];
  uint8_t _meta_bits = 0;
  bool _sing_happy = true;
  size_t _sample_rate = 48000;
  int _avatar_expression = 5; // m5stack-avatarの表情取得用
  const char* _avatar_text = "";
  hvtEventCallback _hvt_evt_cb = nullptr;
  uint8_t _virtual_channel = 0;
  volatile bool _pairing_mode = true;
  volatile bool _stack_ready = false;
  volatile bool _incoming_connections_ready = false;
  volatile bool _bond_reset_pending = false;
  volatile bool _locked_peer_authentication_failed = false;
  bool _clear_bonds_on_stack_up = false;
  bool _bond_removal_requested = false;
  bool _pairing_discoverable = true;
  bool _has_locked_peer = false;
  bool _last_connection_accepted = false;
  uint32_t _last_bond_removal_attempt_ms = 0;
  esp_bd_addr_t _locked_peer = { 0, 0, 0, 0, 0, 0 };

  static bool hasAddress(const esp_bd_addr_t peer)
  {
    const esp_bd_addr_t empty = { 0, 0, 0, 0, 0, 0 };
    return memcmp(peer, empty, ESP_BD_ADDR_LEN) != 0;
  }

  bool isLockedPeer(const esp_bd_addr_t peer) const
  {
    return _has_locked_peer
        && memcmp(_locked_peer, peer, ESP_BD_ADDR_LEN) == 0;
  }

  // ESP32-A2DPは起動時や切断時に接続受付を有効化します。virtual dispatchを
  // ここで受け、ペアリング受付の準備が完了した場合だけtrueを通します。
  void set_scan_mode_connectable(bool connectable) override
  {
    const bool allow = connectable && _pairing_mode && _incoming_connections_ready;
    if (connectable && !allow) {
      ESP_LOGI("StackchanBT", "Blocked library request to become connectable");
    }
    BluetoothA2DPSink::set_scan_mode_connectable(allow);
  }

  void av_hdl_stack_evt(uint16_t event, void *p_param) override
  {
    // 基底クラス末尾のset_scan_mode_connectable(true)は上のoverrideで遮断します。
    BluetoothA2DPSink::av_hdl_stack_evt(event, p_param);
    if (event == BT_APP_EVT_STACK_UP) {
      _stack_ready = true;
      setPairingMode(_pairing_mode, _pairing_discoverable);
    }
  }

  void app_gap_callback(esp_bt_gap_cb_event_t event,
                        esp_bt_gap_cb_param_t* param) override
  {
    switch (event) {
      case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
        ESP_LOGI("StackchanBT",
                 "ACL connected peer=%02x:%02x:%02x:%02x:%02x:%02x status=%d",
                 param->acl_conn_cmpl_stat.bda[0], param->acl_conn_cmpl_stat.bda[1],
                 param->acl_conn_cmpl_stat.bda[2], param->acl_conn_cmpl_stat.bda[3],
                 param->acl_conn_cmpl_stat.bda[4], param->acl_conn_cmpl_stat.bda[5],
                 param->acl_conn_cmpl_stat.stat);
        break;

      case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
        ESP_LOGI("StackchanBT",
                 "ACL disconnected peer=%02x:%02x:%02x:%02x:%02x:%02x reason=%d",
                 param->acl_disconn_cmpl_stat.bda[0], param->acl_disconn_cmpl_stat.bda[1],
                 param->acl_disconn_cmpl_stat.bda[2], param->acl_disconn_cmpl_stat.bda[3],
                 param->acl_disconn_cmpl_stat.bda[4], param->acl_disconn_cmpl_stat.bda[5],
                 param->acl_disconn_cmpl_stat.reason);
        break;

      case ESP_BT_GAP_AUTH_CMPL_EVT: {
        const bool locked_peer = isLockedPeer(param->auth_cmpl.bda);
        ESP_LOGI("StackchanBT",
                 "Authentication peer=%02x:%02x:%02x:%02x:%02x:%02x status=%d locked=%s",
                 param->auth_cmpl.bda[0], param->auth_cmpl.bda[1],
                 param->auth_cmpl.bda[2], param->auth_cmpl.bda[3],
                 param->auth_cmpl.bda[4], param->auth_cmpl.bda[5],
                 param->auth_cmpl.stat, locked_peer ? "true" : "false");
        if (locked_peer) {
          _locked_peer_authentication_failed =
              param->auth_cmpl.stat != ESP_BT_STATUS_SUCCESS;
        }
        break;
      }

      case ESP_BT_GAP_CFM_REQ_EVT:
        if (_has_locked_peer && !isLockedPeer(param->cfm_req.bda)) {
          ESP_LOGW("StackchanBT",
                   "Rejected pairing confirmation from non-locked peer %02x:%02x:%02x:%02x:%02x:%02x",
                   param->cfm_req.bda[0], param->cfm_req.bda[1],
                   param->cfm_req.bda[2], param->cfm_req.bda[3],
                   param->cfm_req.bda[4], param->cfm_req.bda[5]);
          esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, false);
          return;
        }
        break;

      case ESP_BT_GAP_PIN_REQ_EVT:
        if (_has_locked_peer && !isLockedPeer(param->pin_req.bda)) {
          ESP_LOGW("StackchanBT",
                   "Rejected PIN request from non-locked peer %02x:%02x:%02x:%02x:%02x:%02x",
                   param->pin_req.bda[0], param->pin_req.bda[1],
                   param->pin_req.bda[2], param->pin_req.bda[3],
                   param->pin_req.bda[4], param->pin_req.bda[5]);
          esp_bt_gap_pin_reply(param->pin_req.bda, false, 0, nullptr);
          return;
        }
        break;

      case ESP_BT_GAP_KEY_REQ_EVT:
        if (_has_locked_peer && !isLockedPeer(param->key_req.bda)) {
          ESP_LOGW("StackchanBT",
                   "Rejected passkey request from non-locked peer %02x:%02x:%02x:%02x:%02x:%02x",
                   param->key_req.bda[0], param->key_req.bda[1],
                   param->key_req.bda[2], param->key_req.bda[3],
                   param->key_req.bda[4], param->key_req.bda[5]);
          esp_bt_gap_ssp_passkey_reply(param->key_req.bda, false, 0);
          return;
        }
        break;

      case ESP_BT_GAP_REMOVE_BOND_DEV_COMPLETE_EVT:
        ESP_LOGI("StackchanBT",
                 "Bluetooth bond removed peer=%02x:%02x:%02x:%02x:%02x:%02x status=%d",
                 param->remove_bond_dev_cmpl.bda[0], param->remove_bond_dev_cmpl.bda[1],
                 param->remove_bond_dev_cmpl.bda[2], param->remove_bond_dev_cmpl.bda[3],
                 param->remove_bond_dev_cmpl.bda[4], param->remove_bond_dev_cmpl.bda[5],
                 param->remove_bond_dev_cmpl.status);
        break;

      default:
        break;
    }

    BluetoothA2DPSink::app_gap_callback(event, param);
  }

  // ライブラリ標準のhandle_connection_state()は切断時に自前で再接続を試み、
  // その際に一時的にset_scan_mode_connectable(true)を呼ぶため、ロック中の
  // 相手以外にも数百ms間だけ検出・接続可能な状態になってしまいます
  // （processBluetoothReconnect()による定期再接続と役割が重複する上、
  // 通常モードでも外部から見えてしまう原因になります）。
  // ここでは状態の記録とコールバック呼び出しだけを行い、ライブラリ自身の
  // 自動再接続・スキャンモード変更は行いません。
  void handle_connection_state(uint16_t event, void *p_param) override
  {
    esp_a2d_cb_param_t* a2d = (esp_a2d_cb_param_t *)(p_param);

    ESP_LOGI("StackchanBT",
             "A2DP state=%d peer=%02x:%02x:%02x:%02x:%02x:%02x",
             a2d->conn_stat.state,
             a2d->conn_stat.remote_bda[0], a2d->conn_stat.remote_bda[1],
             a2d->conn_stat.remote_bda[2], a2d->conn_stat.remote_bda[3],
             a2d->conn_stat.remote_bda[4], a2d->conn_stat.remote_bda[5]);
    memcpy(peer_bd_addr, a2d->conn_stat.remote_bda, ESP_BD_ADDR_LEN);
    connection_state = a2d->conn_stat.state;
    _last_connection_accepted = false;
    if (connection_state_callback != nullptr) {
      connection_state_callback(connection_state, connection_state_obj);
    }

    switch (a2d->conn_stat.state) {
      case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
        pin_code_int = 0;
        pin_code_request = Undefined;
        if (bt_dis_connected != nullptr) { (*bt_dis_connected)(); }
        if (is_i2s_output) { bt_i2s_task_shut_down(); }
        break;

      case ESP_A2D_CONNECTION_STATE_CONNECTING:
        connection_rety_count++;
        break;

      case ESP_A2D_CONNECTION_STATE_CONNECTED: {
        bool is_valid = true;
        if (address_validator != nullptr) {
          uint8_t *bda = a2d->conn_stat.remote_bda;
          if (!address_validator(bda)) {
            esp_a2d_sink_disconnect(bda);
            is_valid = false;
          }
        }
        if (is_valid) {
          _last_connection_accepted = true;
          if (bt_connected != nullptr) { (*bt_connected)(); }
          set_scan_mode_connectable(false);
          connection_rety_count = 0;
          if (is_i2s_output) { bt_i2s_task_start_up(); }
#ifdef ESP_IDF_4
          esp_bt_gap_read_remote_name(a2d->conn_stat.remote_bda);
#endif
          if (rssi_active) { esp_bt_gap_read_rssi_delta(a2d->conn_stat.remote_bda); }
        }
        break;
      }

      default:
        break;
    }
  }

  void clearMetaData(void)
  {
    for (int i = 0; i < metatext_num; ++i)
    {
      _meta_text[i][0] = 0;
    }
    _meta_bits = (1<<metatext_num)-1;
  }

  void av_hdl_a2d_evt(uint16_t event, void *p_param) override
  {
    esp_a2d_cb_param_t* a2d = (esp_a2d_cb_param_t *)(p_param);

    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
      if (ESP_A2D_CONNECTION_STATE_CONNECTED == a2d->conn_stat.state)
      { // 接続
        _avatar_expression = 5; //Neutral
        _avatar_text = "";
        //avatar.setExpression(Expression::Neutral);

      }
      else
      if (ESP_A2D_CONNECTION_STATE_DISCONNECTED == a2d->conn_stat.state)
      { // 切断
        _avatar_expression = 2;
        _avatar_text = "Bluetooth Mode";
        //avatar.setExpression(Expression::Sad);
      }
      break;

    case ESP_A2D_AUDIO_STATE_EVT:
      if (ESP_A2D_AUDIO_STATE_STARTED == a2d->audio_stat.state)
      { // 再生
        if (_sing_happy) {
          _avatar_expression = 0;
          // avatar.setExpression(Expression::Happy);
        } else {
          _avatar_expression = 5;
          //avatar.setExpression(Expression::Neutral);
        }
        _sing_happy = !_sing_happy;
      } else
      if ( ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND == a2d->audio_stat.state
        || ESP_A2D_AUDIO_STATE_STOPPED        == a2d->audio_stat.state )
      { // 停止
        _avatar_expression = 4;
        // avatar.setExpression(Expression::Sleepy);
        clearMetaData();
        clear();
      }
      break;

    case ESP_A2D_AUDIO_CFG_EVT:
      {
        esp_a2d_cb_param_t *a2d = (esp_a2d_cb_param_t *)(p_param);
        size_t tmp = a2d->audio_cfg.mcc.cie.sbc[0];
        size_t rate = 16000;
        if (     tmp & (1 << 6)) { rate = 32000; }
        else if (tmp & (1 << 5)) { rate = 44100; }
        else if (tmp & (1 << 4)) { rate = 48000; }
        _sample_rate = rate;
      }
      break;

    default:
      break;
    }
    
    if (_hvt_evt_cb != nullptr) {
        _hvt_evt_cb(_avatar_expression, _avatar_text);
    }

    BluetoothA2DPSink::av_hdl_a2d_evt(event, p_param);

    // 最初の接続後は通常モードへ移り、保存済みPC以外からの接続受付を止めます。
    if (event == ESP_A2D_CONNECTION_STATE_EVT
        && a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED
        && _last_connection_accepted) {
      _pairing_mode = false;
    }
    if (_stack_ready) {
      setPairingMode(_pairing_mode, _pairing_discoverable);
    }
  }

  void av_hdl_avrc_evt(uint16_t event, void *p_param) override
  {
    esp_avrc_ct_cb_param_t *rc = (esp_avrc_ct_cb_param_t *)(p_param);

    switch (event)
    {
    case ESP_AVRC_CT_METADATA_RSP_EVT:
      for (size_t i = 0; i < metatext_num; ++i)
      {
        if (0 == (rc->meta_rsp.attr_id & (1 << i))) { continue; }
        strncpy(_meta_text[i], (char*)(rc->meta_rsp.attr_text), metatext_size);
        _meta_bits |= rc->meta_rsp.attr_id;
        break;
      }
      break;

    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
      break;

    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
      break;

    default:
      break;
    }

    BluetoothA2DPSink::av_hdl_avrc_evt(event, p_param);
  }

  int16_t* get_next_buf(const uint8_t* src_data, uint32_t len)
  {
    size_t tri = _tri_index < 2 ? _tri_index + 1 : 0;
    if (_tri_buf_size[tri] < len)
    {
      _tri_buf_size[tri] = len;
      if (_tri_buf[tri] != nullptr) { heap_caps_free(_tri_buf[tri]); }
      auto tmp = (int16_t*)heap_caps_malloc(len, MALLOC_CAP_8BIT);
      _tri_buf[tri] = tmp;
      if (tmp == nullptr)
      {
        _tri_buf_size[tri] = 0;
        return nullptr;
      }
    }
    memcpy(_tri_buf[tri], src_data, len);
    _tri_index = tri;
    return _tri_buf[tri];
  }

  void audio_data_callback(const uint8_t *data, uint32_t length) override
  {
    // Reduce memory requirements by dividing the received data into the first and second halves.
    length >>= 1;
    M5.Speaker.playRaw(get_next_buf( data        , length), length >> 1, _sample_rate, true, 1, _virtual_channel);
    M5.Speaker.playRaw(get_next_buf(&data[length], length), length >> 1, _sample_rate, true, 1, _virtual_channel);
    _export_index = _tri_index;
  }
};


#define FFT_SIZE 256
class fft_t
{
  float _wr[FFT_SIZE + 1];
  float _wi[FFT_SIZE + 1];
  float _fr[FFT_SIZE + 1];
  float _fi[FFT_SIZE + 1];
  uint16_t _br[FFT_SIZE + 1];
  size_t _ie;

public:
  fft_t(void)
  {
#ifndef M_PI
#define M_PI 3.141592653
#endif
    _ie = logf( (float)FFT_SIZE ) / log(2.0) + 0.5;
    static constexpr float omega = 2.0f * M_PI / FFT_SIZE;
    static constexpr int s4 = FFT_SIZE / 4;
    static constexpr int s2 = FFT_SIZE / 2;
    for ( int i = 1 ; i < s4 ; ++i)
    {
    float f = cosf(omega * i);
      _wi[s4 + i] = f;
      _wi[s4 - i] = f;
      _wr[     i] = f;
      _wr[s2 - i] = -f;
    }
    _wi[s4] = _wr[0] = 1;

    size_t je = 1;
    _br[0] = 0;
    _br[1] = FFT_SIZE / 2;
    for ( size_t i = 0 ; i < _ie - 1 ; ++i )
    {
      _br[ je << 1 ] = _br[ je ] >> 1;
      je = je << 1;
      for ( size_t j = 1 ; j < je ; ++j )
      {
        _br[je + j] = _br[je] + _br[j];
      }
    }
  }

  void exec(const int16_t* in)
  {
    memset(_fi, 0, sizeof(_fi));
    for ( size_t j = 0 ; j < FFT_SIZE / 2 ; ++j )
    {
      float basej = 0.25 * (1.0-_wr[j]);
      size_t r = FFT_SIZE - j - 1;

      /// perform han window and stereo to mono convert.
      _fr[_br[j]] = basej * (in[j * 2] + in[j * 2 + 1]);
      _fr[_br[r]] = basej * (in[r * 2] + in[r * 2 + 1]);
    }

    size_t s = 1;
    size_t i = 0;
    do
    {
      size_t ke = s;
      s <<= 1;
      size_t je = FFT_SIZE / s;
      size_t j = 0;
      do
      {
        size_t k = 0;
        do
        {
          size_t l = s * j + k;
          size_t m = ke * (2 * j + 1) + k;
          size_t p = je * k;
          float Wxmr = _fr[m] * _wr[p] + _fi[m] * _wi[p];
          float Wxmi = _fi[m] * _wr[p] - _fr[m] * _wi[p];
          _fr[m] = _fr[l] - Wxmr;
          _fi[m] = _fi[l] - Wxmi;
          _fr[l] += Wxmr;
          _fi[l] += Wxmi;
        } while ( ++k < ke) ;
      } while ( ++j < je );
    } while ( ++i < _ie );
  }

  uint32_t get(size_t index)
  {
    return (index < FFT_SIZE / 2) ? (uint32_t)sqrtf(_fr[ index ] * _fr[ index ] + _fi[ index ] * _fi[ index ]) : 0u;
  }
};
