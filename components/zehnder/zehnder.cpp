#include "zehnder.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace zehnder {

#define MAX_TRANSMIT_TIME 2000

static const char *const TAG = "zehnder";

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
    ESP_LOGW(TAG, "Invalid voltage value %i detected (possible RF corruption), clamping to 0%%", value);
    return 0;
  } else if (value > 100) {
    ESP_LOGW(TAG, "Invalid voltage value %i detected (possible RF corruption), clamping to 100%%", value);
    return 100;
  } else {
    ESP_LOGV(TAG, "Voltage value %i%% is within valid range", value);
    return value;
  }
}

ZehnderRF::ZehnderRF(void) {}

fan::FanTraits ZehnderRF::get_traits() { return fan::FanTraits(false, true, false, this->speed_count_); }

void ZehnderRF::control(const fan::FanCall &call) {
  bool changed = false;
  
  if (call.get_state().has_value()) {
    bool new_state = *call.get_state();
    if (this->state != new_state) {
      ESP_LOGI(TAG, "Fan state change requested: %s -> %s", 
               this->state ? "ON" : "OFF", new_state ? "ON" : "OFF");
      this->state = new_state;
      changed = true;
    }
  }
  
  if (call.get_speed().has_value()) {
    int new_speed = *call.get_speed();
    if (this->speed != new_speed) {
      ESP_LOGI(TAG, "Fan speed change requested: %u -> %u", this->speed, new_speed);
      this->speed = new_speed;
      changed = true;
    }
  }

  if (!changed) {
    ESP_LOGV(TAG, "Control called but no changes requested");
    return;
  }

  switch (this->state_) {
    case StateIdle:
      uint8_t target_speed = this->state ? this->speed : 0x00;
      ESP_LOGI(TAG, "Setting fan speed to %u (state: %s)", target_speed, this->state ? "ON" : "OFF");
      
      // Set speed
      this->setSpeed(target_speed, 0);

      this->lastFanQuery_ = millis();  // Update time
      ESP_LOGD(TAG, "Updated last query time for polling schedule");
      break;

    default:
      ESP_LOGW(TAG, "Control request ignored - device not in idle state (current state: %d)", this->state_);
      break;
  }

  ESP_LOGD(TAG, "Publishing updated state to Home Assistant");
  this->publish_state();
}

void ZehnderRF::setup() {
  ESP_LOGI(TAG, "Initializing Zehnder RF component '%s'", this->get_name().c_str());

  // Clear config
  ESP_LOGD(TAG, "Clearing configuration structure");
  memset(&this->config_, 0, sizeof(Config));

  ESP_LOGD(TAG, "Loading stored configuration from preferences");
  uint32_t hash = fnv1_hash("zehnderrf");
  this->pref_ = global_preferences->make_preference<Config>(hash, true);
  if (this->pref_.load(&this->config_)) {
    ESP_LOGI(TAG, "Stored configuration loaded successfully");
  } else {
    ESP_LOGW(TAG, "No stored configuration found, will require pairing");
  }

  // Set nRF905 config
  ESP_LOGD(TAG, "Configuring nRF905 radio for Zehnder protocol");
  nrf905::Config rfConfig;
  rfConfig = this->rf_->getConfig();

  rfConfig.band = true;     // 868MHz band
  rfConfig.channel = 118;   // Zehnder uses channel 118

  // CRC 16
  rfConfig.crc_enable = true;
  rfConfig.crc_bits = 16;

  // TX power 10dBm
  rfConfig.tx_power = 10;

  // RX power normal
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
  static State lastState = StateStartup;

  // Run RF handler
  this->rfHandler();

  // Log state transitions
  if (this->state_ != lastState) {
    const char* state_names[] = {
      "StateStartup", "StateStartDiscovery", "StateDiscoveryWaitForLinkRequest", 
      "StateDiscoveryWaitForJoinResponse", "StateDiscoveryJoinComplete",
      "StateIdle", "StateWaitQueryResponse", "StateWaitSetSpeedResponse", "StateWaitSetSpeedConfirm"
    };
    ESP_LOGI(TAG, "State transition: %s -> %s", 
             lastState < StateNrOf ? state_names[lastState] : "Unknown",
             this->state_ < StateNrOf ? state_names[this->state_] : "Unknown");
    lastState = this->state_;
  }

  switch (this->state_) {
    case StateStartup:
      // Wait until started up
      if (millis() > 15000) {
        ESP_LOGD(TAG, "Startup period complete, checking configuration");
        // Discovery?
        if ((this->config_.fan_networkId == 0x00000000) || (this->config_.fan_my_device_type == 0) ||
            (this->config_.fan_my_device_id == 0) || (this->config_.fan_main_unit_type == 0) ||
            (this->config_.fan_main_unit_id == 0)) {
          ESP_LOGI(TAG, "No valid pairing configuration found, starting device discovery");

          this->state_ = StateStartDiscovery;
        } else {
          ESP_LOGI(TAG, "Valid pairing configuration found (Network ID: 0x%08X), starting normal operation", 
                   this->config_.fan_networkId);

          rfConfig = this->rf_->getConfig();
          rfConfig.rx_address = this->config_.fan_networkId;
          this->rf_->updateConfig(&rfConfig);
          this->rf_->writeTxAddress(this->config_.fan_networkId);

          // Start with query
          ESP_LOGD(TAG, "Performing initial device query");
          this->queryDevice();
        }
      }
      break;

    case StateStartDiscovery:
      deviceId = this->createDeviceID();
      ESP_LOGI(TAG, "Starting device discovery with device ID: 0x%02X", deviceId);
      this->discoveryStart(deviceId);

      // For now just set TX
      break;

    case StateIdle:
      if (newSetting == true) {
        ESP_LOGD(TAG, "New speed setting requested: %u (timer: %u)", newSpeed, newTimer);
        this->setSpeed(newSpeed, newTimer);
      } else {
        if ((millis() - this->lastFanQuery_) > this->interval_) {
          ESP_LOGV(TAG, "Performing periodic device query (interval: %u ms)", this->interval_);
          this->queryDevice();
        }
      }
      break;

    case StateWaitSetSpeedConfirm:
      if (this->rfState_ == RfStateIdle) {
        // When done, return to idle
        this->state_ = StateIdle;
      }

    default:
      break;
  }
}

void ZehnderRF::rfHandleReceived(const uint8_t *const pData, const uint8_t dataLength) {
  const RfFrame *const pResponse = (RfFrame *) pData;
  RfFrame *const pTxFrame = (RfFrame *) this->_txFrame;  // frame helper
  nrf905::Config rfConfig;

  ESP_LOGD(TAG, "Processing received RF frame in state: %d (data length: %u)", this->state_, dataLength);
  
  // Log basic frame info
  ESP_LOGV(TAG, "Frame: from 0x%02X:0x%02X to 0x%02X:0x%02X, cmd=0x%02X, ttl=%u", 
           pResponse->tx_type, pResponse->tx_id, pResponse->rx_type, pResponse->rx_id,
           pResponse->command, pResponse->ttl);

  switch (this->state_) {
    case StateDiscoveryWaitForLinkRequest:
      ESP_LOGD(TAG, "Processing discovery link request");
      switch (pResponse->command) {
        case FAN_NETWORK_JOIN_OPEN:  // Received linking request from main unit
          ESP_LOGI(TAG, "Discovery: Found unit type 0x%02X (%s) with ID 0x%02X on network 0x%08X", 
                   pResponse->tx_type,
                   pResponse->tx_type == FAN_TYPE_MAIN_UNIT ? "Main Unit" : "Unknown", 
                   pResponse->tx_id,
                   pResponse->payload.networkJoinOpen.networkId);

          this->rfComplete();

          ESP_LOGD(TAG, "Building join request frame for discovered unit");
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
          ESP_LOGD(TAG, "Storing network configuration for pairing");
          this->config_.fan_networkId = pResponse->payload.networkJoinOpen.networkId;
          this->config_.fan_main_unit_type = pResponse->tx_type;
          this->config_.fan_main_unit_id = pResponse->tx_id;

          // Update address
          ESP_LOGD(TAG, "Updating radio configuration for network 0x%08X", pResponse->payload.networkJoinOpen.networkId);
          rfConfig = this->rf_->getConfig();
          rfConfig.rx_address = pResponse->payload.networkJoinOpen.networkId;
          this->rf_->updateConfig(&rfConfig, NULL);
          this->rf_->writeTxAddress(pResponse->payload.networkJoinOpen.networkId, NULL);

          // Send response frame
          ESP_LOGI(TAG, "Sending join request to main unit");
          this->startTransmit(this->_txFrame, FAN_TX_RETRIES, [this]() {
            ESP_LOGE(TAG, "Join request timeout - main unit did not respond");
            ESP_LOGI(TAG, "Restarting discovery process");
            this->state_ = StateStartDiscovery;
          });

          this->state_ = StateDiscoveryWaitForJoinResponse;
          ESP_LOGI(TAG, "Join request sent, waiting for confirmation from main unit");
          break;

        default:
          ESP_LOGW(TAG, "Discovery: Received unexpected frame type 0x%02X from device 0x%02X:0x%02X", 
                   pResponse->command, pResponse->tx_type, pResponse->tx_id);
          break;
      }
      break;

    case StateDiscoveryWaitForJoinResponse:
      ESP_LOGD(TAG, "Processing join response");
      switch (pResponse->command) {
        case FAN_FRAME_0B:
          if ((pResponse->rx_type == this->config_.fan_my_device_type) &&
              (pResponse->rx_id == this->config_.fan_my_device_id) &&
              (pResponse->tx_type == this->config_.fan_main_unit_type) &&
              (pResponse->tx_id == this->config_.fan_main_unit_id)) {
            ESP_LOGI(TAG, "Discovery: Pairing successful! Connected to unit ID 0x%02X on network 0x%08X", 
                     pResponse->tx_id, this->config_.fan_networkId);

            this->rfComplete();

            ESP_LOGD(TAG, "Building network query response");
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
            ESP_LOGD(TAG, "Discovery: received network join success 0x0D");

            this->rfComplete();

            ESP_LOGD(TAG, "Saving pairing config");
            this->pref_.save(&this->config_);

            this->state_ = StateIdle;
          } else {
            ESP_LOGW(TAG, "Unexpected frame join reponse from Type 0x%02X ID 0x%02X", pResponse->tx_type,
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
                     pResponse->payload.fanSettings.timer);

            this->rfComplete();

            this->state = pResponse->payload.fanSettings.speed > 0;
            this->speed = pResponse->payload.fanSettings.speed;
            this->timer = pResponse->payload.fanSettings.timer;
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
                     pResponse->payload.fanSettings.timer);
            // No idea why we need to commit twice, but got it from TimelessNL b4ae8c4
            this->rfComplete();

            this->rfComplete();

            this->state = pResponse->payload.fanSettings.speed > 0;
            this->speed = pResponse->payload.fanSettings.speed;
            this->timer = pResponse->payload.fanSettings.timer;
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

  uint8_t device_id = minmax(random, 1, 0xFE);
  ESP_LOGD(TAG, "Generated random device ID: 0x%02X", device_id);
  return device_id;
}

void ZehnderRF::queryDevice(void) {
  RfFrame *const pFrame = (RfFrame *) this->_txFrame;  // frame helper

  ESP_LOGD(TAG, "Querying fan device for status update");

  this->lastFanQuery_ = millis();  // Update time
  ESP_LOGV(TAG, "Updated last query timestamp to %lu", this->lastFanQuery_);

  // Clear frame data
  ESP_LOGV(TAG, "Building device query frame");
  (void) memset(this->_txFrame, 0, FAN_FRAMESIZE);

  // Build frame
  pFrame->rx_type = this->config_.fan_main_unit_type;
  pFrame->rx_id = this->config_.fan_main_unit_id;
  pFrame->tx_type = this->config_.fan_my_device_type;
  pFrame->tx_id = this->config_.fan_my_device_id;
  pFrame->ttl = FAN_TTL;
  pFrame->command = FAN_TYPE_QUERY_DEVICE;
  pFrame->parameter_count = 0x00;  // No parameters
  
  ESP_LOGV(TAG, "Query frame: to 0x%02X:0x%02X from 0x%02X:0x%02X", 
           pFrame->rx_type, pFrame->rx_id, pFrame->tx_type, pFrame->tx_id);

  this->startTransmit(this->_txFrame, FAN_TX_RETRIES, [this]() {
    ESP_LOGW(TAG, "Device query timeout - fan unit not responding");
    this->state_ = StateIdle;
  });

  this->state_ = StateWaitQueryResponse;
}

void ZehnderRF::setSpeed(const uint8_t paramSpeed, const uint8_t paramTimer) {
  RfFrame *const pFrame = (RfFrame *) this->_txFrame;  // frame helper
  uint8_t speed = paramSpeed;
  uint8_t timer = paramTimer;

  if (speed > this->speed_count_) {
    ESP_LOGW(TAG, "Requested speed %u exceeds maximum %u, clamping", speed, this->speed_count_);
    speed = this->speed_count_;
  }

  ESP_LOGI(TAG, "Setting fan speed to %u (0x%02X) with timer %u minutes", speed, speed, timer);

  if (this->state_ != StateIdle) {
    ESP_LOGW(TAG, "Cannot set speed - device not in idle state (current: %d)", this->state_);
    return;
  }

  ESP_LOGD(TAG, "Building RF frame for speed/timer command");
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
    ESP_LOGD(TAG, "Sending AUTO mode command (timer OFF)");
    pFrame->command = FAN_FRAME_SETTIMER;
    pFrame->parameter_count = sizeof(RfPayloadFanSetTimer);
    pFrame->payload.setTimer.speed = speed;
    pFrame->payload.setTimer.timer = timer;
  }
  else if (timer == 0) {
    ESP_LOGD(TAG, "Sending continuous speed command (CO2 sensor type)");
    pFrame->tx_type = FAN_TYPE_CO2_SENSOR;
    pFrame->command = FAN_FRAME_SETSPEED;
    pFrame->parameter_count = sizeof(RfPayloadFanSetSpeed);
    pFrame->payload.setSpeed.speed = speed;
  } else {
    ESP_LOGD(TAG, "Sending timed speed command (timer remote type)");
    pFrame->tx_type = FAN_TYPE_TIMER_REMOTE_CONTROL;
    pFrame->command = FAN_FRAME_SETTIMER;
    pFrame->parameter_count = sizeof(RfPayloadFanSetTimer);
    pFrame->payload.setTimer.speed = speed;
    pFrame->payload.setTimer.timer = timer;
  }

  ESP_LOGD(TAG, "Frame: type=0x%02X, cmd=0x%02X, params=%u", 
           pFrame->tx_type, pFrame->command, pFrame->parameter_count);

  this->startTransmit(this->_txFrame, FAN_TX_RETRIES, [this]() {
    ESP_LOGE(TAG, "Set speed command timeout - no response from fan unit");
    this->state_ = StateIdle;
  });

  newSetting = false;
  ESP_LOGI(TAG, "Speed command transmitted, waiting for response");
  this->state_ = StateWaitSetSpeedResponse;
  } else {
    ESP_LOGW(TAG, "Device not in idle state, queuing speed change for later (current state: %d)", this->state_);
    newSpeed = speed;
    newTimer = timer;
    newSetting = true;
  }
}

void ZehnderRF::discoveryStart(const uint8_t deviceId) {
  RfFrame *const pFrame = (RfFrame *) this->_txFrame;  // frame helper
  nrf905::Config rfConfig;

  ESP_LOGI(TAG, "Starting device discovery process with device ID: 0x%02X", deviceId);

  this->config_.fan_my_device_type = FAN_TYPE_REMOTE_CONTROL;
  this->config_.fan_my_device_id = deviceId;
  
  ESP_LOGD(TAG, "Set local device type to REMOTE_CONTROL (0x%02X) with ID 0x%02X", 
           FAN_TYPE_REMOTE_CONTROL, deviceId);

  // Build frame
  ESP_LOGD(TAG, "Building network join acknowledgement frame");
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
  
  ESP_LOGD(TAG, "Join frame: tx_type=0x%02X, tx_id=0x%02X, network=0x%08X", 
           pFrame->tx_type, pFrame->tx_id, NETWORK_LINK_ID);

  // Set RX and TX address
  ESP_LOGD(TAG, "Configuring radio for discovery mode (network ID: 0x%08X)", NETWORK_LINK_ID);
  rfConfig = this->rf_->getConfig();
  rfConfig.rx_address = NETWORK_LINK_ID;
  this->rf_->updateConfig(&rfConfig, NULL);
  this->rf_->writeTxAddress(NETWORK_LINK_ID, NULL);

  this->startTransmit(this->_txFrame, FAN_TX_RETRIES, [this]() {
    ESP_LOGE(TAG, "Discovery broadcast timeout - no response from fan units");
    ESP_LOGI(TAG, "Retrying discovery process");
    this->state_ = StateStartDiscovery;
  });

  // Update state
  ESP_LOGI(TAG, "Discovery frame transmitted, waiting for link request from fan unit");
  this->state_ = StateDiscoveryWaitForLinkRequest;
}

Result ZehnderRF::startTransmit(const uint8_t *const pData, const int8_t rxRetries,
                                const std::function<void(void)> callback) {
  Result result = ResultOk;
  unsigned long startTime;
  bool busy = true;

  if (this->rfState_ != RfStateIdle) {
    ESP_LOGW(TAG, "Cannot start transmission - RF handler is busy (state: %d)", this->rfState_);
    result = ResultBusy;
  } else {
    ESP_LOGD(TAG, "Starting RF transmission (retries: %d)", rxRetries);
    
    this->onReceiveTimeout_ = callback;
    this->retries_ = rxRetries;

    // Write data to RF
    // if (pData != NULL) {  // If frame given, load it in the nRF. Else use previous TX payload
    ESP_LOGV(TAG, "Writing %d bytes to RF transmit buffer", FAN_FRAMESIZE);
    this->rf_->writeTxPayload(pData, FAN_FRAMESIZE);  // Use framesize
    // }

    this->rfState_ = RfStateWaitAirwayFree;
    this->airwayFreeWaitTime_ = millis();
    this->msgSendTime_ = millis();  // Track when we started the transmission
    
    ESP_LOGD(TAG, "RF transmission initiated, waiting for clear airway");
  }

  return result;
}

void ZehnderRF::rfComplete(void) {
  ESP_LOGD(TAG, "RF operation completed successfully");
  this->retries_ = -1;  // Disable this->retries_
  this->rfState_ = RfStateIdle;
}

void ZehnderRF::rfHandler(void) {
  static RfState lastRfState = RfStateIdle;
  
  // Log RF state transitions
  if (this->rfState_ != lastRfState) {
    const char* rf_state_names[] = {"RfStateIdle", "RfStateWaitAirwayFree", "RfStateTxBusy", "RfStateRxWait"};
    ESP_LOGD(TAG, "RF state transition: %s -> %s", 
             rf_state_names[lastRfState], rf_state_names[this->rfState_]);
    lastRfState = this->rfState_;
  }

  switch (this->rfState_) {
    case RfStateIdle:
      break;

    case RfStateWaitAirwayFree:
      if ((millis() - this->airwayFreeWaitTime_) > 5000) {
        ESP_LOGW(TAG, "Airway busy timeout after 5000ms, aborting transmission");
        this->rfState_ = RfStateIdle;

        if (this->onReceiveTimeout_ != NULL) {
          this->onReceiveTimeout_();
        } else {
          ESP_LOGW(TAG, "No timeout callback registered");
        }
      } else if (this->rf_->airwayBusy() == false) {
        ESP_LOGI(TAG, "Airway clear, starting transmission (%d retransmissions)", FAN_TX_FRAMES);
        this->rf_->startTx(FAN_TX_FRAMES, nrf905::Receive);  // After transmit, wait for response

        this->rfState_ = RfStateTxBusy;
      } else {
        // Log periodically to avoid spam
        static unsigned long lastBusyLog = 0;
        if (millis() - lastBusyLog > 1000) {
          ESP_LOGV(TAG, "Waiting for airway to become free (elapsed: %lu ms)", 
                   millis() - this->airwayFreeWaitTime_);
          lastBusyLog = millis();
        }
      }
      break;

    case RfStateTxBusy:
      // Transmission in progress, waiting for nRF905 to complete
      break;

    case RfStateRxWait:
      if ((this->retries_ >= 0) && ((millis() - this->msgSendTime_) > FAN_REPLY_TIMEOUT)) {
        ESP_LOGW(TAG, "Reply timeout after %d ms", FAN_REPLY_TIMEOUT);

        if (this->retries_ > 0) {
          --this->retries_;
          ESP_LOGI(TAG, "Retrying transmission (attempts remaining: %u)", this->retries_);

          this->rfState_ = RfStateWaitAirwayFree;
          this->airwayFreeWaitTime_ = millis();
        } else if (this->retries_ == 0) {
          // Oh oh, ran out of options

          ESP_LOGE(TAG, "All retry attempts exhausted, giving up on transmission");
          if (this->onReceiveTimeout_ != NULL) {
            this->onReceiveTimeout_();
          } else {
            ESP_LOGW(TAG, "No timeout callback registered");
          }

          // Back to idle
          this->rfState_ = RfStateIdle;
        }
      } else {
        // Log waiting status periodically
        static unsigned long lastWaitLog = 0;
        if (millis() - lastWaitLog > 500) {
          ESP_LOGV(TAG, "Waiting for reply (elapsed: %lu ms, retries left: %d)", 
                   millis() - this->msgSendTime_, this->retries_);
          lastWaitLog = millis();
        }
      }
      break;

    default:
      ESP_LOGE(TAG, "Unknown RF state: %d", this->rfState_);
      this->rfState_ = RfStateIdle;
      break;
  }
}

}  // namespace zehnder
}  // namespace esphome
