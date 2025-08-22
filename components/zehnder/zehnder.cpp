#include "zehnder.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace zehnder {

#define MAX_TRANSMIT_TIME 2000

static const char *const TAG = "zehnder";

// Forward declaration
static int clamp_voltage(const int value);

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
  uint8_t speed;
  uint8_t voltage;
  uint8_t timer;
} RfPayloadFanSettings;

typedef struct __attribute__((packed)) {
  uint8_t speed;
} RfPayloadFanSetSpeed;

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
  // Clamp voltage values to reasonable percentage range (0-100%)
  // This prevents invalid/corrupted RF data from causing extreme percentage values
  // in Home Assistant (e.g., ±1.5 billion % as reported in issue #18)
  if (value < 0) {
    ESP_LOGW(TAG, "Invalid voltage value %i clamped to 0", value);
    return 0;
  } else if (value > 100) {
    ESP_LOGW(TAG, "Invalid voltage value %i clamped to 100", value);
    return 100;
  } else {
    return value;
  }
}

ZehnderRF::ZehnderRF(void) {
  rf_healthy = true;  // Start assuming healthy connection
}

fan::FanTraits ZehnderRF::get_traits() { return fan::FanTraits(false, true, false, this->speed_count_); }

void ZehnderRF::control(const fan::FanCall &call) {
  if (call.get_state().has_value()) {
    this->state = *call.get_state();
    ESP_LOGD(TAG, "Control has state: %u", this->state);
  }
  if (call.get_speed().has_value()) {
    this->speed = *call.get_speed();
    ESP_LOGD(TAG, "Control has speed: %u", this->speed);
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
    ESP_LOGD(TAG, "Config load ok");
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
    ESP_LOGD(TAG, "Tx Ready");
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
    ESP_LOGV(TAG, "Received frame");
    this->rfHandleReceived(pData, dataLength);
  });
}

void ZehnderRF::dump_config(void) {
  ESP_LOGCONFIG(TAG, "Zehnder Fan config:");
  ESP_LOGCONFIG(TAG, "  Polling interval   %u", this->interval_);
  ESP_LOGCONFIG(TAG, "  Fan networkId      0x%08X", this->config_.fan_networkId);
  ESP_LOGCONFIG(TAG, "  Fan my device type 0x%02X", this->config_.fan_my_device_type);
  ESP_LOGCONFIG(TAG, "  Fan my device id   0x%02X", this->config_.fan_my_device_id);
  ESP_LOGCONFIG(TAG, "  Fan main_unit type 0x%02X", this->config_.fan_main_unit_type);
  ESP_LOGCONFIG(TAG, "  Fan main unit id   0x%02X", this->config_.fan_main_unit_id);
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
  ESP_LOGD(TAG, "Saving pairing config");
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
        ESP_LOGI(TAG, "Startup complete after 15 seconds, checking configuration");
        // Discovery?
        if ((this->config_.fan_networkId == 0x00000000) || (this->config_.fan_my_device_type == 0) ||
            (this->config_.fan_my_device_id == 0) || (this->config_.fan_main_unit_type == 0) ||
            (this->config_.fan_main_unit_id == 0)) {
          ESP_LOGI(TAG, "Configuration invalid - starting device discovery/pairing process");
          ESP_LOGD(TAG, "Config details: networkId=0x%08X, myType=0x%02X, myId=0x%02X, mainType=0x%02X, mainId=0x%02X",
                   this->config_.fan_networkId, this->config_.fan_my_device_type, this->config_.fan_my_device_id,
                   this->config_.fan_main_unit_type, this->config_.fan_main_unit_id);

          this->state_ = StateStartDiscovery;
        } else {
          ESP_LOGI(TAG, "Configuration valid - starting normal operation with polling");
          ESP_LOGD(TAG, "Using networkId=0x%08X for RF communication", this->config_.fan_networkId);

          rfConfig = this->rf_->getConfig();
          rfConfig.rx_address = this->config_.fan_networkId;
          this->rf_->updateConfig(&rfConfig);
          this->rf_->writeTxAddress(this->config_.fan_networkId);

          // Start with query
          this->queryDevice();
        }
      }
      break;

    case StateStartDiscovery:
      deviceId = this->createDeviceID();
      ESP_LOGI(TAG, "Starting device discovery with generated device ID: 0x%02X", deviceId);
      this->discoveryStart(deviceId);

      // For now just set TX
      break;

    case StateIdle:
      if (newSetting == true) {
        ESP_LOGV(TAG, "Processing queued speed change request");
        this->setSpeed(newSpeed, newTimer);
      } else {
        // Periodic status query
        if ((millis() - this->lastFanQuery_) > this->interval_) {
          ESP_LOGV(TAG, "Periodic query interval reached (%ums elapsed, interval: %ums)", 
                   millis() - this->lastFanQuery_, this->interval_);
          this->queryDevice();
        }
      }
      break;

    case StateWaitSetSpeedConfirm:
      if (this->rfState_ == RfStateIdle) {
        // When done, return to idle
        ESP_LOGD(TAG, "Speed change confirmation complete - State transition: %s -> %s", 
                 getStateName(this->state_), getStateName(StateIdle));
        this->state_ = StateIdle;
      }
      break;

    default:
      break;
  }
}

void ZehnderRF::rfHandleReceived(const uint8_t *const pData, const uint8_t dataLength) {
  const RfFrame *const pResponse = (RfFrame *) pData;
  RfFrame *const pTxFrame = (RfFrame *) this->_txFrame;  // frame helper
  nrf905::Config rfConfig;

  ESP_LOGV(TAG, "RF frame received - length: %u bytes, current state: %s (0x%02X)",
           dataLength, getStateName(this->state_), this->state_);
  ESP_LOGV(TAG, "Frame details: tx_type=0x%02X, tx_id=0x%02X, command=0x%02X, ttl=%u",
           pResponse->tx_type, pResponse->tx_id, pResponse->command, pResponse->ttl);
  
  switch (this->state_) {
    case StateDiscoveryWaitForLinkRequest:
      ESP_LOGD(TAG, "Processing discovery - waiting for link request");
      switch (pResponse->command) {
        case FAN_NETWORK_JOIN_OPEN:  // Received linking request from main unit
          ESP_LOGI(TAG, "Discovery: Found main unit type 0x%02X (%s) with ID 0x%02X on network 0x%08X",
                   pResponse->tx_type, pResponse->tx_type == FAN_TYPE_MAIN_UNIT ? "Main Unit" : "Unknown",
                   pResponse->tx_id, pResponse->payload.networkJoinOpen.networkId);

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
            ESP_LOGW(TAG, "Query Timeout");
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
      ESP_LOGD(TAG, "DiscoverStateWaitForJoinResponse");
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
              ESP_LOGW(TAG, "Query Timeout");
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
      ESP_LOGD(TAG, "StateDiscoveryJoinComplete");
      switch (pResponse->command) {
        case FAN_TYPE_QUERY_NETWORK:
          if ((pResponse->rx_type == this->config_.fan_main_unit_type) &&
              (pResponse->rx_id == this->config_.fan_main_unit_id) &&
              (pResponse->tx_type == this->config_.fan_main_unit_type) &&
              (pResponse->tx_id == this->config_.fan_main_unit_id)) {
            ESP_LOGI(TAG, "Discovery: Network join successful! Connected to main unit type=0x%02X, id=0x%02X",
                     pResponse->tx_type, pResponse->tx_id);

            this->rfComplete();
            
            // Mark RF communication as healthy after successful pairing
            this->updateRfHealth(true);

            ESP_LOGI(TAG, "Saving successful pairing configuration to preferences");
            this->pref_.save(&this->config_);

            ESP_LOGD(TAG, "State transition: %s -> %s", getStateName(this->state_), getStateName(StateIdle));
            this->state_ = StateIdle;
          } else {
            ESP_LOGW(TAG, "Unexpected join response - wrong device type/ID: rx_type=0x%02X rx_id=0x%02X tx_type=0x%02X tx_id=0x%02X", 
                     pResponse->rx_type, pResponse->rx_id, pResponse->tx_type, pResponse->tx_id);
          }
          break;

        default:
          ESP_LOGE(TAG, "Discovery: Received unknown frame type 0x%02X from device type=0x%02X id=0x%02X on network 0x%08X",
                   pResponse->command, pResponse->tx_type, pResponse->tx_id, this->config_.fan_networkId);
          break;
      }
      break;

    case StateWaitQueryResponse:
      ESP_LOGV(TAG, "Processing query response - checking if frame is addressed to us");
      if ((pResponse->rx_type == this->config_.fan_my_device_type) &&  // If type
          (pResponse->rx_id == this->config_.fan_my_device_id)) {      // and id match, it is for us
        ESP_LOGD(TAG, "Received response addressed to us (type=0x%02X, id=0x%02X)", 
                 pResponse->rx_type, pResponse->rx_id);
        switch (pResponse->command) {
          case FAN_TYPE_FAN_SETTINGS:
            ESP_LOGI(TAG, "Fan status received - Speed: %u (%s), Voltage: %d%%, Timer: %u min",
                     pResponse->payload.fanSettings.speed,
                     pResponse->payload.fanSettings.speed == 0 ? "Auto" :
                     pResponse->payload.fanSettings.speed == 1 ? "Low" :
                     pResponse->payload.fanSettings.speed == 2 ? "Medium" :
                     pResponse->payload.fanSettings.speed == 3 ? "High" :
                     pResponse->payload.fanSettings.speed == 4 ? "Max" : "Unknown",
                     pResponse->payload.fanSettings.voltage,
                     pResponse->payload.fanSettings.timer);

            this->rfComplete();

            // Update internal state
            this->state = pResponse->payload.fanSettings.speed > 0;
            this->speed = pResponse->payload.fanSettings.speed;
            this->timer = pResponse->payload.fanSettings.timer;
            this->voltage = clamp_voltage(pResponse->payload.fanSettings.voltage);
            ESP_LOGD(TAG, "Publishing updated fan state to Home Assistant");
            this->publish_state();

            // Mark RF communication as healthy
            this->updateRfHealth(true);

            ESP_LOGD(TAG, "State transition: %s -> %s", getStateName(this->state_), getStateName(StateIdle));
            this->state_ = StateIdle;
            break;

          default:
            ESP_LOGW(TAG, "Received unexpected command 0x%02X from main unit (type=0x%02X, id=0x%02X)", 
                     pResponse->command, pResponse->tx_type, pResponse->tx_id);
            break;
        }
      } else {
        ESP_LOGV(TAG, "Frame not addressed to us - rx_type=0x%02X (expected 0x%02X), rx_id=0x%02X (expected 0x%02X)", 
                 pResponse->rx_type, this->config_.fan_my_device_type,
                 pResponse->rx_id, this->config_.fan_my_device_id);
        ESP_LOGD(TAG, "Ignoring frame from device type=0x%02X id=0x%02X with command=0x%02X", 
                 pResponse->tx_type, pResponse->tx_id, pResponse->command);
      }
      break;

    case StateWaitSetSpeedResponse:
      ESP_LOGV(TAG, "Processing set speed response");
      if ((pResponse->rx_type == this->config_.fan_my_device_type) &&  // If type
          (pResponse->rx_id == this->config_.fan_my_device_id)) {      // and id match, it is for us
        switch (pResponse->command) {
          case FAN_TYPE_FAN_SETTINGS:
            ESP_LOGI(TAG, "Speed change confirmed - New settings: Speed: %u (%s), Voltage: %dV, Timer: %u min",
                     pResponse->payload.fanSettings.speed,
                     pResponse->payload.fanSettings.speed == 0 ? "Auto" :
                     pResponse->payload.fanSettings.speed == 1 ? "Low" :
                     pResponse->payload.fanSettings.speed == 2 ? "Medium" :
                     pResponse->payload.fanSettings.speed == 3 ? "High" :
                     pResponse->payload.fanSettings.speed == 4 ? "Max" : "Unknown",
                     pResponse->payload.fanSettings.voltage,
                     pResponse->payload.fanSettings.timer);
            // No idea why we need to commit twice, but got it from TimelessNL b4ae8c4
            ESP_LOGV(TAG, "Completing RF operation (double commit as per protocol requirements)");
            this->rfComplete();

            this->rfComplete();

            // Update internal state
            this->state = pResponse->payload.fanSettings.speed > 0;
            this->speed = pResponse->payload.fanSettings.speed;
            this->timer = pResponse->payload.fanSettings.timer;

            this->voltage = clamp_voltage(pResponse->payload.fanSettings.voltage);
            ESP_LOGD(TAG, "Publishing updated fan state after speed change");
            this->publish_state();

            // Mark RF communication as healthy
            this->updateRfHealth(true);

            ESP_LOGD(TAG, "Preparing acknowledgment frame for speed change");
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
            ESP_LOGD(TAG, "Sending speed change acknowledgment to main unit");
            this->startTransmit(this->_txFrame, -1, NULL);

            ESP_LOGD(TAG, "State transition: %s -> %s", getStateName(this->state_), getStateName(StateWaitSetSpeedConfirm));
            this->state_ = StateWaitSetSpeedConfirm;
            break;

          case FAN_FRAME_SETSPEED_REPLY:
          case FAN_FRAME_SETVOLTAGE_REPLY:
            ESP_LOGV(TAG, "Received acknowledgment frame (0x%02X) - operation completed", pResponse->command);
            // this->rfComplete();
            // this->state_ = StateIdle;
            break;

          default:
            ESP_LOGW(TAG, "Received unexpected command 0x%02X during speed change from device id=0x%02X", 
                     pResponse->command, pResponse->tx_id);
            break;
        }
      } else {
        ESP_LOGV(TAG, "Frame not for us during speed change - from device type=0x%02X id=0x%02X with command=0x%02X", 
                 pResponse->tx_type, pResponse->tx_id, pResponse->command);
      }
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

void ZehnderRF::queryDevice(void) {
  RfFrame *const pFrame = (RfFrame *) this->_txFrame;  // frame helper

  ESP_LOGD(TAG, "Querying main unit (type=0x%02X, id=0x%02X) for current status",
           this->config_.fan_main_unit_type, this->config_.fan_main_unit_id);

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
    ESP_LOGW(TAG, "Query timeout - main unit did not respond after %u retries", FAN_TX_RETRIES);
    this->updateRfHealth(false);
    this->state_ = StateIdle;
  });

  ESP_LOGD(TAG, "State transition: %s -> %s", getStateName(this->state_), getStateName(StateWaitQueryResponse));
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

  ESP_LOGI(TAG, "Setting fan speed to %u (%s) with timer %u minutes",
           speed, speed == 0 ? "Auto" : speed == 1 ? "Low" : speed == 2 ? "Medium" : speed == 3 ? "High" : speed == 4 ? "Max" : "Unknown", timer);

  if (this->state_ == StateIdle) {
    ESP_LOGD(TAG, "Device ready - building RF command frame");
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
      ESP_LOGD(TAG, "Creating AUTO mode command (FAN_FRAME_SETTIMER with speed=0, timer=0)");
      pFrame->command = FAN_FRAME_SETTIMER;
      pFrame->parameter_count = sizeof(RfPayloadFanSetTimer);
      pFrame->payload.setTimer.speed = speed;
      pFrame->payload.setTimer.timer = timer;
      ESP_LOGD(TAG, "Command payload: speed=%u, timer=%u", pFrame->payload.setTimer.speed, pFrame->payload.setTimer.timer);
    }
    else if (timer == 0) {
      ESP_LOGD(TAG, "Creating speed-only command (FAN_FRAME_SETSPEED) as CO2 sensor type");
      pFrame->tx_type = FAN_TYPE_CO2_SENSOR;
      pFrame->command = FAN_FRAME_SETSPEED;
      pFrame->parameter_count = sizeof(RfPayloadFanSetSpeed);
      pFrame->payload.setSpeed.speed = speed;
      ESP_LOGD(TAG, "Command payload: speed=%u", pFrame->payload.setSpeed.speed);
    } else {
      ESP_LOGD(TAG, "Creating timed speed command (FAN_FRAME_SETTIMER) as timer remote type");
      pFrame->tx_type = FAN_TYPE_TIMER_REMOTE_CONTROL;
      pFrame->command = FAN_FRAME_SETTIMER;
      pFrame->parameter_count = sizeof(RfPayloadFanSetTimer);
      pFrame->payload.setTimer.speed = speed;
      pFrame->payload.setTimer.timer = timer;
      ESP_LOGD(TAG, "Command payload: speed=%u, timer=%u", pFrame->payload.setTimer.speed, pFrame->payload.setTimer.timer);
    }

    this->startTransmit(this->_txFrame, FAN_TX_RETRIES, [this]() {
      ESP_LOGW(TAG, "Set speed command timeout - no response received after %u retries", FAN_TX_RETRIES);
      this->updateRfHealth(false);
      this->state_ = StateIdle;
    });

    newSetting = false;
    ESP_LOGD(TAG, "State transition: %s -> %s", getStateName(this->state_), getStateName(StateWaitSetSpeedResponse));
    this->state_ = StateWaitSetSpeedResponse;
  } else {
    ESP_LOGD(TAG, "Device busy (state: %s), queueing speed change for later", getStateName(this->state_));
    newSpeed = speed;
    newTimer = timer;
    newSetting = true;
  }
}

void ZehnderRF::discoveryStart(const uint8_t deviceId) {
  RfFrame *const pFrame = (RfFrame *) this->_txFrame;  // frame helper
  nrf905::Config rfConfig;

  ESP_LOGI(TAG, "Starting device discovery process with generated device ID: 0x%02X", deviceId);
  ESP_LOGD(TAG, "Configuring as remote control type for network joining");

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
  ESP_LOGV(TAG, "Discovery frame built: tx_type=0x%02X, tx_id=0x%02X, command=0x%02X", 
           pFrame->tx_type, pFrame->tx_id, pFrame->command);

  // Set RX and TX address
  ESP_LOGD(TAG, "Switching to discovery network (0x%08X) for pairing", NETWORK_LINK_ID);
  rfConfig = this->rf_->getConfig();
  rfConfig.rx_address = NETWORK_LINK_ID;
  this->rf_->updateConfig(&rfConfig, NULL);
  this->rf_->writeTxAddress(NETWORK_LINK_ID, NULL);

  this->startTransmit(this->_txFrame, FAN_TX_RETRIES, [this]() {
    ESP_LOGW(TAG, "Discovery timeout - no response from main unit during pairing process");
    this->updateRfHealth(false);
    this->state_ = StateStartDiscovery;
  });

  // Update state
  ESP_LOGD(TAG, "State transition: %s -> %s", getStateName(this->state_), getStateName(StateDiscoveryWaitForLinkRequest));
  this->state_ = StateDiscoveryWaitForLinkRequest;
}

Result ZehnderRF::startTransmit(const uint8_t *const pData, const int8_t rxRetries,
                                const std::function<void(void)> callback) {
  Result result = ResultOk;
  unsigned long startTime;
  bool busy = true;

  if (this->rfState_ != RfStateIdle) {
    ESP_LOGW(TAG, "TX still ongoing");
    result = ResultBusy;
  } else {
    this->onReceiveTimeout_ = callback;
    this->retries_ = rxRetries;

    // Write data to RF
    // if (pData != NULL) {  // If frame given, load it in the nRF. Else use previous TX payload
    // ESP_LOGD(TAG, "Write payload");
    ESP_LOGD(TAG, "Loading %u-byte payload into nRF905 TX buffer", FAN_FRAMESIZE);
    this->rf_->writeTxPayload(pData, FAN_FRAMESIZE);  // Use framesize
    // }

    ESP_LOGV(TAG, "RF state transition: %s -> RfStateWaitAirwayFree", 
             this->rfState_ == RfStateIdle ? "RfStateIdle" : "Other");
    this->rfState_ = RfStateWaitAirwayFree;
    this->airwayFreeWaitTime_ = millis();
    ESP_LOGD(TAG, "Waiting for airway to be free before transmission");
  }

  return result;
}

void ZehnderRF::rfComplete(void) {
  ESP_LOGD(TAG, "RF operation complete - resetting retry counter and returning to idle");
  this->retries_ = -1;  // Disable this->retries_
  this->rfState_ = RfStateIdle;
}

void ZehnderRF::rfHandler(void) {
  switch (this->rfState_) {
    case RfStateIdle:
      break;

    case RfStateWaitAirwayFree:
      if ((millis() - this->airwayFreeWaitTime_) > 5000) {
        ESP_LOGW(TAG, "Airway busy timeout after 5 seconds - aborting transmission");
        this->updateRfHealth(false);
        this->rfState_ = RfStateIdle;

        if (this->onReceiveTimeout_ != NULL) {
          this->onReceiveTimeout_();
        }
      } else if (this->rf_->airwayBusy() == false) {
        ESP_LOGD(TAG, "Airway clear - starting RF transmission with %u frames", FAN_TX_FRAMES);
        this->rf_->startTx(FAN_TX_FRAMES, nrf905::Receive);  // After transmit, wait for response

        this->rfState_ = RfStateTxBusy;
        this->msgSendTime_ = millis();  // Record transmission start time
      }
      break;

    case RfStateTxBusy:
      break;

    case RfStateRxWait:
      if ((this->retries_ >= 0) && ((millis() - this->msgSendTime_) > FAN_REPLY_TIMEOUT)) {
        ESP_LOGD(TAG, "RF receive timeout after %u ms", FAN_REPLY_TIMEOUT);

        if (this->retries_ > 0) {
          --this->retries_;
          ESP_LOGD(TAG, "No response received, retrying transmission (%u attempts remaining)", this->retries_);

          this->rfState_ = RfStateWaitAirwayFree;
          this->airwayFreeWaitTime_ = millis();
        } else if (this->retries_ == 0) {
          // Oh oh, ran out of options
          ESP_LOGW(TAG, "All retry attempts exhausted - no response received from fan unit");
          this->updateRfHealth(false);
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

const char* ZehnderRF::getStateName(State state) {
  switch (state) {
    case StateStartup: return "Startup";
    case StateStartDiscovery: return "StartDiscovery";
    case StateDiscoveryWaitForLinkRequest: return "DiscoveryWaitForLinkRequest";
    case StateDiscoveryWaitForJoinResponse: return "DiscoveryWaitForJoinResponse";
    case StateDiscoveryJoinComplete: return "DiscoveryJoinComplete";
    case StateIdle: return "Idle";
    case StateWaitQueryResponse: return "WaitQueryResponse";
    case StateWaitSetSpeedResponse: return "WaitSetSpeedResponse";
    case StateWaitSetSpeedConfirm: return "WaitSetSpeedConfirm";
    default: return "Unknown";
  }
}

void ZehnderRF::updateRfHealth(bool success) {
  uint32_t currentTime = millis();
  
  if (success) {
    // Reset failure counter and update last successful time
    this->rfFailureCount_ = 0;
    this->lastSuccessfulRfTime_ = currentTime;
    
    // If we were unhealthy, mark as healthy and log the recovery
    if (!this->rf_healthy) {
      ESP_LOGI(TAG, "RF communication restored - status sensor healthy");
      this->rf_healthy = true;
    }
  } else {
    // Increment failure counter
    this->rfFailureCount_++;
    
    // Consider unhealthy if multiple failures or no successful communication for extended period
    bool shouldBeUnhealthy = (this->rfFailureCount_ >= 3) || 
                             ((currentTime - this->lastSuccessfulRfTime_) > 300000);  // 5 minutes
    
    if (shouldBeUnhealthy && this->rf_healthy) {
      ESP_LOGW(TAG, "RF communication failed (%d failures, last success %d ms ago) - status sensor unhealthy", 
               this->rfFailureCount_, (currentTime - this->lastSuccessfulRfTime_));
      this->rf_healthy = false;
    }
  }
}

// ZehnderRFStatusSensor implementation
void ZehnderRFStatusSensor::setup() {
  ESP_LOGCONFIG(TAG, "ZehnderRF Status Sensor '%s'", this->get_name().c_str());
}

void ZehnderRFStatusSensor::loop() {
  if (parent_ != nullptr) {
    this->publish_state(parent_->rf_healthy);
  }
}

}  // namespace zehnder
}  // namespace esphome
