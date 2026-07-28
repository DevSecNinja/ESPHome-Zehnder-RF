#include "zehnder.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/core/application.h"

namespace esphome {
namespace zehnder {

#define MAX_TRANSMIT_TIME 2000

static const char *const TAG = "zehnder";
// Dedicated tag for the raw RF frame sniffer so it can be isolated from the rest of the
// state-machine chatter via the logger `logs:` config (e.g. set `zehnder.rf: DEBUG` while
// keeping `zehnder: INFO`). See PR: protocol review / dump cross-checking.
static const char *const TAG_RF = "zehnder.rf";

typedef struct __attribute__((packed)) {
  uint32_t networkId;
} RfPayloadNetworkJoinOpen;

typedef struct __attribute__((packed)) {
  uint32_t networkId;
} RfPayloadNetworkJoinRequest;

typedef struct __attribute__((packed)) {
  uint32_t networkId;
} RfPayloadNetworkJoinAck;

typedef struct __attribute__((packed)) {
  uint8_t speed;    // 0x0B Current speed preset (0x01: low, 0x02: medium, 0x03: high, 0x04: max)
  uint8_t voltage;  // 0x0C Current fan voltage (0x00-0x64 => 0.0-10.0V)
  uint8_t flags;    // 0x0D Flag byte; bit0: timer active (0=off after Set speed, 1=on after Set timer)
  // NOTE: offset 0x0E (unit type / firmware, unknown) is not parsed here.
} RfPayloadFanSettings;

typedef struct __attribute__((packed)) {
  uint8_t speed;
} RfPayloadFanSetSpeed;

typedef struct __attribute__((packed)) {
  uint8_t voltage;  // Fan voltage as percentage (0x00-0x64 => 0.0-10.0V)
} RfPayloadFanSetVoltage;

typedef struct __attribute__((packed)) {
  uint8_t speed;
  uint8_t timer;
} RfPayloadFanSetTimer;

typedef struct __attribute__((packed)) {
  uint8_t rx_type;          // 0x00 RX Type
  uint8_t rx_id;            // 0x01 RX ID
  uint8_t tx_type;          // 0x02 TX Type
  uint8_t tx_id;            // 0x03 TX ID
  uint8_t ttl;              // 0x04 Time-To-Live
  uint8_t command;          // 0x05 Frame type
  uint8_t parameter_count;  // 0x06 Number of parameters

  union {
    uint8_t parameters[9];                           // 0x07 - 0x0F Depends on command
    RfPayloadFanSetSpeed setSpeed;                   // Command 0x02
    RfPayloadFanSetVoltage setVoltage;               // Command 0x01
    RfPayloadFanSetTimer setTimer;                   // Command 0x03
    RfPayloadNetworkJoinRequest networkJoinRequest;  // Command 0x04
    RfPayloadNetworkJoinOpen networkJoinOpen;        // Command 0x06
    RfPayloadFanSettings fanSettings;                // Command 0x07
    RfPayloadNetworkJoinAck networkJoinAck;          // Command 0x0C
  } payload;
} RfFrame;

static uint8_t minmax(const uint8_t value, const uint8_t min, const uint8_t max) {
  if (value <= min) {
    return min;
  } else if (value >= max) {
    return max;
  } else {
    return value;
  }
}

static int clamp_voltage(const int value) {
  // Per the protocol spec the fan voltage byte only uses bits 0-6 (0-127 => 0.0-12.7V);
  // bit 7 (MSB) is ignored by the fan. Mask it off first so a stray high bit can't produce
  // a garbage percentage (e.g. the huge values reported in issue #18).
  int v = value & 0x7F;

  // The ComfoFan caps its output at ~10.5V, so anything above 100% is reported as 100%.
  if (v > 100) {
    ESP_LOGW(TAG, "Voltage value %i (masked from %i) clamped to 100", v, value);
    return 100;
  }
  return v;
}

ZehnderRF::ZehnderRF(void) {}

fan::FanTraits ZehnderRF::get_traits() { return fan::FanTraits(false, true, false, this->speed_count_); }

void ZehnderRF::control(const fan::FanCall &call) {
  if (call.get_state().has_value()) {
    this->state = *call.get_state();
    ESP_LOGD(TAG, "Fan control state changed: %s", this->state ? "ON" : "OFF");
  }
  if (call.get_speed().has_value()) {
    this->speed = *call.get_speed();
    ESP_LOGD(TAG, "Fan control speed changed: %u", this->speed);
  }

  switch (this->state_) {
    case StateIdle:
      // Set speed
      this->setSpeed(this->state ? this->speed : 0x00, 0);

      this->lastFanQuery_ = millis();  // Update time
      break;

    default:
      break;
  }

  this->publish_state();
}

void ZehnderRF::setup() {
  ESP_LOGCONFIG(TAG, "ZEHNDER '%s':", this->get_name().c_str());

  // Clear config
  memset(&this->config_, 0, sizeof(Config));

  uint32_t hash = fnv1_hash("zehnderrf");
  this->pref_ = global_preferences->make_preference<Config>(hash, true);
  if (this->pref_.load(&this->config_)) {
    ESP_LOGD(TAG, "Configuration loaded successfully");
  } else {
    ESP_LOGD(TAG, "No saved configuration found, using defaults");
  }

  // Set nRF905 config
  nrf905::Config rfConfig;
  rfConfig = this->rf_->getConfig();

  rfConfig.band = true;
  rfConfig.channel = 118;

  // // CRC 16
  rfConfig.crc_enable = true;
  rfConfig.crc_bits = 16;

  // // TX power 10
  rfConfig.tx_power = 10;

  // // RX power normal
  rfConfig.rx_power = nrf905::PowerNormal;

  rfConfig.rx_address = 0x89816EA9;  // ZEHNDER_NETWORK_LINK_ID;
  rfConfig.rx_address_width = 4;
  rfConfig.rx_payload_width = 16;

  rfConfig.tx_address_width = 4;
  rfConfig.tx_payload_width = 16;

  rfConfig.xtal_frequency = 16000000;  // defaults for now
  rfConfig.clkOutFrequency = nrf905::ClkOut500000;
  rfConfig.clkOutEnable = false;

  // Write config back
  this->rf_->updateConfig(&rfConfig);
  this->rf_->writeTxAddress(0x89816EA9);

  this->speed_count_ = 4;

  this->rf_->setOnTxReady([this](void) {
    ESP_LOGD(TAG, "TX ready");
    if (this->rfState_ == RfStateTxBusy) {
      if (this->retries_ >= 0) {
        this->msgSendTime_ = millis();
        this->rfState_ = RfStateRxWait;
      } else {
        this->rfState_ = RfStateIdle;
      }
    }
  });

  this->rf_->setOnRxComplete([this](const uint8_t *const pData, const uint8_t dataLength) {
    ESP_LOGV(TAG, "RF frame received, length: %u bytes", dataLength);
    this->rfHandleReceived(pData, dataLength);
  });
}

void ZehnderRF::dump_config(void) {
  ESP_LOGCONFIG(TAG, "Zehnder Fan config:");
  ESP_LOGCONFIG(TAG, "  Polling interval   %u", this->interval_);
  ESP_LOGCONFIG(TAG, "  Sniffer mode       %s", this->sniffer_mode_ ? "ON (passive capture, no control)" : "off");
  ESP_LOGCONFIG(TAG, "  Fan networkId      0x%08X", this->config_.fan_networkId);
  ESP_LOGCONFIG(TAG, "  Fan my device type 0x%02X", this->config_.fan_my_device_type);
  ESP_LOGCONFIG(TAG, "  Fan my device id   0x%02X", this->config_.fan_my_device_id);
  ESP_LOGCONFIG(TAG, "  Fan main_unit type 0x%02X", this->config_.fan_main_unit_type);
  ESP_LOGCONFIG(TAG, "  Fan main unit id   0x%02X", this->config_.fan_main_unit_id);
  ESP_LOGCONFIG(TAG, "Connection Status Sensor:");
  ESP_LOGCONFIG(TAG, "  Health timeout     %u ms", this->interval_ * 5);
  ESP_LOGCONFIG(TAG, "  Failure threshold  3 consecutive timeouts");
}

void ZehnderRF::set_config(const uint32_t fan_networkId,
                           const uint8_t  fan_my_device_type,
                           const uint8_t  fan_my_device_id,
                           const uint8_t  fan_main_unit_type,
                           const uint8_t  fan_main_unit_id) {
  this->config_.fan_networkId      = fan_networkId;      // Fan (Zehnder/BUVA) network ID
  this->config_.fan_my_device_type = fan_my_device_type; // Fan (Zehnder/BUVA) device type
  this->config_.fan_my_device_id   = fan_my_device_id;   // Fan (Zehnder/BUVA) device ID
  this->config_.fan_main_unit_type = fan_main_unit_type; // Fan (Zehnder/BUVA) main unit type
  this->config_.fan_main_unit_id   = fan_main_unit_id;   // Fan (Zehnder/BUVA) main unit ID
  ESP_LOGD(TAG, "Saving pairing configuration");
  this->pref_.save(&this->config_);
}

void ZehnderRF::loop(void) {
  uint8_t deviceId;
  nrf905::Config rfConfig;

  // Run RF handler
  this->rfHandler();

  switch (this->state_) {
    case StateStartup:
      // Wait until started up
      if (millis() > 15000) {
        // Sniffer mode: skip pairing/polling entirely and just listen.
        if (this->sniffer_mode_) {
          this->startSniffer();
          break;
        }

        // Discovery?
        if ((this->config_.fan_networkId == 0x00000000) || (this->config_.fan_my_device_type == 0) ||
            (this->config_.fan_my_device_id == 0) || (this->config_.fan_main_unit_type == 0) ||
            (this->config_.fan_main_unit_id == 0)) {
          ESP_LOGD(TAG, "Invalid config, starting pairing");

          this->state_ = StateStartDiscovery;
        } else {
          ESP_LOGD(TAG, "Configuration data valid, starting polling");

          rfConfig = this->rf_->getConfig();
          rfConfig.rx_address = this->config_.fan_networkId;
          this->rf_->updateConfig(&rfConfig);
          this->rf_->writeTxAddress(this->config_.fan_networkId);

          ESP_LOGD(TAG, "RF network configured, starting device query");
          // Start with query
          this->queryDevice();
        }
      }
      break;

    case StateStartDiscovery:
      deviceId = this->createDeviceID();
      this->discoveryStart(deviceId);

      // For now just set TX
      break;

    case StateIdle:
      if (newSetting == true) {
        if (this->newSpeedIsVoltage) {
          this->setVoltage(this->newVoltage);
        } else {
          this->setSpeed(newSpeed, newTimer);
        }
      } else {
        if ((millis() - this->lastFanQuery_) > this->interval_) {
          this->queryDevice();
        }
      }
      
      // Periodic health check - if no successful communication for too long, mark as unhealthy
      this->check_connection_health();
      break;

    case StateWaitSetSpeedConfirm:
      if (this->rfState_ == RfStateIdle) {
        // When done, return to idle
        this->state_ = StateIdle;
      }

    case StateSniffer:
      // Passive capture only: the radio stays in RX and every frame is logged by
      // rfHandleReceived via the 'zehnder.rf' tag. Nothing else to do here.
      break;

    default:
      break;
  }
}

void ZehnderRF::rfHandleReceived(const uint8_t *const pData, const uint8_t dataLength) {
  const RfFrame *const pResponse = (RfFrame *) pData;
  RfFrame *const pTxFrame = (RfFrame *) this->_txFrame;  // frame helper
  nrf905::Config rfConfig;

  // Decoded one-line dump of every received on-network frame, on a dedicated tag so it can be
  // captured in isolation without enabling VERY_VERBOSE globally. Header fields plus the raw
  // 16-byte payload, so it can be cross-checked against the protocol spec.
  ESP_LOGD(TAG_RF, "RX rx=%02X:%02X tx=%02X:%02X ttl=%02X cmd=%02X n=%u | %s", pResponse->rx_type,
           pResponse->rx_id, pResponse->tx_type, pResponse->tx_id, pResponse->ttl, pResponse->command,
           pResponse->parameter_count, format_hex_pretty(pData, dataLength).c_str());

  ESP_LOGD(TAG, "Current state: 0x%02X", this->state_);
  switch (this->state_) {
    case StateDiscoveryWaitForLinkRequest:
      ESP_LOGD(TAG, "Discovery state: waiting for link request");
      switch (pResponse->command) {
        case FAN_NETWORK_JOIN_OPEN:  // Received linking request from main unit
          ESP_LOGD(TAG, "Discovery: Found unit type 0x%02X (%s) with ID 0x%02X on network 0x%08X", pResponse->tx_type,
                   pResponse->tx_type == FAN_TYPE_MAIN_UNIT ? "Main" : "?", pResponse->tx_id,
                   pResponse->payload.networkJoinOpen.networkId);

          this->rfComplete();

          (void) memset(this->_txFrame, 0, FAN_FRAMESIZE);  // Clear frame data

          // Found a main unit, so send a join request
          pTxFrame->rx_type = FAN_TYPE_MAIN_UNIT;  // Set type to main unit
          pTxFrame->rx_id = pResponse->tx_id;      // Set ID to the ID of the main unit
          pTxFrame->tx_type = this->config_.fan_my_device_type;
          pTxFrame->tx_id = this->config_.fan_my_device_id;
          pTxFrame->ttl = FAN_TTL;
          pTxFrame->command = FAN_NETWORK_JOIN_REQUEST;  // Request to connect to network
          pTxFrame->parameter_count = sizeof(RfPayloadNetworkJoinOpen);
          // Request to connect to the received network ID
          pTxFrame->payload.networkJoinRequest.networkId = pResponse->payload.networkJoinOpen.networkId;

          // Store for later
          this->config_.fan_networkId = pResponse->payload.networkJoinOpen.networkId;
          this->config_.fan_main_unit_type = pResponse->tx_type;
          this->config_.fan_main_unit_id = pResponse->tx_id;

          // Update address
          rfConfig = this->rf_->getConfig();
          rfConfig.rx_address = pResponse->payload.networkJoinOpen.networkId;
          this->rf_->updateConfig(&rfConfig, NULL);
          this->rf_->writeTxAddress(pResponse->payload.networkJoinOpen.networkId, NULL);

          // Send response frame
          this->startTransmit(this->_txFrame, FAN_TX_RETRIES, [this]() {
            ESP_LOGW(TAG, "Discovery query timeout, restarting discovery");
            this->state_ = StateStartDiscovery;
          });

          this->state_ = StateDiscoveryWaitForJoinResponse;
          break;

        default:
          ESP_LOGD(TAG, "Discovery: Received unknown frame type 0x%02X from ID 0x%02X", pResponse->command,
                   pResponse->tx_id);
          break;
      }
      break;

    case StateDiscoveryWaitForJoinResponse:
      ESP_LOGD(TAG, "Discovery state: waiting for join response");
      switch (pResponse->command) {
        case FAN_FRAME_0B:
          if ((pResponse->rx_type == this->config_.fan_my_device_type) &&
              (pResponse->rx_id == this->config_.fan_my_device_id) &&
              (pResponse->tx_type == this->config_.fan_main_unit_type) &&
              (pResponse->tx_id == this->config_.fan_main_unit_id)) {
            ESP_LOGD(TAG, "Discovery: Link successful to unit with ID 0x%02X on network 0x%08X", pResponse->tx_id,
                     this->config_.fan_networkId);

            this->rfComplete();

            (void) memset(this->_txFrame, 0, FAN_FRAMESIZE);  // Clear frame data

            pTxFrame->rx_type = FAN_TYPE_MAIN_UNIT;  // Set type to main unit
            pTxFrame->rx_id = pResponse->tx_id;      // Set ID to the ID of the main unit
            // pTxFrame->rx_id = 0x00;  // Broadcast - this should fix the CO2 sensor overriding the call?
            // Per https://github.com/TimelessNL/ESPHome-Zehnder-RF/pull/1 we shouldn't broadcast on link success
            pTxFrame->tx_type = this->config_.fan_my_device_type;
            pTxFrame->tx_id = this->config_.fan_my_device_id;
            pTxFrame->ttl = FAN_TTL;
            pTxFrame->command = FAN_FRAME_0B;  // 0x0B acknowledge link successful
            pTxFrame->parameter_count = 0x00;  // No parameters

            // Send response frame
            this->startTransmit(this->_txFrame, FAN_TX_RETRIES, [this]() {
              ESP_LOGW(TAG, "Join response timeout, restarting discovery");
              this->state_ = StateStartDiscovery;
            });

            this->state_ = StateDiscoveryJoinComplete;
          } else {
            ESP_LOGE(TAG, "Discovery: Received unknown link success from ID 0x%02X on network 0x%08X", pResponse->tx_id,
                     this->config_.fan_networkId);
          }
          break;

        default:
          ESP_LOGE(TAG, "Discovery: Received unknown frame type 0x%02X from ID 0x%02X", pResponse->command,
                   pResponse->tx_id);
          break;
      }
      break;

    case StateDiscoveryJoinComplete:
      ESP_LOGD(TAG, "Discovery state: join complete");
      switch (pResponse->command) {
        case FAN_TYPE_QUERY_NETWORK:
          if ((pResponse->rx_type == this->config_.fan_main_unit_type) &&
              (pResponse->rx_id == this->config_.fan_main_unit_id) &&
              (pResponse->tx_type == this->config_.fan_main_unit_type) &&
              (pResponse->tx_id == this->config_.fan_main_unit_id)) {
            ESP_LOGD(TAG, "Discovery: received network join success");

            this->rfComplete();

            ESP_LOGI(TAG, "Pairing completed successfully with main unit type 0x%02X ID 0x%02X", this->config_.fan_main_unit_type, this->config_.fan_main_unit_id);
            ESP_LOGD(TAG, "Saving pairing configuration");
            this->pref_.save(&this->config_);

            this->state_ = StateIdle;
          } else {
            ESP_LOGW(TAG, "Unexpected frame join response from Type 0x%02X ID 0x%02X", pResponse->tx_type,
                     pResponse->tx_id);
          }
          break;

        default:
          ESP_LOGE(TAG, "Discovery: Received unknown frame type 0x%02X from ID 0x%02X on network 0x%08X",
                   pResponse->command, pResponse->tx_id, this->config_.fan_networkId);
          break;
      }
      break;

    case StateWaitQueryResponse:
      if ((pResponse->rx_type == this->config_.fan_my_device_type) &&  // If type
          (pResponse->rx_id == this->config_.fan_my_device_id)) {      // and id match, it is for us
        switch (pResponse->command) {
          case FAN_TYPE_FAN_SETTINGS:
            ESP_LOGD(TAG, "Received fan settings; speed: 0x%02X voltage: %i timer: %i",
                     pResponse->payload.fanSettings.speed, pResponse->payload.fanSettings.voltage,
                     pResponse->payload.fanSettings.flags & 0x01);

            this->rfComplete();

            this->state = pResponse->payload.fanSettings.speed > 0;
            this->speed = pResponse->payload.fanSettings.speed;
            this->timer = (pResponse->payload.fanSettings.flags & 0x01) != 0;
            this->voltage = clamp_voltage(pResponse->payload.fanSettings.voltage);
            this->publish_state();

            this->state_ = StateIdle;
            break;

          default:
            ESP_LOGD(TAG, "Received unexpected frame; type 0x%02X from ID 0x%02X", pResponse->command,
                     pResponse->tx_id);
            break;
        }
      } else {
        ESP_LOGD(TAG, "Received frame from unknown device; type 0x%02X from ID 0x%02X type 0x%02X", pResponse->command,
                 pResponse->tx_id, pResponse->tx_type);
      }
      break;

    case StateWaitSetSpeedResponse:
      if ((pResponse->rx_type == this->config_.fan_my_device_type) &&  // If type
          (pResponse->rx_id == this->config_.fan_my_device_id)) {      // and id match, it is for us
        switch (pResponse->command) {
          case FAN_TYPE_FAN_SETTINGS:
            ESP_LOGD(TAG, "Received fan settings; speed: 0x%02X voltage: %i timer: %i",
                     pResponse->payload.fanSettings.speed, pResponse->payload.fanSettings.voltage,
                     pResponse->payload.fanSettings.flags & 0x01);

            this->rfComplete();

            this->state = pResponse->payload.fanSettings.speed > 0;
            this->speed = pResponse->payload.fanSettings.speed;
            this->timer = (pResponse->payload.fanSettings.flags & 0x01) != 0;
            this->voltage = clamp_voltage(pResponse->payload.fanSettings.voltage);
            this->publish_state();

            (void) memset(this->_txFrame, 0, FAN_FRAMESIZE);  // Clear frame data

            pTxFrame->rx_type = this->config_.fan_main_unit_type;  // Set type to main unit
            pTxFrame->rx_id = this->config_.fan_main_unit_id;      // Set ID to the ID of the main unit
            pTxFrame->tx_type = this->config_.fan_my_device_type;
            pTxFrame->tx_id = this->config_.fan_my_device_id;
            pTxFrame->ttl = FAN_TTL;
            pTxFrame->command = FAN_FRAME_SETSPEED_REPLY;  // 0x0B acknowledge link successful
            pTxFrame->parameter_count = 0x03;              // 3 parameters
            pTxFrame->payload.parameters[0] = 0x54;
            pTxFrame->payload.parameters[1] = 0x03;
            pTxFrame->payload.parameters[2] = 0x20;

            // Send response frame
            this->startTransmit(this->_txFrame, -1, NULL);

            this->state_ = StateWaitSetSpeedConfirm;
            break;

          case FAN_FRAME_SETSPEED_REPLY:
          case FAN_FRAME_SETVOLTAGE_REPLY:
            // this->rfComplete();

            // this->state_ = StateIdle;
            break;

          default:
            ESP_LOGD(TAG, "Received unexpected frame; type 0x%02X from ID 0x%02X", pResponse->command,
                     pResponse->tx_id);
            break;
        }
      } else {
        ESP_LOGD(TAG, "Received frame from unknown device; type 0x%02X from ID 0x%02X type 0x%02X", pResponse->command,
                 pResponse->tx_id, pResponse->tx_type);
      }
      break;

    case StateSniffer:
      // Passive capture: the decoded dump at the top of this function already logged the
      // frame under the 'zehnder.rf' tag; nothing further to process.
      break;

    default:
      ESP_LOGD(TAG, "Received frame from unknown device in unknown state; type 0x%02X from ID 0x%02X type 0x%02X",
               pResponse->command, pResponse->tx_id, pResponse->tx_type);
      break;
  }
}

uint8_t ZehnderRF::createDeviceID(void) {
  uint8_t random = (uint8_t) random_uint32();
  // Generate random device_id; don't use 0x00 and 0xFF

  // TODO: there's a 1 in 255 chance that the generated ID matches the ID of the main unit. Decide how to deal
  // withthis (some sort of ping discovery?)

  return minmax(random, 1, 0xFE);
}

void ZehnderRF::startSniffer(void) {
  nrf905::Config rfConfig = this->rf_->getConfig();

  // Prefer the paired fan network so we capture this unit's real traffic. If we were never
  // paired, fall back to the linking network so at least pairing broadcasts are visible.
  uint32_t network = this->config_.fan_networkId;
  if (network == 0x00000000) {
    network = NETWORK_LINK_ID;
    ESP_LOGW(TAG, "Sniffer: no paired network stored; listening on link network 0x%08X", network);
  } else {
    ESP_LOGI(TAG, "Sniffer: listening on paired network 0x%08X", network);
  }

  rfConfig.rx_address = network;
  this->rf_->updateConfig(&rfConfig);
  this->rf_->writeTxAddress(network);

  // Park the radio in receive mode. The nRF905 loop() will deliver every matching frame to
  // rfHandleReceived, which logs it under the 'zehnder.rf' tag. We never transmit or poll.
  this->rf_->setMode(nrf905::Receive);

  ESP_LOGI(TAG, "RF sniffer mode active: passively logging all frames (no TX, no polling)");
  this->state_ = StateSniffer;
}

void ZehnderRF::queryDevice(void) {  RfFrame *const pFrame = (RfFrame *) this->_txFrame;  // frame helper

  ESP_LOGD(TAG, "Query device");

  this->lastFanQuery_ = millis();  // Update time

  // Clear frame data
  (void) memset(this->_txFrame, 0, FAN_FRAMESIZE);

  // Build frame
  pFrame->rx_type = this->config_.fan_main_unit_type;
  pFrame->rx_id = this->config_.fan_main_unit_id;
  pFrame->tx_type = this->config_.fan_my_device_type;
  pFrame->tx_id = this->config_.fan_my_device_id;
  pFrame->ttl = FAN_TTL;
  pFrame->command = FAN_TYPE_QUERY_DEVICE;
  pFrame->parameter_count = 0x00;  // No parameters

  this->startTransmit(this->_txFrame, FAN_TX_RETRIES, [this]() {
    ESP_LOGW(TAG, "Device query timeout, returning to idle state");
    this->update_connection_status(false);
    this->state_ = StateIdle;
  });

  this->state_ = StateWaitQueryResponse;
}

void ZehnderRF::setSpeed(const uint8_t paramSpeed, const uint8_t paramTimer) {
  RfFrame *const pFrame = (RfFrame *) this->_txFrame;  // frame helper
  uint8_t speed = paramSpeed;
  uint8_t timer = paramTimer;

  if (speed > this->speed_count_) {
    ESP_LOGW(TAG, "Requested speed %u exceeds maximum %u, clamping to maximum", speed, this->speed_count_);
    speed = this->speed_count_;
  }

  ESP_LOGI(TAG, "Set speed: 0x%02X; Timer %u minutes", speed, timer);

  if (this->state_ == StateIdle) {
    (void) memset(this->_txFrame, 0, FAN_FRAMESIZE);  // Clear frame data

    // Build frame
    pFrame->rx_type = this->config_.fan_main_unit_type;
    pFrame->rx_id = 0x00;  // Broadcast
    // pFrame->tx_type = this->config_.fan_my_device_type;
    pFrame->tx_id = this->config_.fan_my_device_id;
    pFrame->ttl = FAN_TTL;

    if (timer == 0 && speed == 0) {
      // We want to switch to auto by setting both the timer and speed to 0
      // This mimics the Timer RF 'OFF' command.
      pFrame->tx_type = FAN_TYPE_TIMER_REMOTE_CONTROL;
      pFrame->command = FAN_FRAME_SETTIMER;
      pFrame->parameter_count = sizeof(RfPayloadFanSetTimer);
      pFrame->payload.setTimer.speed = speed;
      pFrame->payload.setTimer.timer = timer;
    }
    else if (timer == 0) {
      // Manual fixed-speed request (no timer).
      //
      // Previously this transmitted as a CO2 sensor (FAN_TYPE_CO2_SENSOR). That put our
      // command in the same arbitration "bucket" as the real CO2 sensor, so the sensor's
      // periodic Set speed frames would immediately override our setting (see issue: manual
      // set speed never sticks while a CO2 sensor is present).
      //
      // Instead we now transmit as the device we actually paired as (fan_my_device_type,
      // i.e. an RFZ remote control, 0x03). This is the standard manual controller and also
      // makes the fan's 0x07 reply addressable back to us (its rx_type/rx_id then match our
      // own type/id), fixing the reply-address mismatch that could flap the health sensor.
      pFrame->tx_type = this->config_.fan_my_device_type;
      pFrame->command = FAN_FRAME_SETSPEED;
      pFrame->parameter_count = sizeof(RfPayloadFanSetSpeed);
      pFrame->payload.setSpeed.speed = speed;
    } else {
      pFrame->tx_type = FAN_TYPE_TIMER_REMOTE_CONTROL;
      pFrame->command = FAN_FRAME_SETTIMER;
      pFrame->parameter_count = sizeof(RfPayloadFanSetTimer);
      pFrame->payload.setTimer.speed = speed;
      pFrame->payload.setTimer.timer = timer;
    }

    this->startTransmit(this->_txFrame, FAN_TX_RETRIES, [this]() {
      ESP_LOGW(TAG, "Set speed timeout, returning to idle state");
      this->update_connection_status(false);
      this->state_ = StateIdle;
    });

    newSetting = false;
    this->state_ = StateWaitSetSpeedResponse;
  } else {
    ESP_LOGD(TAG, "Invalid state for speed setting, will retry later");
    newSpeed = speed;
    newTimer = timer;
    newSpeedIsVoltage = false;
    newSetting = true;
  }
}

void ZehnderRF::setVoltage(const uint8_t percentage) {
  RfFrame *const pFrame = (RfFrame *) this->_txFrame;  // frame helper
  uint8_t voltage = percentage > 100 ? 100 : percentage;

  ESP_LOGI(TAG, "Set voltage: %u%%", voltage);

  if (this->state_ == StateIdle) {
    (void) memset(this->_txFrame, 0, FAN_FRAMESIZE);  // Clear frame data

    // Build frame - command 0x01 (Set voltage) for fine-grained percentage control.
    // Transmit as the device we paired as (RFZ remote control), consistent with setSpeed.
    pFrame->rx_type = this->config_.fan_main_unit_type;
    pFrame->rx_id = 0x00;  // Broadcast to all fans
    pFrame->tx_type = this->config_.fan_my_device_type;
    pFrame->tx_id = this->config_.fan_my_device_id;
    pFrame->ttl = FAN_TTL;
    pFrame->command = FAN_FRAME_SETVOLTAGE;
    pFrame->parameter_count = sizeof(RfPayloadFanSetVoltage);
    pFrame->payload.setVoltage.voltage = voltage;

    this->startTransmit(this->_txFrame, FAN_TX_RETRIES, [this]() {
      ESP_LOGW(TAG, "Set voltage timeout, returning to idle state");
      this->update_connection_status(false);
      this->state_ = StateIdle;
    });

    newSetting = false;
    this->state_ = StateWaitSetSpeedResponse;
  } else {
    ESP_LOGD(TAG, "Invalid state for voltage setting, will retry later");
    newVoltage = voltage;
    newSpeedIsVoltage = true;
    newSetting = true;
  }
}

void ZehnderRF::discoveryStart(const uint8_t deviceId) {
  RfFrame *const pFrame = (RfFrame *) this->_txFrame;  // frame helper
  nrf905::Config rfConfig;

  ESP_LOGD(TAG, "Starting discovery with device ID %u", deviceId);

  this->config_.fan_my_device_type = FAN_TYPE_REMOTE_CONTROL;
  this->config_.fan_my_device_id = deviceId;

  // Build frame
  (void) memset(this->_txFrame, 0, FAN_FRAMESIZE);  // Clear frame data

  // Set payload, available for linking
  pFrame->rx_type = 0x04;
  pFrame->rx_id = 0x00;
  pFrame->tx_type = this->config_.fan_my_device_type;
  pFrame->tx_id = this->config_.fan_my_device_id;
  pFrame->ttl = FAN_TTL;
  pFrame->command = FAN_NETWORK_JOIN_ACK;
  pFrame->parameter_count = sizeof(RfPayloadNetworkJoinAck);
  pFrame->payload.networkJoinAck.networkId = NETWORK_LINK_ID;

  // Set RX and TX address
  rfConfig = this->rf_->getConfig();
  rfConfig.rx_address = NETWORK_LINK_ID;
  this->rf_->updateConfig(&rfConfig, NULL);
  this->rf_->writeTxAddress(NETWORK_LINK_ID, NULL);

  this->startTransmit(this->_txFrame, FAN_TX_RETRIES, [this]() {
    ESP_LOGW(TAG, "Discovery start timeout, retrying");
    this->state_ = StateStartDiscovery;
  });

  // Update state
  this->state_ = StateDiscoveryWaitForLinkRequest;
}

Result ZehnderRF::startTransmit(const uint8_t *const pData, const int8_t rxRetries,
                                const std::function<void(void)> callback) {
  Result result = ResultOk;
  unsigned long startTime;
  bool busy = true;

  if (this->rfState_ != RfStateIdle) {
    ESP_LOGW(TAG, "RF transmission still ongoing, cannot start new transmission");
    result = ResultBusy;
  } else {
    this->onReceiveTimeout_ = callback;
    this->retries_ = rxRetries;

    // Write data to RF
    // if (pData != NULL) {  // If frame given, load it in the nRF. Else use previous TX payload
    this->rf_->writeTxPayload(pData, FAN_FRAMESIZE);  // Use framesize
    // }

    this->rfState_ = RfStateWaitAirwayFree;
    this->airwayFreeWaitTime_ = millis();
  }

  return result;
}

void ZehnderRF::rfComplete(void) {
  this->retries_ = -1;  // Disable this->retries_
  this->rfState_ = RfStateIdle;
  
  // Update connection status on successful communication
  this->update_connection_status(true);
}

void ZehnderRF::rfHandler(void) {
  switch (this->rfState_) {
    case RfStateIdle:
      break;

    case RfStateWaitAirwayFree:
      if ((millis() - this->airwayFreeWaitTime_) > 5000) {
        ESP_LOGW(TAG, "RF airway too busy, transmission timeout");
        this->rfState_ = RfStateIdle;

        if (this->onReceiveTimeout_ != NULL) {
          this->onReceiveTimeout_();
        }
      } else if (this->rf_->airwayBusy() == false) {
        ESP_LOGD(TAG, "Starting RF transmission");
        this->rf_->startTx(FAN_TX_FRAMES, nrf905::Receive);  // After transmit, wait for response

        this->rfState_ = RfStateTxBusy;
      }
      break;

    case RfStateTxBusy:
      break;

    case RfStateRxWait:
      if ((this->retries_ >= 0) && ((millis() - this->msgSendTime_) > FAN_REPLY_TIMEOUT)) {
        ESP_LOGD(TAG, "Receive timeout");

        if (this->retries_ > 0) {
          --this->retries_;
          ESP_LOGD(TAG, "No response received, retrying (%u attempts remaining)", this->retries_);

          this->rfState_ = RfStateWaitAirwayFree;
          this->airwayFreeWaitTime_ = millis();
        } else if (this->retries_ == 0) {
          // Oh oh, ran out of options

          ESP_LOGD(TAG, "No response received after all retries, giving up");
          if (this->onReceiveTimeout_ != NULL) {
            this->onReceiveTimeout_();
          }

          // Back to idle
          this->rfState_ = RfStateIdle;
        }
      }
      break;

    default:
      break;
  }
}

void ZehnderRF::update_connection_status(bool success) {
  if (success) {
    this->last_successful_communication_ = millis();
    this->consecutive_timeouts_ = 0;
    
    // If we were unhealthy, we're now healthy
    if (!this->connection_healthy_) {
      this->connection_healthy_ = true;
      ESP_LOGI(TAG, "Connection to ventilation system restored");
    }
  } else {
    this->consecutive_timeouts_++;
    ESP_LOGW(TAG, "Communication timeout (%u consecutive failures)", this->consecutive_timeouts_);
    
    // Consider connection unhealthy after 3 consecutive timeouts
    if (this->consecutive_timeouts_ >= 3 && this->connection_healthy_) {
      this->connection_healthy_ = false;
      ESP_LOGW(TAG, "Connection to ventilation system lost after %u consecutive failures", this->consecutive_timeouts_);
    }
  }
}

void ZehnderRF::check_connection_health() {
  // If we haven't had successful communication in too long, mark as unhealthy
  // Allow some grace time beyond the normal query interval
  uint32_t health_timeout = this->interval_ * 5; // 5x the query interval
  
  if (this->connection_healthy_ && this->last_successful_communication_ != 0) {
    if ((millis() - this->last_successful_communication_) > health_timeout) {
      this->connection_healthy_ = false;
      ESP_LOGW(TAG, "Connection to ventilation system timed out (no successful communication for %u ms)", 
               millis() - this->last_successful_communication_);
    }
  }
}

}  // namespace zehnder
}  // namespace esphome
