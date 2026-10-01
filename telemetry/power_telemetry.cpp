#include "unified_telemetry.h"

#include <windows.h>

#include <chrono>
#include <mutex>

static std::mutex g_power_mutex;

static bool GetBatteryStatus(
    double& battery_percent,
    bool& ac_online,
    bool& battery_charging,
    bool& battery_present)
{
    SYSTEM_POWER_STATUS status{};

    if (!GetSystemPowerStatus(&status))
        return false;

    battery_percent =
        status.BatteryLifePercent == 255
            ? 0.0
            : static_cast<double>(
                  status.BatteryLifePercent);

    ac_online =
        status.ACLineStatus == 1;

    battery_present =
        status.BatteryFlag !=
        BATTERY_FLAG_NO_BATTERY;

    battery_charging =
        battery_present &&
        ((status.BatteryFlag &
          BATTERY_FLAG_CHARGING) != 0);

    return true;
}

PowerTelemetry CollectPowerTelemetry()
{
    std::lock_guard<std::mutex> lock(
        g_power_mutex);

    PowerTelemetry telemetry{};

    double battery_percent = 0.0;
    bool ac_online = false;
    bool battery_charging = false;
    bool battery_present = false;

    bool battery_ok =
        GetBatteryStatus(
            battery_percent,
            ac_online,
            battery_charging,
            battery_present);

    telemetry.battery_percent =
        battery_percent;

    telemetry.ac_online =
        ac_online;

    telemetry.battery_charging =
        battery_charging;

    telemetry.battery_present =
        battery_present;

    telemetry.battery_status_available =
        battery_ok;

    telemetry.measured_power_watts =
        0.0;

    telemetry.system_power_watts =
        0.0;

    telemetry.measured_energy_pwh =
        0.0;

    telemetry.measurement_time_100ns =
        0;

    telemetry.energy_meter_available =
        false;

    telemetry.power_available =
        false;

    telemetry.system_power_available =
        false;

    telemetry.meter_is_system_scope =
        false;

    return telemetry;
}