#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>
#include <nvml.h>
#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>

// --- CONFIGURATION ---
const int GPU_THRESHOLD = 65;
const int CPU_THRESHOLD = 65;
const int CHECK_INTERVAL_MS = 1000;
const int COOL_DOWN_DELAY_SEC = 20;

// Globals
AppIndicator *indicator = nullptr;
bool is_max = false;
bool override_max = false;   // True if user clicked MAX mode
auto last_high_temp_time = std::chrono::steady_clock::now();
nvmlDevice_t device;

// Helper to run your shell command
void set_fan_mode(int mode) {
    std::string cmd = "echo " + std::to_string(mode) + " | sudo tee /sys/devices/platform/hp-wmi/hwmon/hwmon*/pwm1_enable > /dev/null";
    int result = std::system(cmd.c_str());
    if (result == 0) {
        g_print("\n[%d] --- Fans set to: %s ---\n", mode, (mode == 0 ? "MAX" : "AUTO"));
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

// Callback for the menu toggle
void on_fan_mode_toggled(GtkCheckMenuItem *menuitem, gpointer user_data) {
    override_max = gtk_check_menu_item_get_active(menuitem);
    
    if (override_max) {
        set_fan_mode(0); // MAX
        is_max = true;
    } else {
        // Will be picked up by the next tick of update_logic to revert to AUTO if temps are low
        set_fan_mode(2); // AUTO
        is_max = false;
        last_high_temp_time = std::chrono::steady_clock::now() - std::chrono::hours(1); // Force immediate cooldown logic check
    }
}

// Main logic tick
gboolean update_logic(gpointer data) {
    try {
        unsigned int gpu_temp = 0;
        nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &gpu_temp);
        int cpu_temp = get_cpu_temp();

        // Update Tray Label
        char label_text[128];
        snprintf(label_text, sizeof(label_text), "CPU: %d°C | GPU: %d°C", cpu_temp, gpu_temp);
        app_indicator_set_label(indicator, label_text, "CPU: 000°C | GPU: 000°C");

        // Thermal Logic (only if not running under forced override)
        if (!override_max) {
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
        }
    } catch (...) {
        if (!override_max) set_fan_mode(2); // Failsafe
    }
    
    return G_SOURCE_CONTINUE;
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    // Force dark theme
    GtkSettings *settings = gtk_settings_get_default();
    g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, NULL);

    // Initialize NVML
    nvmlReturn_t result = nvmlInit();
    if (result != NVML_SUCCESS) {
        g_printerr("Failed to initialize NVML: %s\n", nvmlErrorString(result));
        return 1;
    }
    nvmlDeviceGetHandleByIndex(0, &device);

    // Create AppIndicator
    indicator = app_indicator_new("omen-thermal-daemon",
                                  "utilities-system-monitor", // standard icon or fallback
                                  APP_INDICATOR_CATEGORY_HARDWARE);
    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_label(indicator, "CPU: --°C | GPU: --°C", "CPU: 000°C | GPU: 000°C");
    app_indicator_set_title(indicator, "HP Omen 17 Fan Control");

    // Create Menu
    GtkWidget *menu = gtk_menu_new();
    
    // Toggle Item for MAX / AUTO
    GtkWidget *toggle_item = gtk_check_menu_item_new_with_label("Force MAX Fan Mode");
    g_signal_connect(toggle_item, "toggled", G_CALLBACK(on_fan_mode_toggled), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), toggle_item);

    // Quit Option
    GtkWidget *quit_item = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(quit_item, "activate", G_CALLBACK(gtk_main_quit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);

    gtk_widget_show_all(menu);
    app_indicator_set_menu(indicator, GTK_MENU(menu));

    // Register 1-second timeout
    g_timeout_add(CHECK_INTERVAL_MS, update_logic, NULL);

    g_print("Omen 17 UI Daemon Active. Monitoring...\n");
    gtk_main();

    nvmlShutdown();
    return 0;
}