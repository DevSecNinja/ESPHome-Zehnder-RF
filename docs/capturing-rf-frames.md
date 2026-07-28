# Capturing Zehnder RF frames (sniffer mode)

This guide explains how to passively capture the raw RF traffic on your Zehnder /
ComfoFan network so it can be cross-checked against the reverse-engineered protocol.
It is useful when debugging why a command isn't honoured (e.g. a fixed speed being
overridden by a CO2 sensor) or when reverse-engineering unknown frames on your unit.

The protocol reference used throughout is
[eelcohn/ZehnderComfoair](https://github.com/eelcohn/ZehnderComfoair).

> **Sniffer mode is passive and read-only.** It parks the nRF905 radio in receive mode
> and logs every frame it hears. It does **not** pair, poll, or transmit anything, so it
> cannot change your ventilation. Use a spare ESP for continuous capture, or temporarily
> flip your existing device into sniffer mode.

---

## 1. Enable the component with sniffer mode

Point `external_components` at the branch that contains sniffer mode and the decoded
frame logging (until it is merged to `main`), and set `sniffer_mode: true` on the fan:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/DevSecNinja/ESPHome-Zehnder-RF
      ref: devsecninja/fix-co2-override-and-protocol-review
    components: [ nrf905, zehnder ]
    refresh: 0s   # always re-pull while iterating

fan:
  - platform: zehnder
    id: my_ventilation
    name: "Ventilation (sniffer)"
    nrf905: nrf905_rf
    sniffer_mode: true   # passive capture only – no control
```

Notes:

- If the device was **already paired**, the sniffer listens on your fan's real network,
  so you capture the actual CO2 sensor / timer remote / main-unit traffic.
- If it was **never paired**, the sniffer falls back to the linking network
  (`0xA55A5AA5`) so you can at least see the "main unit available for linking" broadcasts.

## 2. Configure the logger to show only the frames

The frame dump is emitted on a dedicated tag, **`zehnder.rf`**, at `DEBUG`. Keep the
global level at `DEBUG` (so that line is compiled in) and turn everything else down so the
log is clean:

```yaml
logger:
  level: DEBUG
  logs:
    zehnder.rf: DEBUG   # the frame sniffer – keep this
    zehnder: INFO       # mute the state-machine chatter
    nrf905: INFO
    spi: INFO
    scheduler: INFO
    component: INFO
    sensor: INFO
    api: INFO
    api.service: INFO
    wifi: WARN
    esp-idf: INFO
```

> ESPHome's per-tag `logs:` can only make a tag **less** verbose than the global `level`
> (anything more verbose is not compiled in). That is why the global level must be `DEBUG`.

## 3. Capture the log to a file

Over the network (recommended – heavy serial logging can slow the MCU):

```bash
esphome logs your-config.yaml 2>&1 | tee zehnder-frames.txt
```

To keep only the frame lines:

```bash
# Linux / macOS
esphome logs your-config.yaml 2>&1 | grep "zehnder.rf" | tee zehnder-frames.txt
```

```powershell
# Windows PowerShell
esphome logs your-config.yaml 2>&1 | Select-String "zehnder.rf" | Tee-Object zehnder-frames.txt
```

Each captured frame looks like:

```
[D][zehnder.rf]: RX rx=01:00 tx=18:2A ttl=FA cmd=02 n=01 | A5.5A.5A.A5.01.00.18.2A.FA.02.01.02.00.00.00.00 (16)
```

## 4. Annotate what you did

Raw frames are far more useful with context. While capturing, note **what you did and
when**, for example:

```
15:42 pressed "medium" on the wall remote
15:45 opened a window; CO2 climbed and the sensor bumped the speed
15:47 called setSpeed(2, 0) from Home Assistant
15:50 pressed the 30-minute timer button on the timer remote
```

Then share both the annotations and `zehnder-frames.txt` (see the RF capture issue
template).

---

## 5. How each frame is decoded

The nRF905 delivers a **16-byte payload** (the 4-byte network address and 2-byte CRC are
handled by the radio and are not part of these 16 bytes). The layout is:

| Offset | Field             | Notes |
|:------:|-------------------|-------|
| 0x00   | `rx_type`         | Receiver device type |
| 0x01   | `rx_id`           | Receiver device id (`0x00` = broadcast to all fans) |
| 0x02   | `tx_type`         | Transmitter device type |
| 0x03   | `tx_id`           | Transmitter device id |
| 0x04   | `ttl`             | Time-to-live (usually `0xFA` = 250) |
| 0x05   | `command`         | Frame/command type (see below) |
| 0x06   | `parameter_count` | Number of parameter bytes that follow |
| 0x07.. | `parameters[9]`   | Command-specific payload |

The decoded log line maps directly to this: `rx=<rx_type>:<rx_id>`,
`tx=<tx_type>:<tx_id>`, `cmd=<command>`, `n=<parameter_count>`, followed by the raw bytes.

### Device types

| Value | Type |
|:-----:|------|
| `0x00` | Broadcast |
| `0x01` | Main unit (the fan) |
| `0x03` | RFZ remote control |
| `0x16` | Timer RF remote control |
| `0x18` | CO2 RF sensor |

### Commands

| Value | Command |
|:-----:|---------|
| `0x01` | Set voltage (parameter: `0x00`–`0x64` = 0.0–10.0 V) |
| `0x02` | Set speed (parameter: `0x01` low, `0x02` medium, `0x03` high, `0x04` max) |
| `0x03` | Set timer (parameters: speed, duration in minutes) |
| `0x04` | Current network address |
| `0x05` | Reply to set speed / timer |
| `0x06` | Main unit available for linking |
| `0x07` | Current fan settings (`0x0B` speed, `0x0C` voltage, `0x0D` flags bit0=timer active, `0x0E` unit/fw) |
| `0x0B` | Linking successful |
| `0x0C` | RFZ available for linking |
| `0x0D` | Query device (broadcast) |
| `0x10` | Query device |
| `0x1D` | Reply to set voltage |

### Worked example

```
tx=18:2A cmd=02 n=01 params=02
```

A CO2 sensor (`tx_type=0x18`, id `0x2A`) sends command `0x02` (Set speed) with one
parameter `0x02` (medium). If you had just set a different speed manually, this frame is
the CO2 sensor overriding you.
