#!/bin/sh

# Thermal zone path
CPU_TEMP_PATH="/sys/class/thermal/thermal_zone11/temp"

# PWM-FAN hwmon path: prefer device named "pwmfan"
HWMON_DIR=""
for d in /sys/class/hwmon/hwmon*; do
    if [ -f "$d/name" ] && [ "$(cat "$d/name" 2>/dev/null)" = "pwmfan" ]; then
        HWMON_DIR="$d"
        break
    fi
done

# Fallback: if not found by name, keep the first hwmon as before
if [ -z "$HWMON_DIR" ]; then
    HWMON_DIR=$(find /sys/class/hwmon -maxdepth 1 -name "hwmon*" | head -n1)
fi

FAN_PWM_PATH="$HWMON_DIR/pwm1"
FAN_PWM_ENABLE_PATH="$HWMON_DIR/pwm1_enable"
FAN_RPM_PATH="$HWMON_DIR/fan1_input"

# Stop mode: make service ExecStop consistent with the PWM control path
if [ "$1" = "stop" ] || [ "$1" = "--stop" ]; then
    if [ ! -w "$FAN_PWM_PATH" ]; then
        echo "Error: Fan PWM file $FAN_PWM_PATH not writable (need root?)"
        exit 1
    fi

    # Stop/disable output if supported by driver (common: pwm1_enable=0)
    if [ -w "$FAN_PWM_ENABLE_PATH" ]; then
        echo 0 > "$FAN_PWM_ENABLE_PATH"
    fi

    # Also set PWM to 0 as a safe fallback
    echo 0 > "$FAN_PWM_PATH"
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] Fan stopped (pwm1_enable=0, PWM=0)"
    echo "[FAN_CTRL] Stopped: pwm1_enable=0, PWM=0" > /dev/kmsg
    exit 0
fi

# Temperature thresholds (in millidegrees Celsius)
LOW_TEMP=40000
MID_TEMP=60000
HIGH_TEMP=75000

# PWM value range: 0-255
PWM_OFF=0
PWM_LOW=85
PWM_MID=170
PWM_HIGH=255

# If the fan has a high startup threshold, kick it at full PWM briefly when starting
KICKSTART_PWM=255
KICKSTART_SEC=1

# Check if temperature file exists
if [ ! -f "$CPU_TEMP_PATH" ]; then
    echo "Error: CPU temperature file $CPU_TEMP_PATH not found!"
    exit 1
fi

# Check if PWM file exists and is writable
if [ ! -w "$FAN_PWM_PATH" ]; then
    echo "Error: Fan PWM file $FAN_PWM_PATH not writable (need root?)"
    exit 1
fi

# Read CPU temperature
CPU_TEMP_RAW=$(cat "$CPU_TEMP_PATH")
CPU_TEMP_C=$((CPU_TEMP_RAW / 1000))

# Determine target PWM based on temperature
if [ "$CPU_TEMP_RAW" -lt "$LOW_TEMP" ]; then
    TARGET_SPEED=$PWM_OFF
    TEMP_DESC="below ${LOW_TEMP%000}℃ (OFF)"
elif [ "$CPU_TEMP_RAW" -lt "$MID_TEMP" ]; then
    TARGET_SPEED=$PWM_LOW
    TEMP_DESC="between ${LOW_TEMP%000}℃ and ${MID_TEMP%000}℃ (LOW)"
elif [ "$CPU_TEMP_RAW" -lt "$HIGH_TEMP" ]; then
    TARGET_SPEED=$PWM_MID
    TEMP_DESC="between ${MID_TEMP%000}℃ and ${HIGH_TEMP%000}℃ (MID)"
else
    TARGET_SPEED=$PWM_HIGH
    TEMP_DESC="above ${HIGH_TEMP%000}℃ (HIGH)"
fi

# Read current fan speed
LAST_SPEED=$(cat "$FAN_PWM_PATH" 2>/dev/null || echo "")

# Set new speed if changed
if [ "$TARGET_SPEED" != "$LAST_SPEED" ]; then
    # Control enable mode if supported by driver:
    # - pwm1_enable=0 typically disables output (fan really stops)
    # - pwm1_enable=1 enables manual PWM control
    if [ -w "$FAN_PWM_ENABLE_PATH" ]; then
        if [ "$TARGET_SPEED" -eq 0 ]; then
            echo 0 > "$FAN_PWM_ENABLE_PATH"
        else
            echo 1 > "$FAN_PWM_ENABLE_PATH"
        fi
    fi

    # Kickstart: if starting from 0 -> >0, pulse full speed briefly
    if [ "$TARGET_SPEED" -gt 0 ] && [ "${LAST_SPEED:-0}" -eq 0 ] && [ "$KICKSTART_SEC" -gt 0 ]; then
        echo "$KICKSTART_PWM" > "$FAN_PWM_PATH"
        sleep "$KICKSTART_SEC"
    fi

    echo "$TARGET_SPEED" > "$FAN_PWM_PATH"
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] Current CPU temperature: ${CPU_TEMP_C}℃"
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] Set fan PWM to ${TARGET_SPEED} (${TEMP_DESC})"
    
    # Log to kernel message buffer
    echo "[FAN_CTRL] Temp=${CPU_TEMP_C}C, PWM=${TARGET_SPEED} (${TEMP_DESC})" > /dev/kmsg
fi

# Optional: Read and log fan RPM if available
if [ -f "$FAN_RPM_PATH" ]; then
    FAN_RPM=$(cat "$FAN_RPM_PATH" 2>/dev/null || echo "N/A")
    if [ "$FAN_RPM" != "N/A" ] && [ "$FAN_RPM" -gt 0 ]; then
        echo "[$(date +'%Y-%m-%d %H:%M:%S')] Fan RPM: ${FAN_RPM}"
    fi
fi

exit 0
