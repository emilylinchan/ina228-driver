"""
data_stream.py

UART data streamer for STM32 INA228 sensor measurements.
Sends a START command to the MCU over a serial connection, receives
timestamped voltage, current, and power samples as they are acquired,
and updates a live matplotlib plot while streaming.

Line format from the MCU: <ms_since_start>,<voltage>,<current>,<power>

"""

import serial
import time
import matplotlib.pyplot as plt

# ======================================================================
# Configuration
# ======================================================================

SERIAL_PORT = "COM9" 
BAUD_RATE   = 115200
TIMEOUT_S   = 2

REDRAW_INTERVAL_S = 0.05  # redraw plot at most every 50 ms (~20 FPS)

ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=TIMEOUT_S)
time.sleep(2)  # allow Nucleo reset

# ======================================================================
# User Input
# ======================================================================

total_time = int(input("Enter total time (seconds): "))

command = f"START,{total_time}\n"
print("Sending:", command.strip())

ser.write(command.encode("ascii"))

# ======================================================================
# Wait for MCU Acknowledgment
# ======================================================================

response = ser.readline().decode("ascii", errors="ignore").strip()
print("MCU response:", response)

if response != "OK":
    print("MCU did not acknowledge command.")
    ser.close()
    exit(1)

# ======================================================================
# Set Up Live Plot
# ======================================================================

plt.style.use("seaborn-v0_8")
plt.ion()  # interactive mode: plot window stays live while script runs

# Three stacked panels sharing the time axis, each with its own y-scale
fig, (ax_v, ax_i, ax_p) = plt.subplots(3, 1, figsize=(10, 7), sharex=True)

line_v, = ax_v.plot([], [], color="tab:blue",   label="Voltage (V)")
line_i, = ax_i.plot([], [], color="tab:orange", label="Current (A)")
line_p, = ax_p.plot([], [], color="tab:green",  label="Power (W)")

ax_v.set_ylabel("Voltage (V)")
ax_i.set_ylabel("Current (A)")
ax_p.set_ylabel("Power (W)")
ax_p.set_xlabel("Time (s)")

ax_v.set_title("Voltage, Current, and Power vs Time")

for a in (ax_v, ax_i, ax_p):
    a.set_xlim(0, total_time)
    a.grid(True)
    a.legend(loc="upper right")

fig.tight_layout()
fig.show()

times    = []   # seconds since start (from MCU timestamp)
voltages = []
currents = []
powers   = []

def update_plot():
    """Push the accumulated data into the plots and redraw."""
    if not times:
        return
    line_v.set_data(times, voltages)
    line_i.set_data(times, currents)
    line_p.set_data(times, powers)
    xmax = max(total_time, times[-1]) # extend x-axis to keep last few overrun samples
    for a in (ax_v, ax_i, ax_p):
        a.set_xlim(0, xmax)
        a.relim()
        a.autoscale_view(scalex=False, scaley=True)  # each panel scales independently
    fig.canvas.draw_idle()
    fig.canvas.flush_events()

# ======================================================================
# Receive Samples and Plot in Real Time
# ======================================================================

print("Receiving samples...")

# Header for console output
print("\n" + "="*60)
print(f" {'Time (s)':<12} {'Voltage (V)':<15} {'Current (A)':<15} {'Power (W)':<15}")
print("="*60)

aborted = False
last_redraw = time.monotonic()

while True:
    raw = ser.readline()
    line = raw.decode("ascii", errors="ignore").strip()

    if not line:
        # Timeout with no data — keep the GUI responsive and try again
        fig.canvas.flush_events()
        continue

    # Sensor comm failure reported by MCU
    if line == "FAULT_SENSOR_COMM":
        print("\n  MCU reported sensor communication failure.")
        aborted = True
        break

    if line == "DONE":
        print("Sampling complete.")
        break

    try:
        # Parse data
        ts_ms, v, i, p = line.split(",")
        t = float(ts_ms) / 1000.0   # ms -> s
        times.append(t)
        voltages.append(float(v))
        currents.append(float(i))
        powers.append(float(p))

        print(f"{t:<12.4f} {float(v):<15.6f} {float(i):<15.6f} {float(p):<15.6f}")

    except ValueError:
        continue # ignore malformed lines

    # Throttled redraw so plotting can't fall behind the serial stream
    now = time.monotonic()
    if now - last_redraw >= REDRAW_INTERVAL_S:
        update_plot()
        last_redraw = now

ser.close()

# ======================================================================
# Final Plot + Summary
# ======================================================================

num_samples = len(times)

if num_samples == 0:
    print("No data received.")
    exit(1)

update_plot() # one last redraw so the plot includes every sample

print(f"\nReceived {num_samples} samples over {times[-1]:.3f} s.")
if times[-1] > 0:
    print(f"Achieved average sampling rate: {num_samples / times[-1]:.1f} Hz")
if aborted:
    print("Collection ended early due to MCU fault.")
print("Close the plot window to exit.")

plt.ioff()
plt.show()  # blocks until the user closes the window