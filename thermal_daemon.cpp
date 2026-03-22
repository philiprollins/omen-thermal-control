#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>
#include <nvml.h>

// --- CONFIGURATION ---
const int GPU_THRESHOLD = 65;
const int CPU_THRESHOLD = 65;
const int CHECK_INTERVAL_MS = 1000;
const int COOL_DOWN_DELAY_SEC = 20;

// Helper to run your shell command
void set_fan_mode(int mode) {
    std::string cmd = "echo " + std::to_string(mode) + " | sudo tee /sys/devices/platform/hp-wmi/hwmon/hwmon*/pwm1_enable > /dev/null";
    int result = std::system(cmd.c_str());
    if (result == 0) {
        std::cout << "\n[" << mode << "] --- Fans set to: " << (mode == 0 ? "MAX" : "AUTO") << " ---" << std::endl;
    }
}

// Read CPU temp from sysfs (millidegrees to degrees)
int get_cpu_temp() {
    std::ifstream file("/sys/class/thermal/thermal_zone0/temp");
    if (!file.is_open()) return 0;
    int temp;
    file >> temp;
    return temp / 1000;
}

int main() {
    // Initialize NVIDIA Management Library
    nvmlReturn_t result = nvmlInit();
    if (result != NVML_SUCCESS) {
        std::cerr << "Failed to initialize NVML: " << nvmlErrorString(result) << std::endl;
        return 1;
    }

    nvmlDevice_t device;
    nvmlDeviceGetHandleByIndex(0, &device);

    bool is_max = false;
    auto last_high_temp_time = std::chrono::steady_clock::now();

    std::cout << "Omen 17 C++ Daemon Active. Monitoring..." << std::endl;

    try {
        while (true) {
            unsigned int gpu_temp = 0;
            nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &gpu_temp);
            int cpu_temp = get_cpu_temp();

            std::cout << "\rCPU: " << cpu_temp << "°C | GPU: " << gpu_temp << "°C | Active: " << (is_max ? "YES" : "NO") << std::flush;

            if (gpu_temp >= GPU_THRESHOLD || cpu_temp >= CPU_THRESHOLD) {
                last_high_temp_time = std::chrono::steady_clock::now();
                if (!is_max) {
                    set_fan_mode(0);
                    is_max = true;
                }
            } else if (is_max) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_high_temp_time).count();

                if (gpu_temp < (GPU_THRESHOLD - 5) && cpu_temp < (CPU_THRESHOLD - 5) && elapsed > COOL_DOWN_DELAY_SEC) {
                    set_fan_mode(2);
                    is_max = false;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(CHECK_INTERVAL_MS));
        }
    } catch (...) {
        set_fan_mode(2); // Failsafe
    }

    nvmlShutdown();
    return 0;
}
