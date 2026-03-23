# OptiMaestro Optimizer

## Build Instructions

### Prerequisites

Make sure the following components are installed on your system:
- GNU Make, CMake, and GCC (recent versions)
- Linux (recommended and tested environment)
- Submodules initialized (`MaestroCore`, `cJSON`)

Initialize submodules:
```bash
git submodule update --init --recursive
```

### Building

Before building, make sure to install using

```bash
make install
```

Then compile the optimizer by running:
```bash
make
```

This will:
- Build dependencies (including MaestroCore)
- Compile source files from `src/` and `common/` directories
- Link into a final binary at `~/.local/bin/optimizer`

---

## Running the Optimizer

You can run the application directly after building:
```bash
make run
```

Or execute the binary manually:
```bash
~/.local/bin/optimizer
```

---

## Installation

To install system directories and configuration files required by the optimizer:
```bash
sudo make install
```

This step prepares:
- `/var/lib/maestro/` – runtime directory for application data  
- `/etc/maestro/optimizer.conf` – configuration file  
- `/var/log/maestro.log` – log file  

Ownership and permissions are set automatically to match the current user.

---

## Daemon Mode

To run OptiMaestro Optimizer as a background service:
```bash
sudo make install-daemon
```

This installs the daemon version of the optimizer and a corresponding systemd service file. Once installed:
```bash
sudo systemctl enable optimizer-daemon.service
sudo systemctl start optimizer-daemon.service
```

You can monitor its status:
```bash
systemctl status optimizer-daemon.service
```

---

## Cleaning and Debugging

Clean build artifacts:
```bash
make clean
```

Clean everything, including cache and installed binaries:
```bash
make full-clean
```

Debug builds can be run with:
```bash
make gdb
```

Run memory checks using:
```bash
make valgrind
```

---

## Configuration

The configuration file (`/etc/maestro/optimizer.conf`) defines system behavior and external API details.

Adjust these values to fine-tune the optimizer or set custom data directories.

---

## Example Workflow

```bash
git clone https://github.com/MaestroSoft-AB/OptiMaestroOptimizer.git
cd OptiMaestroOptimizer
git submodule update --init --recursive
sudo make install
make run
```

With daemon:
```bash
git clone https://github.com/MaestroSoft-AB/OptiMaestroOptimizer.git
cd OptiMaestroOptimizer
git submodule update --init --recursive
sudo make install
sudo make daemon-install
sudo systemctl start optimizer-daemon
```
