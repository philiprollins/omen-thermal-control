# OMEN Thermal Control

A lightweight C++ daemon for managing thermal states on HP OMEN laptops running Ubuntu. This tool uses the NVIDIA Management Library (NVML) to monitor GPU temperatures and adjust system behavior accordingly.

## Prerequisites

Before installing, ensure your system has the necessary NVIDIA drivers and build tools:

```bash
sudo apt update
sudo apt install build-essential libnvidia-ml-dev
````

## Installation

We have included an installation script to handle compilation, permissions, and systemd service setup.

Clone the repository:

```bash
git clone git@github.com:philiprollins/omen-thermal-control.git
cd omen-thermal-control
```

Run the installer:

```bash
chmod +x install.sh
./install.sh
```

## Service Management

The installer sets up a systemd service called `omen-thermal.service`. You can manage it using standard commands:

```bash
# Check status
systemctl status omen-thermal.service

# Restart service
sudo systemctl restart omen-thermal.service

# Stop service
sudo systemctl stop omen-thermal.service

# View logs
journalctl -u omen-thermal.service -f
```

## Project Structure

* `thermal_daemon.cpp`: Core daemon source code.
* `omen-thermal.service`: Systemd unit configuration.
* `install.sh`: Automated build and installation script.