# compile
g++ -o thermal_daemon thermal_daemon.cpp -lnvidia-ml -lpthread

# copy and adjust permissions

sudo cp ./thermal_daemon /usr/local/bin/thermal_daemon
sudo chown root:root /usr/local/bin/thermal_daemon
sudo chmod 755 /usr/local/bin/thermal_daemon

# enable and start

sudo cp omen-thermal.service /etc/systemd/system/omen-thermal.service
sudo systemctl daemon-reload
sudo systemctl enable omen-thermal.service
sudo systemctl start omen-thermal.service
