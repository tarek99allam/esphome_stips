# Switch Cause Tracking and Logs HTTP Delivery (`esphome_stips`)

This document details the core framework modifications supporting the `/logs` feature on branch `logs`.

## Overview

To enable accurate identification of switch state changes across different control interfaces, cause tracking was implemented directly in core ESPHome components, alongside a reliable chunked HTTP handler in the web server.

---

## Changes by Component

### 1. Switch Component Core (`esphome/components/switch/switch.h`)
- Added `const char *last_cause` member variable initialized to `"local switch"`.
- Added methods:
  ```cpp
  void set_cause(const char *cause) { this->last_cause = cause; }
  const char *get_cause() const { return this->last_cause; }
  ```
- Any component issuing a state change calls `set_cause(...)` before toggling the switch state.

### 2. Web Server (`esphome/components/web_server/web_server.cpp`)
- **URL Routing**: Registered `/logs` in URL matcher and routing handler.
- **Chunked HTTP Streaming**: Implemented `handle_logs_index_request` using `request->sendChunked(...)` instead of buffering full responses in RAM. This prevents heap exhaustion on ESP8266 when serving HTML and JSON tables.
  ```cpp
  auto shared_resp = std::make_shared<std::string>(std::move(response));
  request->sendChunked(wants_json ? "application/json" : "text/html",
      [shared_resp](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        if (index >= shared_resp->length()) return 0;
        size_t to_copy = std::min(maxLen, shared_resp->length() - index);
        memcpy(buffer, shared_resp->data() + index, to_copy);
        return to_copy;
      });
  ```
- **Cause Tagging**: Added `sw->set_cause("web server")` in `execute_switch_action()` when users interact with the web UI.
- **Clean Restart**: Removed blocking `delay(3000)` inside `request->onDisconnect` in `handle_restart` to ensure `global_preferences->sync()` finishes cleanly and reports `"Software/System restart"` instead of a watchdog timeout.

### 3. Home Assistant Native API (`esphome/components/api/api_connection.cpp`)
- Tagged `on_switch_command_request()` with cause `"home assistant"`:
  ```cpp
  sw->set_cause("home assistant");
  ```
- Differentiates commands triggered by Home Assistant dashboards, automations, or scripts from local/web toggles.

### 4. MQTT Switch (`esphome/components/mqtt/mqtt_switch.cpp`)
- Added `sw->set_cause("mqtt")` when a command is received via MQTT topics.

### 5. Hardware Input Handling (`esphome/components/hardwareHandle/hardwareHandle.cpp`)
- Added `sw->set_cause("local switch")` when a physical input toggles the switch.
