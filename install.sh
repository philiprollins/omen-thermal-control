#!/bin/bash

# Ensure running correctly
if [ "$EUID" -eq 0 ]; then
  echo "Please run as your normal user, do not run with sudo. It will prompt when necessary."
  exit 1
fi

echo "Building thermal_daemon with GUI support..."
g++ -o thermal_daemon thermal_daemon.cpp -lnvidia-ml -lpthread $(pkg-config gtk+-3.0 ayatana-appindicator3-0.1 --cflags --libs)
if [ $? -ne 0 ]; then
    echo "Compilation failed! Ensure libgtk-3-dev and libayatana-appindicator3-dev are installed."
    exit 1
fi

echo "Setting up permissions..."
sudo cp ./thermal_daemon /usr/local/bin/thermal_daemon
sudo chown root:root /usr/local/bin/thermal_daemon
sudo chmod 755 /usr/local/bin/thermal_daemon

echo "Configuring sudoers for passwordless fan control..."
echo "$USER ALL=(ALL) NOPASSWD: /usr/bin/tee /sys/devices/platform/hp-wmi/hwmon/hwmon*/pwm1_enable" | sudo tee /etc/sudoers.d/omen-fan > /dev/null
sudo chmod 440 /etc/sudoers.d/omen-fan

echo "Disabling old root service if it exists..."
sudo systemctl stop omen-thermal.service 2>/dev/null
sudo systemctl disable omen-thermal.service 2>/dev/null
sudo rm -f /etc/systemd/system/omen-thermal.service
sudo systemctl daemon-reload

echo "Setting up user systemd service..."
mkdir -p ~/.config/systemd/user
cp omen-tray.service ~/.config/systemd/user/omen-tray.service
systemctl --user daemon-reload
systemctl --user enable omen-tray.service
systemctl --user restart omen-tray.service

echo "Done! The tray icon should now appear."
