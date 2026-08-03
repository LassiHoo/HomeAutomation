# Home Automation Hub

A small C++ service for a Raspberry Pi that exposes GPIO control over a REST
API, with structured logging that also lands in a queryable SQLite event log.

## Architecture

```
                       +-->  GpioController  --->  libgpiod v2
config/devices.json ---+-->  Bme280Sensor    --->  I2C (/dev/i2c-1)
     (DeviceManager)    \                                |
                         \--------------+  ApiServer  <--+
                                        |       |
                                        v       v
                          cpp-httplib REST API  (GET /status, POST /toggle/{pin},
                                                  GET /events, GET /sensors/bme280)

spdlog ---> [console sink] [rotating file sink] [SqliteLogSink] ---> data/events.db (events table)
```

- **`device_manager`** loads `config/devices.json` (a pin → name → component
  mapping, plus the BME280's I2C bus/address) and answers lookups for the
  other components.
- **`gpio_controller`** wraps libgpiod v2's C++ bindings (`gpiod::chip`,
  `gpiod::line_request`). It requests every configured pin as an output line
  at startup. If the GPIO chip can't be opened (e.g. you're not running on a
  Pi), it disables itself gracefully instead of crashing — `/status` and
  `/toggle` still respond, just reporting GPIO as unavailable.
- **`bme280_sensor`** talks to a BME280 temperature/humidity/pressure sensor
  over I2C using raw Linux `i2c-dev` ioctls (`I2C_RDWR`) — no `libi2c`/smbus
  dependency. The Bosch datasheet compensation math is a separate `static`
  function (`Bme280Sensor::compensate`) taking calibration coefficients and
  raw ADC values, kept independent of the hardware I/O so it's unit-testable
  without real hardware. Same graceful-degradation philosophy as
  `gpio_controller`: if the bus or sensor isn't present, it disables itself
  instead of crashing.
- **`api_server`** is a `cpp-httplib` server exposing four routes (see
  below). It reads the event log via its own read-only SQLite connection.
- **`sqlite_log_sink`** is a custom spdlog sink (`spdlog::sinks::base_sink`)
  that writes every log record into an `events` table via a prepared
  `INSERT`, with the database in WAL mode. Thread-safety comes from
  `base_sink`'s own mutex — the sink doesn't need to lock anything itself.
- **`main`** wires it all together: builds the three spdlog sinks (colored
  console, rotating file, SQLite), creates a logger per component (`gpio`,
  `api_server`, `device_manager`), and starts the HTTP server.

Everything except `main.cpp` builds into a `hub_core` static library, which
both the `hub` executable and the [tests](tests/) link against.

### Design decisions

- **libgpiod v2, not v1.** This project uses the modern character-device C++
  API (`gpiod::chip` / `gpiod::line_request`), not the older v1 API or
  WiringPi. The catch: Ubuntu 22.04's apt repo only packages libgpiod 1.6.3.
  The Docker build stage therefore builds libgpiod v2.2.4 from source and the
  runtime stage copies over just the resulting `libgpiod.so.3` /
  `libgpiodcxx.so.2`. See the comment at the top of the [Dockerfile](Dockerfile)
  for the full rationale.
- **spdlog/nlohmann-json/cpp-httplib are vendored via CMake `FetchContent`**
  (pinned versions) rather than relying on system packages, so the build is
  reproducible regardless of what the host or CI distro happens to ship.
  `libgpiod` and `SQLite3` are still resolved as system libraries since they
  talk to the kernel/filesystem directly.
- **The runtime container runs as root.** GPIO passthrough containers
  generally need this since `/dev/gpiochip0` on Raspberry Pi OS is owned by
  `root` (or `root:gpio`), and there's no reliable way to map the host's
  `gpio` group into the container. The same reasoning applies to `/dev/i2c-1`
  for the BME280.
- **BME280 over raw `i2c-dev` ioctls, not `libi2c-dev`.** Register reads use
  `ioctl(fd, I2C_RDWR, ...)` with two `i2c_msg`s (write register address, read
  N bytes) to get a proper repeated-start transaction, using only Linux
  kernel headers (`linux/i2c.h`, `linux/i2c-dev.h`) already present via
  `linux-libc-dev`. This avoids adding a system library dependency for what's
  otherwise a couple dozen lines of ioctl calls.

## REST API

| Route | Method | Description |
|---|---|---|
| `/status` | GET | Service + GPIO health, and the current state of every configured device |
| `/toggle/{pin}` | POST | Flips the output line for `pin`; 404 if unconfigured, 503 if GPIO unavailable |
| `/events?level=&component=&limit=` | GET | Queries the SQLite event log; all params optional (default limit 100, max 1000) |
| `/sensors/bme280` | GET | Current temperature (°C), humidity (%RH), and pressure (hPa); 503 if the sensor is unavailable |

Example:

```bash
curl localhost:8080/status
curl -X POST localhost:8080/toggle/17
curl "localhost:8080/events?component=gpio&level=info&limit=20"
curl localhost:8080/sensors/bme280
```

## Building locally

Requires a C++20 compiler, CMake ≥ 3.16, and the `libgpiod`/`SQLite3` system
headers. `spdlog`, `nlohmann-json`, and `cpp-httplib` are fetched
automatically by CMake.

```bash
sudo apt update && sudo apt install -y \
    build-essential cmake git libgpiod-dev libsqlite3-dev pkg-config

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

./build/hub config/devices.json
```

### Running the tests

Unit tests (GoogleTest, fetched automatically by CMake) cover `DeviceManager`
config loading, `GpioController`'s graceful degradation when no GPIO chip is
present, `SqliteLogSink`'s schema/WAL setup and inserts, and the `ApiServer`
routes end-to-end against a live instance on a test port:

```bash
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

The binary reads its config path from `argv[1]` (default
`config/devices.json`) and these environment variables:

| Variable | Default | Purpose |
|---|---|---|
| `HUB_HOST` | `0.0.0.0` | HTTP bind address |
| `HUB_PORT` | `8080` | HTTP port |
| `HUB_DATA_DIR` | `data` | Where `events.db` (SQLite) lives |
| `HUB_LOG_DIR` | `logs` | Where the rotating log file lives |

## Building the Docker image (cross-build for Raspberry Pi)

```bash
docker buildx create --use --name hub-builder   # once
docker buildx build --platform linux/arm64 -t home-automation-hub:local --load .
```

`--platform linux/arm64` builds the whole image under QEMU emulation, so no
cross-compiler setup is needed locally — it's slower than a native build but
produces a real arm64 image you can run with `docker run --platform
linux/arm64 ...` even on an x86 dev machine.

## Deploying

`docker-compose.yml` runs the image with `restart: unless-stopped`, exposes
port 8080, passes through `/dev/gpiochip0` and `/dev/i2c-1`, and persists
SQLite data + logs in named volumes. Replace the placeholder
`ghcr.io/OWNER/home-automation-hub` image reference with your actual GitHub
owner/repo (lowercase) before using it standalone.

If your BME280 is wired to a different I2C bus or address, edit the
`bme280` section of `config/devices.json` accordingly — enable the I2C
interface itself with `sudo raspi-config` (Interface Options → I2C) on the
Pi if you haven't already.

```bash
docker compose up -d
```

### One-time setup on the Raspberry Pi

The `deploy.yml` workflow (below) only *pulls and restarts* — it assumes
`docker-compose.yml` already exists on the Pi. Once, by hand:

```bash
mkdir -p ~/home-automation-hub && cd ~/home-automation-hub
# copy docker-compose.yml here, pointing at ghcr.io/<owner>/<repo>:latest
docker login ghcr.io -u <your-github-username>   # or make the package public
docker compose up -d
```

### CI/CD

- **`.github/workflows/build.yml`** — runs on every push/PR. Builds the
  linux/arm64 image via `docker/setup-qemu-action`,
  `docker/setup-buildx-action`, and `docker/build-push-action` with
  `push: false`, purely to verify the build succeeds.
- **`.github/workflows/deploy.yml`** — runs on push to `main`. Builds +
  pushes the linux/arm64 image to `ghcr.io/<owner>/<repo>`, then SSHes into
  the Pi and runs `docker compose pull && docker compose up -d`.

#### Required GitHub repo secrets

Add these under **Settings → Secrets and variables → Actions**:

| Secret | Value |
|---|---|
| `PI_HOST` | The Pi's hostname or IP address (must be reachable from GitHub's runners — e.g. a Tailscale/VPN address, or a port-forwarded public one) |
| `PI_USER` | The SSH username on the Pi (e.g. `pi`) |
| `PI_SSH_KEY` | The **private** half of a dedicated deploy keypair (see below) |

`GITHUB_TOKEN` (used to push to `ghcr.io`) is provided automatically by
Actions — no secret to add, just the `packages: write` permission already
set in `deploy.yml`.

#### Generating the deploy SSH key

Generate a dedicated keypair for CI (don't reuse your personal key):

```bash
ssh-keygen -t ed25519 -f deploy_key -C "github-actions-deploy" -N ""
```

- Copy **`deploy_key.pub`** into the Pi's `~/.ssh/authorized_keys`:
  ```bash
  ssh-copy-id -i deploy_key.pub <PI_USER>@<PI_HOST>
  ```
- Paste the contents of **`deploy_key`** (the private key, including the
  `-----BEGIN...-----`/`-----END...-----` lines) into the `PI_SSH_KEY` repo
  secret.
- Delete the local `deploy_key`/`deploy_key.pub` files once both are in
  place (the private key must not stay on disk anywhere outside the Pi's
  authorized key and GitHub's secret store).

## Querying the event log with the `sqlite3` CLI

```bash
sqlite3 data/events.db

sqlite> .headers on
sqlite> .mode column
sqlite> SELECT id, timestamp, level, component, message FROM events ORDER BY id DESC LIMIT 20;

-- filter by component and level, same as the /events API route:
sqlite> SELECT * FROM events WHERE component = 'gpio' AND level = 'info' ORDER BY id DESC LIMIT 20;
```

Since the database is in WAL mode, it's safe to run read queries with
`sqlite3` while the hub is running and writing to it concurrently.
