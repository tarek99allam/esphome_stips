# Switch Cause Tracking & Zero-Heap Logs Delivery (`esphome_stips`)

This document details the core framework modifications supporting the `/logs` feature on branch `logs`.

---

## Architecture Overview

To enable complete auditing of switch state transitions and device restart history without crashing memory-constrained microcontrollers like the ESP8266 (which typically has only ~2–3 KB of contiguous free RAM), the solution is divided into:
1. **Core Cause Tracking**: Direct tagging in ESPHome core components whenever a switch state is requested.
2. **Zero-Heap HTML Dashboard**: Serving the rich web application directly from Flash **PROGMEM** using `beginResponse_P`, consuming **0 bytes of RAM heap**.
3. **Chunked Dynamic JSON API**: Fetching lightweight runtime logs (~500 bytes) asynchronously via `/logs?format=json`.

```
Browser / Client                     ESP8266 (web_server)                  mqttSub (Flash & Core)
      |                                        |                                      |
      |--- GET /logs ------------------------->|                                      |
      |<-- Stream HTML from Flash (0 heap) ----| (beginResponse_P from PROGMEM)       |
      |                                        |                                      |
      |--- GET /logs?format=json ------------->|                                      |
      |                                        |--- handle_logs_index_request(true) ->|
      |                                        |<-- JSON string (~500 bytes) ---------|
      |<-- sendChunked(JSON) ------------------|                                      |
```

---

## Changes by Component

### 1. Switch Component Core (`esphome/components/switch/switch.h`)
- Added `const char *last_cause` member variable initialized to `"local switch"`.
- Added cause accessors:
  ```cpp
  void set_cause(const char *cause) { this->last_cause = cause; }
  const char *get_cause() const { return this->last_cause; }
  ```
- Any component issuing a state change calls `set_cause(...)` before toggling the switch state.

### 2. Web Server (`esphome/components/web_server/`)
- **PROGMEM Template Header (`logs_index_html.h`)**:
  - Contains the entire responsive dark-mode HTML5, CSS, and vanilla JS application stored in `PROGMEM`.
  - Features real-time status indicator, last 3 restarts table with color badges, last 10 switch events with cause badges, per-switch tab filtering, and in-place auto-refresh (3s toggle).
- **Zero-Heap HTTP Delivery (`web_server.cpp`)**:
  ```cpp
  if (!wants_json) {
  #ifndef USE_ESP8266
    response = request->beginResponse(200, "text/html", LOGS_INDEX_HTML, sizeof(LOGS_INDEX_HTML) - 1);
  #else
    response = request->beginResponse_P(200, "text/html", reinterpret_cast<const uint8_t *>(LOGS_INDEX_HTML), sizeof(LOGS_INDEX_HTML) - 1);
  #endif
    request->send(response);
    return;
  }
  ```
- **Chunked JSON Delivery**: When `wants_json` is true, calls `mqtt_sub_->handle_logs_index_request(true)` and streams the payload via `request->sendChunked(...)`.
- **Cause Tagging**: Added `sw->set_cause("web server")` in `execute_switch_action()` when users interact with the web interface.
- **OTA Updates**: Enabled Web Server OTA via `POST /update`.

### 3. Home Assistant Native API (`esphome/components/api/api_connection.cpp`)
- Tagged `on_switch_command_request()` with cause `"home assistant"`:
  ```cpp
  sw->set_cause("home assistant");
  ```
- Differentiates commands triggered by Home Assistant dashboards, automations, or scripts from local physical touches or web clicks.

### 4. MQTT Switch (`esphome/components/mqtt/mqtt_switch.cpp`)
- Added `sw->set_cause("mqtt")` when a command is received via MQTT command topics.

### 5. Hardware Input Handling (`esphome/components/hardwareHandle/hardwareHandle.cpp`)
- Added `sw->set_cause("local switch")` when physical wall switch buttons or touch sensors trigger relay state changes.

---

## Verification
- Verified on ESP8266 hardware (NodeMCU `stips-wsa3-8ef3f2` and WSA3 `stips-wsa3-98eead`).
- Memory stress testing: 10 concurrent browser requests yielded 100% success (`HTTP 200`, ~20ms latency) with zero RAM allocation failures and zero device resets.
