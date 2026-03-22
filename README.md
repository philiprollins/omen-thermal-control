# HP Omen 17 Fan Controller

A lightweight C++ GTK3 system tray application for monitoring temperatures and managing fan speeds on HP OMEN laptops running Linux. This tool uses the NVIDIA Management Library (NVML) to read GPU temperatures, sysfs for CPU temperatures, and provides a convenient GUI in your system tray to manually override fan modes.

## Features

* **System Tray GUI**: Displays real-time CPU and GPU temperatures directly in your system tray.
* **Automatic Thermal Management**: Automatically ramps up device fans when crossing 65°C, and restores normal operation after temps cool down.
* **Manual Override**: A toggle in the tray menu allows you to force "MAX" fan mode on demand.
* **Seamless User Service**: Runs as a local Wayland/X11 user service on boot, avoiding the need for brittle root GUI applications.

## Prerequisites

Before installing, ensure your system has the necessary build tools, NVIDIA development headers, and GTK3/AppIndicator libraries:

```bash
sudo apt update
sudo apt install build-essential libnvidia-ml-dev libgtk-3-dev libayatana-appindicator3-dev
```

## Installation

An installation script is provided to handle compilation, sudoers permissions (for passwordless fan control), and the systemd user service setup.

Clone the repository:

```bash
git clone git@github.com:philiprollins/omen-thermal-control.git
cd omen-thermal-control
```

Run the installer **as your regular user** (do not use `sudo` to run the script itself, it will request your password when needed internally to set permissions):

```bash
chmod +x install.sh
./install.sh
```

## Service Management

The installer sets up a user-level systemd service called `omen-tray.service`. This means the service activates when you log into your desktop environment.

You can manage it using standard systemctl commands with the `--user` flag:

```bash
# Check status
systemctl --user status omen-tray.service

# Restart service
systemctl --user restart omen-tray.service

# Stop service
systemctl --user stop omen-tray.service

# View logs
journalctl --user -u omen-tray.service -f
```

## Project Structure

* `thermal_daemon.cpp`: Core application, GTK and AppIndicator daemon source code.
* `omen-tray.service`: Systemd user unit configuration.
* `install.sh`: Automated compile, permission configuration, and installation script.
