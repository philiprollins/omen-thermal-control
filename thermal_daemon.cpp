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

GtkWidget *ram_menu_item = nullptr;
GtkWidget *vram_menu_item = nullptr;
GtkWidget *cpu_menu_item = nullptr;
GtkWidget *gpu_menu_item = nullptr;

// Helper to generate a text-based progress bar for DBus menus
std::string make_progress_bar(double used, double total, int width = 12) {
    if (total <= 0) return "";
    int filled = (int)((used / total) * width);
    if (filled > width) filled = width;
    std::string bar = "[";
    for (int i = 0; i < width; ++i) {
        if (i < filled) bar += "█";
        else bar += "░";
    }
    bar += "]";
    return bar;
}

// Helper to calculate CPU usage
double get_cpu_usage() {
    static unsigned long long lastTotalUser = 0, lastTotalUserLow = 0, lastTotalSys = 0, lastTotalIdle = 0;
    std::ifstream file("/proc/stat");
    std::string line;
    std::getline(file, line);
    unsigned long long totalUser, totalUserLow, totalSys, totalIdle, totalReserved;
    sscanf(line.c_str(), "cpu %llu %llu %llu %llu %llu", &totalUser, &totalUserLow, &totalSys, &totalIdle, &totalReserved);

    if (totalUser < lastTotalUser || totalUserLow < lastTotalUserLow || 
        totalSys < lastTotalSys || totalIdle < lastTotalIdle) {
        // Overflow detection. Just skip this value.
        lastTotalUser = totalUser;
        lastTotalUserLow = totalUserLow;
        lastTotalSys = totalSys;
        lastTotalIdle = totalIdle;
        return 0.0;
    }

    unsigned long long total = (totalUser - lastTotalUser) + (totalUserLow - lastTotalUserLow) + (totalSys - lastTotalSys);
    double percent = 0.0;
    if (total > 0) {
        percent = (double)total / (double)(total + (totalIdle - lastTotalIdle)) * 100.0;
    }

    lastTotalUser = totalUser;
    lastTotalUserLow = totalUserLow;
    lastTotalSys = totalSys;
    lastTotalIdle = totalIdle;

    return percent;
}

// Helper to calculate RAM usage
double get_system_ram_usage_gb(double& total_gb) {
    std::ifstream file("/proc/meminfo");
    std::string line;
    long long mem_total = 0, mem_available = 0;
    while (std::getline(file, line)) {
        if (line.compare(0, 9, "MemTotal:") == 0) {
            sscanf(line.c_str(), "MemTotal: %lld kB", &mem_total);
        } else if (line.compare(0, 13, "MemAvailable:") == 0) {
            sscanf(line.c_str(), "MemAvailable: %lld kB", &mem_available);
        }
    }
    total_gb = mem_total / (1024.0 * 1024.0);
    return (mem_total - mem_available) / (1024.0 * 1024.0);
}

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

        // Update CPU limits
        double cpu_usage = get_cpu_usage();
        std::string cpu_bar = make_progress_bar(cpu_usage, 100.0, 12);
        char cpu_text[256];
        snprintf(cpu_text, sizeof(cpu_text), "CPU:  %s  %5.1f %%", cpu_bar.c_str(), cpu_usage);
        gtk_menu_item_set_label(GTK_MENU_ITEM(cpu_menu_item), cpu_text);

        // Update GPU limits
        nvmlUtilization_t gpu_util;
        if (nvmlDeviceGetUtilizationRates(device, &gpu_util) == NVML_SUCCESS) {
            std::string gpu_bar = make_progress_bar(gpu_util.gpu, 100.0, 12);
            char gpu_text[256];
            snprintf(gpu_text, sizeof(gpu_text), "GPU:  %s  %5.1d %%", gpu_bar.c_str(), gpu_util.gpu);
            gtk_menu_item_set_label(GTK_MENU_ITEM(gpu_menu_item), gpu_text);
        }

        // Update System RAM limits
        double ram_total_gb = 0;
        double ram_used_gb = get_system_ram_usage_gb(ram_total_gb);
        std::string ram_bar = make_progress_bar(ram_used_gb, ram_total_gb, 12);
        char ram_text[256];
        snprintf(ram_text, sizeof(ram_text), "RAM:  %s  %.1f / %.1f GB", ram_bar.c_str(), ram_used_gb, ram_total_gb);
        gtk_menu_item_set_label(GTK_MENU_ITEM(ram_menu_item), ram_text);

        // Update VRAM limits
        nvmlMemory_t vram_info;
        if (nvmlDeviceGetMemoryInfo(device, &vram_info) == NVML_SUCCESS) {
            double vram_used_gb = vram_info.used / (1024.0 * 1024.0 * 1024.0);
            double vram_total_gb = vram_info.total / (1024.0 * 1024.0 * 1024.0);
            std::string vram_bar = make_progress_bar(vram_used_gb, vram_total_gb, 12);
            char vram_text[256];
            snprintf(vram_text, sizeof(vram_text), "VRAM: %s  %.1f / %.1f GB", vram_bar.c_str(), vram_used_gb, vram_total_gb);
            gtk_menu_item_set_label(GTK_MENU_ITEM(vram_menu_item), vram_text);
        }

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
    
    // System Stats (CPU/GPU) Block Progress
    cpu_menu_item = gtk_menu_item_new_with_label("CPU:  [░░░░░░░░░░░░]   -- %");
    gtk_widget_set_sensitive(cpu_menu_item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), cpu_menu_item);

    gpu_menu_item = gtk_menu_item_new_with_label("GPU:  [░░░░░░░░░░░░]   -- %");
    gtk_widget_set_sensitive(gpu_menu_item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gpu_menu_item);

    // Separator line
    GtkWidget *separator1 = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator1);

    // System Stats (RAM/VRAM) Block Progress
    ram_menu_item = gtk_menu_item_new_with_label("RAM:  [░░░░░░░░░░░░]  -- / -- GB");
    gtk_widget_set_sensitive(ram_menu_item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), ram_menu_item);

    vram_menu_item = gtk_menu_item_new_with_label("VRAM: [░░░░░░░░░░░░]  -- / -- GB");
    gtk_widget_set_sensitive(vram_menu_item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), vram_menu_item);

    // Separator line
    GtkWidget *separator2 = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator2);

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