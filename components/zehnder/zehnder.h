#ifndef __COMPONENT_ZEHNDER_H__
#define __COMPONENT_ZEHNDER_H__

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/spi/spi.h"
#include "esphome/components/fan/fan_state.h"
#include "esphome/components/nrf905/nRF905.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace zehnder {

#define FAN_FRAMESIZE 16        // Each frame consists of 16 bytes
#define FAN_TX_FRAMES 4         // Retransmit every transmitted frame 4 times
#define FAN_TX_RETRIES 10       // Retry transmission 10 times if no reply is received
#define FAN_TTL 250             // 0xFA, default time-to-live for a frame
#define FAN_REPLY_TIMEOUT 2000  // Wait 2000ms for receiving a reply

/* Fan device types */
// Ref: https://github.com/eelcohn/ZehnderComfoair#transmitter-and-receiver-types
enum {
  FAN_TYPE_BROADCAST = 0x00,            // Broadcast to all devices
  FAN_TYPE_MAIN_UNIT = 0x01,            // Fans
  FAN_TYPE_REMOTE_CONTROL = 0x03,       // Remote controls
  FAN_TYPE_TIMER_REMOTE_CONTROL = 0x16, // Timer RF remote control. The one with 10/30/60/timer off buttons
  FAN_TYPE_CO2_SENSOR = 0x18
};  // CO2 sensors

/* Fan commands */
enum {
  FAN_FRAME_SETVOLTAGE = 0x01,  // Set speed (voltage / percentage)
  FAN_FRAME_SETSPEED = 0x02,    // Set speed (preset)
  FAN_FRAME_SETTIMER = 0x03,    // Set speed with timer
  FAN_NETWORK_JOIN_REQUEST = 0x04,
  FAN_FRAME_SETSPEED_REPLY = 0x05,
  FAN_NETWORK_JOIN_OPEN = 0x06,
  FAN_TYPE_FAN_SETTINGS = 0x07,  // Current settings, sent by fan in reply to 0x01, 0x02, 0x10
  FAN_FRAME_0B = 0x0B,
  FAN_NETWORK_JOIN_ACK = 0x0C,
  // FAN_NETWORK_JOIN_FINISH = 0x0D,
  FAN_TYPE_QUERY_NETWORK = 0x0D,
  FAN_TYPE_QUERY_DEVICE = 0x10,
  FAN_FRAME_SETVOLTAGE_REPLY = 0x1D,

  // New diagnostic commands
  FAN_TYPE_QUERY_ERROR_STATUS = 0x30,    // Request error codes
  FAN_TYPE_ERROR_STATUS_RESPONSE = 0x31, // Response with error codes
  FAN_TYPE_QUERY_FILTER_STATUS = 0x32,   // Request filter status
  FAN_TYPE_FILTER_STATUS_RESPONSE = 0x33 // Response with filter status
};

/* Fan speed presets */
enum {
  FAN_SPEED_AUTO = 0x00,    // Off:      0% or  0.0 volt
  FAN_SPEED_LOW = 0x01,     // Low:     30% or  3.0 volt
  FAN_SPEED_MEDIUM = 0x02,  // Medium:  50% or  5.0 volt
  FAN_SPEED_HIGH = 0x03,    // High:    90% or  9.0 volt
  FAN_SPEED_MAX = 0x04
};  // Max:    100% or 10.0 volt

#define NETWORK_LINK_ID 0xA55A5AA5
#define NETWORK_DEFAULT_ID 0xE7E7E7E7
#define FAN_JOIN_DEFAULT_TIMEOUT 10000

typedef enum { ResultOk, ResultBusy, ResultFailure } Result;

class ZehnderRF : public Component, public fan::Fan {
 public:
  ZehnderRF();

  void setup() override;

  // Setup things
  void set_rf(nrf905::nRF905 *const pRf) { rf_ = pRf; }

  void set_update_interval(const uint32_t interval) { interval_ = interval; }

  // Sensors
  void set_filter_remaining_sensor(sensor::Sensor *sensor) { filter_remaining_sensor_ = sensor; }
  void set_filter_runtime_sensor(sensor::Sensor *sensor) { filter_runtime_sensor_ = sensor; }
  void set_error_count_sensor(sensor::Sensor *sensor) { error_count_sensor_ = sensor; }
  void set_error_code_sensor(text_sensor::TextSensor *sensor) { error_code_sensor_ = sensor; }

  void dump_config() override;
  void set_config(const uint32_t fan_networkId,
                  const uint8_t  fan_my_device_type,
                  const uint8_t  fan_my_device_id,
                  const uint8_t  fan_main_unit_type,
                  const uint8_t  fan_main_unit_id);

  fan::FanTraits get_traits() override;
  int get_speed_count() { return this->speed_count_; }

  void loop() override;

  void control(const fan::FanCall &call) override;

  float get_setup_priority() const override { return setup_priority::DATA; }

  void setSpeed(const uint8_t speed, const uint8_t timer = 0);

  bool timer;
  int voltage;

 protected:
  // State enum definition - needs to be before getStateName declaration
  typedef enum {
    StateStartup,
    StateStartDiscovery,
    StateDiscoveryWaitForLinkRequest,
    StateDiscoveryWaitForJoinResponse,
    StateDiscoveryJoinComplete,

    StateIdle,
    StateWaitQueryResponse,
    StateWaitSetSpeedResponse,
    StateWaitSetSpeedConfirm,

    // New states for diagnostic queries
    StateWaitFilterStatusResponse,
    StateWaitErrorStatusResponse,

    StateNrOf  // Keep last
  } State;

  void queryDevice(void);
  void queryErrorStatus(void);
  void queryFilterStatus(void);

  uint8_t createDeviceID(void);
  void discoveryStart(const uint8_t deviceId);

  Result startTransmit(const uint8_t *const pData, const int8_t rxRetries = -1,
                       const std::function<void(void)> callback = NULL);
  void rfComplete(void);
  void rfHandler(void);
  void rfHandleReceived(const uint8_t *const pData, const uint8_t dataLength);

  // Helper function to get readable state names for logging
  const char* getStateName(State state);
  State state_{StateStartup};
  int speed_count_{};

  nrf905::nRF905 *rf_;
  uint32_t interval_;

  // Sensors
  sensor::Sensor *filter_remaining_sensor_{nullptr};
  sensor::Sensor *filter_runtime_sensor_{nullptr};
  sensor::Sensor *error_count_sensor_{nullptr};
  text_sensor::TextSensor *error_code_sensor_{nullptr};

  uint8_t _txFrame[FAN_FRAMESIZE];

  ESPPreferenceObject pref_;

  typedef struct {
    uint32_t fan_networkId;      // Fan (Zehnder/BUVA) network ID
    uint8_t fan_my_device_type;  // Fan (Zehnder/BUVA) device type
    uint8_t fan_my_device_id;    // Fan (Zehnder/BUVA) device ID
    uint8_t fan_main_unit_type;  // Fan (Zehnder/BUVA) main unit type
    uint8_t fan_main_unit_id;    // Fan (Zehnder/BUVA) main unit ID
  } Config;
  Config config_;

  uint32_t lastFanQuery_{0};
  uint32_t lastFilterQuery_{0};
  uint32_t lastErrorQuery_{0};
  std::function<void(void)> onReceiveTimeout_ = NULL;

  uint32_t msgSendTime_{0};
  uint32_t airwayFreeWaitTime_{0};
  int8_t retries_{-1};

  uint8_t newSpeed{0};
  uint8_t newTimer{0};
  bool newSetting{false};

  typedef enum {
    RfStateIdle,            // Idle state
    RfStateWaitAirwayFree,  // wait for airway free
    RfStateTxBusy,          //
    RfStateRxWait,
  } RfState;
  RfState rfState_{RfStateIdle};
};

// New payload structures for diagnostic features
typedef struct __attribute__((packed)) {
  uint8_t errorCount;      // Number of active errors
  uint8_t errorCodes[5];   // Array of error codes
  uint8_t errorSeverity;   // Severity level (warning/critical)
} RfPayloadErrorStatus;

typedef struct __attribute__((packed)) {
  uint16_t totalRunHours;       // Total operation hours
  uint16_t filterRunHours;      // Hours since last filter change
  uint8_t filterPercentRemaining; // Filter life remaining percentage
} RfPayloadFilterStatus;

}  // namespace zehnder
}  // namespace esphome

#endif /* __COMPONENT_ZEHNDER_H__ */
