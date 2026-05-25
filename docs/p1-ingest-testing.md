# HomeWizard P1 Ingest Testing

This guide explains how the Raspberry Pi sender, `optiserver`, and `optimizer` fit together, and how to verify that meter readings are reaching the server.

## How It Works

The Raspberry Pi reads HomeWizard P1 meter data and sends it to:

```text
POST http://<server-ip>:10580/api/v1/ingest
```

On the VM, `optiserver` listens on TCP port `10580`. The ingest endpoint parses the JSON body, stores the latest meter reading in memory, and persists accepted readings to SQLite:

```text
/var/lib/maestro/meter_readings/meter_readings.db
```

The latest accepted reading can then be checked through:

```text
GET http://<server-ip>:10580/api/v1/power/current
GET http://<server-ip>:10580/api/v1/display/current
```

Restarting `optiserver` clears the in-memory latest reading, but accepted readings remain in the SQLite database.

`optimizer` is a separate process. It owns the Unix socket at:

```text
/run/maestro/optimizer.sock
```

When `optiserver` starts, it sends `reload` and `run` commands to the optimizer over that socket. If `optimizer` is not running first, `optiserver` may print `connect: Connection refused`.

## Start The Services

Start the optimizer first:

```bash
make optimizer/install
make optimizer/run
```

Leave that process running. In another terminal, start the server:

```bash
make server/run
```

The server should print something like:

```text
Server listening on port: 10580
```

For a daemon setup, install and start the optimizer daemon instead:

```bash
make optimizer/daemon-install
sudo systemctl start optimizer-daemon.service
make server/run
```

## Test Locally On The VM

Before any meter reading has been accepted, this may return `404`:

```bash
curl -i http://127.0.0.1:10580/api/v1/power/current
```

Expected response before data arrives:

```json
{"error":"no meter reading available"}
```

Send a minimal valid test reading:

```bash
curl -i -X POST http://127.0.0.1:10580/api/v1/ingest \
  -H 'Content-Type: application/json' \
  -d '{"active_power_w":1234}'
```

Expected response:

```json
{"status":"ok"}
```

Confirm that the server stored the latest reading:

```bash
curl -s http://127.0.0.1:10580/api/v1/power/current
```

Expected response includes the accepted power value:

```json
{
  "received_at": 1779100000,
  "present_flags": 32768,
  "power_w": 1234
}
```

The exact `received_at` and `present_flags` values may differ.

## Test From The Raspberry Pi

From the Raspberry Pi, send a test request to the VM server IP:

```bash
curl -i -X POST http://100.68.34.53:10580/api/v1/ingest \
  -H 'Content-Type: application/json' \
  -d '{"active_power_w":5678}'
```

Expected response:

```json
{"status":"ok"}
```

Then verify from either the VM or the Raspberry Pi:

```bash
curl -s http://100.68.34.53:10580/api/v1/power/current
```

Expected response includes:

```json
"power_w":5678
```

## Payload Requirements

The ingest endpoint requires a JSON request body. At minimum, the body must contain one of:

```json
{"power_w":1234}
```

or:

```json
{"active_power_w":1234}
```

If you post without a body:

```bash
curl -i -X POST http://127.0.0.1:10580/api/v1/ingest
```

the server returns `400 Bad Request`.

If the JSON body is present but does not contain `power_w` or `active_power_w`, the server returns:

```json
{"error":"invalid meter reading"}
```

## Useful Checks

Check whether the optimizer process is running:

```bash
pgrep -a optimizer
```

Check whether the optimizer runtime directory exists:

```bash
ls -l /run/maestro
```

Check the latest reading:

```bash
curl -s http://127.0.0.1:10580/api/v1/power/current
```

Check through the Tailscale or VM network IP:

```bash
curl -s http://100.68.34.53:10580/api/v1/power/current
```
