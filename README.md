# OptiMaestro

**OptiMaestro** is a utility for optimizing electricity consumption based on real-time price and weather data. It integrates information from **elprisjustnu** (electricity pricing API) and **open-meteo** (weather and solar data) to determine when during the day it's cheapest or most efficient to consume or produce electricity, especially for setups that include solar panels.

The project consists of two modules:
- **optimizer** — fetches, calculates, and stores energy/weather data
- **server** — HTTP server that exposes the optimizer's data via a REST API

---

## Prerequisites

- GCC (or Clang on macOS), GNU Make
- Linux (tested environment; macOS partially supported via Clang)
- Git submodules initialized

Initialize submodules:
```bash
git submodule update --init --recursive
```

---

## Building

Build everything (MaestroCore + all modules):
```bash
make all
```

Build individual modules:
```bash
make optimizer
make server
```

Optional: enable JSON support in MaestroCore:
```bash
make all JSON=1
```

Clean build artifacts:
```bash
make clean
```

---

## Running

### Server

Build and run the server directly:
```bash
make server/run
```

The server binary is installed to `~/.local/bin/optiserver` and listens on port **10580** by default (configurable via `server/config/system.conf`).

Debug targets:
```bash
make server/valgrind
make server/gdb
make server/run-asan
```

### Optimizer

Install system directories and config files (required on first run):
```bash
sudo make optimizer/install
```

This creates:
- `/var/lib/maestro/` — runtime data directory
- `/etc/maestro/optimizer.conf` — configuration file
- `/var/log/maestro.log` — log file

Run the optimizer:
```bash
make optimizer/run
```

Install and run as a systemd daemon:
```bash
sudo make optimizer/daemon-install
sudo systemctl enable optimizer-daemon.service
sudo systemctl start optimizer-daemon.service
```

---

## Configuration

### Server — `server/config/system.conf`

| Key | Default | Description |
|-----|---------|-------------|
| `http.port` | `10580` | HTTP listen port |
| `tcp.backlog` | `10000` | TCP connection backlog |

### Optimizer — `/etc/maestro/optimizer.conf`

Controls facility config directory, data/cache paths, and external API settings. Adjust `facility.conf.dir` and `data.calcs.dir` to override defaults.

Facility configs live in `/etc/maestro/facility/` by default (one `.conf` file per facility). Editable fields per facility:

| Key | Description |
|-----|-------------|
| `name` | Facility name |
| `currency` | Currency (e.g. `SEK`) |
| `energy_zone` | Price zone 1–4 |
| `latitude` | GPS latitude |
| `longitude` | GPS longitude |
| `panel.tilt` | Solar panel tilt (degrees) |
| `panel.azimuth` | Solar panel azimuth (degrees) |
| `panel.m2_size` | Solar panel area (m²) |

---

## API Reference

Base URL: `http://<host>:10580`  
API root: `/api/v1`

All responses use `Connection: close`.

---

### Echo

**`GET /echo`**

Returns a JSON echo of the incoming request. Useful for debugging.

```json
{
  "method": "GET",
  "path": "/echo",
  "query": "",
  "headers": [
    { "key": "Host", "value": "localhost:10580" }
  ],
  "body": ""
}
```

---

### Facilities

**`GET /api/v1/facilities`**

Returns a newline-separated plain-text list of all configured facility names.

```
Content-Type: text/plain

MyHouse
CabinNorth
```

Errors:
- `503` `{"error":"failed to list facilities"}`

---

**`GET /api/v1/config`**

Returns the raw `.conf` file for a facility.

Query params:
| Param | Required | Description |
|-------|----------|-------------|
| `name` | yes | Facility name |

```
Content-Type: text/plain

name=MyHouse
currency=SEK
energy_zone=3
latitude=59.33
longitude=18.06
panel.tilt=30
panel.azimuth=180
panel.m2_size=20
```

Errors:
- `400` `{"error":"Missing parameter for 'name'"}`
- `404` `{"error":"facility config not found"}`
- `503` `{"error":"facility config not available"}`

---

**`POST /api/v1/config`**

Updates a facility's config and triggers a reload + recalculation in the optimizer.

Query params:
| Param | Required | Description |
|-------|----------|-------------|
| `name` | yes | Facility name (creates a new config if it doesn't exist) |

Body (`text/plain`, `key=value` lines, only editable keys accepted):
```
latitude=59.33
longitude=18.06
panel.tilt=35
```

Response `200`:
```json
{"status":"facility config saved"}
```

Errors:
- `400` `{"error":"Missing parameter for 'name'"}` / `{"error":"config body missing"}` / `{"error":"invalid config key in update"}`
- `503` `{"error":"..."}` — various write/signal failures

---

### Energy data

**`GET /api/v1/weather/cache`**

Returns cached weather data from the local SQLite database in a compact text format.

Query params:
| Param | Required | Description |
|-------|----------|-------------|
| `name` | no | Facility name (reads lat/lon/panel config from facility) |
| `latitude` / `lat` | if no `name` | GPS latitude |
| `longitude` / `lon` | if no `name` | GPS longitude |
| `panel_tilt` / `panel.tilt` | no | Panel tilt override |
| `panel_azimuth` / `panel.azimuth` | no | Panel azimuth override |
| `range` | no | `24h` (default), `7d`, `30d` |
| `from` | no | Unix epoch start (overrides `range`) |
| `to` | no | Unix epoch end (overrides `range`) |
| `forecast` | no | `0` to exclude forecast data (default: include) |

Response `200` (`text/plain`):
```
M,<count>,<forecast 0|1>,<interval_minutes>
V,<unix_timestamp>,<temperature>,<precipitation>,<windspeed>,<radiation_shortwave>
V,...
```

Errors:
- `400` `{"error":"invalid time range"}` / `{"error":"invalid latitude/longitude"}` / `{"error":"invalid panel_tilt"}` / `{"error":"invalid panel_azimuth"}`
- `404` `{"error":"facility not found"}`

---

**`GET /api/v1/spot-price/cache`**

Returns cached electricity spot prices from the local SQLite database.

Query params:
| Param | Required | Description |
|-------|----------|-------------|
| `energy_zone` / `price_class` | no | Price zone 1–4 (default: 3, or from facility config) |
| `name` | no | Facility name (reads zone from facility config) |
| `range` | no | `24h` (default), `7d`, `30d` |
| `from` | no | Unix epoch start |
| `to` | no | Unix epoch end |

Response `200` (`application/json`):
```json
{
  "energy_zone": 3,
  "unit": "SEK/kWh",
  "interval_minutes": 60,
  "count": 24,
  "prices": [
    { "time_start": 1718834400, "time_end": 1718838000, "spot_price": 0.42 },
    { "time_start": 1718838000, "time_end": 1718841600, "spot_price": 0.38 }
  ]
}
```

Errors:
- `400` `{"error":"invalid time range"}` / `{"error":"invalid energy_zone"}`
- `404` `{"error":"facility not found"}`

---

**`GET /api/v1/average-daily`**

Returns a pre-computed daily average consumption file from the calcs directory.

Query params:
| Param | Required | Description |
|-------|----------|-------------|
| `name` | yes | Facility name |
| `type` | no | File type (default: `json`) |
| `epd` / `sp` | no | Entries per day (default: `96`) |

Response `200` (`application/json`): file contents from calcs directory.

Errors:
- `503` `{"error":"Missing parameter for 'name'"}` / `{"error":"File not found (...)"}`
- `500` `{"error":"Failed to format filename"}`

---

**`GET /api/v1/average-hourly`**

Returns today's pre-computed hourly average file (`YYYYMMDD-SP24-SE3.json`) from `/var/lib/maestro/calcs/`.

Response `200` (`application/json`): file contents.

Errors:
- `503` `{"error":"average.json not available"}`

---

### Meter readings (P1/DSMR)

**`POST /api/v1/ingest`**

Ingest a meter reading from a smart meter (P1/DSMR or compatible). Stores the reading and keeps the latest in memory for `/power/current`.

Body (`application/json`). At minimum `power_w` (or `active_power_w`) must be present. All other fields are optional:

```json
{
  "unique_id": "DEVICE-001",
  "meter_model": "XMX5XMXABCE000021673",
  "timestamp": "2024-06-19T10:00:00",
  "protocol_version": 50,
  "tariff": 1,
  "energy_import_kwh": 1234.567,
  "energy_import_t1_kwh": 600.0,
  "energy_import_t2_kwh": 634.567,
  "energy_export_kwh": 45.0,
  "power_w": 1200.0,
  "power_l1_w": 400.0,
  "power_l2_w": 400.0,
  "power_l3_w": 400.0,
  "voltage_l1_v": 230.1,
  "voltage_l2_v": 229.8,
  "voltage_l3_v": 230.5,
  "current_l1_a": 1.7,
  "current_l2_a": 1.7,
  "current_l3_a": 1.7,
  "frequency_hz": 50.0
}
```

Alternative field names accepted (e.g. `active_power_w`, `total_power_import_kwh`, `active_voltage_l1_v`, etc.).

Response `200`:
```json
{"status":"ok"}
```

Errors:
- `400` `{"error":"meter reading body missing"}` / `{"error":"invalid meter reading"}`

---

**`GET /api/v1/power/current`**

Returns the latest ingested meter reading.

Response `200` (`application/json`):
```json
{
  "received_at": 1718834400,
  "present_flags": 12345,
  "unique_id": "DEVICE-001",
  "meter_model": "XMX5XMXABCE000021673",
  "timestamp": "2024-06-19T10:00:00",
  "power_w": 1200.0,
  "power_l1_w": 400.0,
  "power_l2_w": 400.0,
  "power_l3_w": 400.0,
  "voltage_l1_v": 230.1,
  "energy_import_kwh": 1234.567
}
```

Only fields present in the original reading are included. `present_flags` is a bitmask indicating which fields are set.

Errors:
- `404` `{"error":"no meter reading available"}`

---

**`GET /api/v1/display/current`**

Identical to `/api/v1/power/current`.

---

**`GET /api/v1/display/graph/hour`**

Returns a pre-computed display graph JSON file for the current day from the calcs directory.

Query params:
| Param | Required | Description |
|-------|----------|-------------|
| `name` | no | Facility name (defaults to first available facility) |
| `range` | no | `7d` or `30d` (default: today's graph) |

Response `200` (`application/json`): file contents from `<calcs_dir>/<facility>-Consumption_<YYYY-MM-DD>-display[-7d|-30d].json`.

Errors:
- `404` `{"error":"no facility available"}` / `{"error":"facility not found"}`
- `503` `{"error":"display graph not available (...)"}`
- `500` `{"error":"failed to resolve calcs dir"}` / `{"error":"failed to format display graph filename"}`

---

### Optimizer control

**`GET /api/v1/recalc`**

Triggers a config reload and full recalculation in the optimizer process via Unix domain socket.

Response `200`:
```
Reloaded config and ran calculations
```

Errors:
- `500` `"Failed to reload config"` / `"Failed to run calculations"`

---

**`GET /api/v1/kill`**

Sends a KILL signal to the optimizer process via Unix domain socket.

Response `200`:
```
Committed first degree murder on optimizer
```

Errors:
- `500` `"Failed to reload config"`

---

### Stub endpoints (not yet implemented)

| Endpoint | Response |
|----------|----------|
| `GET /api/v1/solar-cell` | `{"Solar":"No sun until April"}` |
| `GET /api/v1/temp-sensor-1` | `{"Temp-Sensor":"It's a me, Mario"}` |
| `GET /api/v1/jacuzzi` | `{"Jacuzzi":"Out of water"}` |
| `GET /api/v1/overview` | `{"overview":Yahoooooooo}` *(invalid JSON — placeholder)* |

---

### Error responses

Unknown paths return:
```json
HTTP 404
{"error":"not found"}
```

Internal errors return:
```
HTTP 500
Internal Server Error
```
