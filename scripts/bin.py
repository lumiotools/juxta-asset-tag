import subprocess
import sys
from pathlib import Path
import serial
import serial.tools.list_ports
import time

# ================= USER CONFIG =================

# Use script's location to build absolute path to build directory
SCRIPT_DIR = Path(__file__).parent
BUILD_ROOT = (SCRIPT_DIR / "../test/power_latch_sensor_led_test/build").resolve()   # Resolves to absolute path

# Auto-detect the esp32 build subdirectory (e.g., esp32.esp32.esp32c6)
def find_build_dir():
    if not BUILD_ROOT.exists():
        return None
    
    # Look for esp32.* subdirectories
    esp32_dirs = list(BUILD_ROOT.glob("esp32.*"))
    if esp32_dirs:
        return esp32_dirs[0]  # Use the first match
    
    # Fall back to BUILD_ROOT if no subdirectory found
    return BUILD_ROOT

BUILD_DIR = find_build_dir()

CHIP = "esp32c6"
BAUD = "921600"

FLASH_MODE = "dio"
FLASH_FREQ = "80m"
FLASH_SIZE = "4MB"

# Serial Monitor Settings
SERIAL_MONITOR_BAUD = "115200"  # Default baud rate for serial monitor

# ===============================================


def find_ports():
    return list(serial.tools.list_ports.comports())


def select_port(ports):
    print("\nAvailable COM ports:")
    for i, p in enumerate(ports):
        print(f"[{i}] {p.device} - {p.description}")

    if len(ports) == 1:
        choice = input(f"\n✅ Auto-selected: {ports[0].device} - Use this port? (Y/n/manual): ").lower()
        if choice == "" or choice == "y":
            return ports[0].device
        elif choice == "manual" or choice == "m":
            manual_port = input("Enter COM port (e.g., COM3): ").strip()
            return manual_port

    print(f"[m] Manual entry")
    choice = input("\nSelect port index or 'm' for manual: ").strip().lower()
    
    if choice == "m" or choice == "manual":
        manual_port = input("Enter COM port (e.g., COM3): ").strip()
        return manual_port
    
    return ports[int(choice)].device



def print_troubleshooting():
    """Print troubleshooting steps for common serial port errors"""
    print("\n" + "="*60)
    print("⚠️  TROUBLESHOOTING STEPS:")
    print("="*60)
    print("1. Close Arduino IDE Serial Monitor or any other serial terminals")
    print("2. Unplug and replug the USB cable")
    print("3. Put the ESP32-C6 into bootloader mode:")
    print("   - Hold the BOOT button (GPIO9)")
    print("   - Press and release the RESET button")
    print("   - Release the BOOT button")
    print("   - Try the flash command again within 5 seconds")
    print("4. Check if the correct COM port is selected")
    print("5. Try a different USB cable or USB port")
    print("6. Install/update CH340 or CP210x USB drivers")
    print("7. Try a lower baud rate (edit BAUD in script to 115200)")
    print("="*60 + "\n")


def serial_monitor(port, baud=None):
    """Open serial monitor to view ESP32-C6 output"""
    if baud is None:
        baud = SERIAL_MONITOR_BAUD
    
    print("\n" + "="*60)
    print(f"📡 Serial Monitor - {port} @ {baud} baud")
    print("="*60)
    print("Press Ctrl+C to exit")
    print("="*60 + "\n")
    
    try:
        # Wait a moment for the device to reset after flashing
        time.sleep(2)
        
        # Open serial port
        ser = serial.Serial(
            port=port,
            baudrate=int(baud),
            timeout=1,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE
        )
        
        print(f"✅ Connected to {port}")
        print("Waiting for data...\n")
        
        # Buffer for incomplete lines
        buffer = ""
        
        while True:
            if ser.in_waiting > 0:
                try:
                    # Read available data
                    data = ser.read(ser.in_waiting)
                    # Decode with error handling
                    text = data.decode('utf-8', errors='replace')
                    buffer += text
                    
                    # Print complete lines
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        print(line)
                        sys.stdout.flush()
                    
                except UnicodeDecodeError:
                    # Print raw bytes if decode fails
                    print(f"[RAW] {data.hex()}")
            else:
                # Small delay to prevent CPU spinning
                time.sleep(0.01)
                
    except serial.SerialException as e:
        print(f"\n❌ Serial port error: {e}")
        print("The device may have been disconnected or the port is in use.")
    except KeyboardInterrupt:
        print("\n\n📴 Serial monitor closed")
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()
            print(f"Disconnected from {port}")


def run_esptool(args, retry_on_fail=True):
    """Run esptool with error handling and retry option"""
    cmd = [sys.executable, "-m", "esptool"] + args
    print("\n🚀 Running:")
    print(" ".join(cmd))
    
    try:
        result = subprocess.run(cmd, check=True, capture_output=True, text=True)
        print(result.stdout)
        return True
    except subprocess.CalledProcessError as e:
        print("\n❌ Error occurred:")
        if e.stderr:
            print(e.stderr)
        if e.stdout:
            print(e.stdout)
        
        # Check for common errors
        error_msg = str(e.stderr) + str(e.stdout)
        
        if "PermissionError" in error_msg or "ClearCommError" in error_msg:
            print("\n⚠️  Serial port access error!")
            print_troubleshooting()
            
            if retry_on_fail:
                retry = input("Put device in bootloader mode and retry? (y/N): ").lower()
                if retry == "y":
                    print("\n⏳ Waiting 2 seconds for bootloader mode...")
                    time.sleep(2)
                    return run_esptool(args, retry_on_fail=False)  # Only retry once
        
        elif "No serial data received" in error_msg or "Failed to connect" in error_msg:
            print("\n⚠️  Cannot connect to ESP32-C6!")
            print_troubleshooting()
        
        return False
    except Exception as e:
        print(f"\n❌ Unexpected error: {e}")
        return False



def flash_merged(port):
    merged = next(BUILD_DIR.glob("*.merged.bin"), None)
    if not merged:
        return False

    print(f"\n✅ Found merged image: {merged.name}")

    success = run_esptool([
        "--chip", CHIP,
        "--port", port,
        "--baud", BAUD,
        "write-flash",
        "--flash-mode", FLASH_MODE,
        "--flash-freq", FLASH_FREQ,
        "--flash-size", FLASH_SIZE,
        "0x0000", str(merged),
    ])
    return success


def flash_split(port):
    files = {
        "0x0000": "*.bootloader.bin",
        "0x8000": "*.partitions.bin",
        "0xe000": "boot_app0.bin",
        "0x10000": "*.ino.bin",
    }

    flash_args = []

    for addr, pattern in files.items():
        matches = list(BUILD_DIR.glob(pattern))
        if not matches:
            print(f"❌ Missing file: {pattern}")
            return False
        flash_args.extend([addr, str(matches[0])])

    success = run_esptool([
        "--chip", CHIP,
        "--port", port,
        "--baud", BAUD,
        "write_flash",
        "--flash_mode", FLASH_MODE,
        "--flash_freq", FLASH_FREQ,
        "--flash_size", FLASH_SIZE,
        *flash_args,
    ])
    return success


def main():
    print("\nESP32-C6 Arduino Flash Tool")
    print("----------------------------")

    if BUILD_DIR is None or not BUILD_DIR.exists():
        print(f"❌ Build directory not found: {BUILD_ROOT}")
        print(f"   Looking for: esp32.* subdirectories")
        sys.exit(1)
    
    print(f"📁 Using build directory: {BUILD_DIR}")

    ports = find_ports()
    if not ports:
        print("⚠️  No COM ports detected automatically")
        manual = input("Enter COM port manually? (y/N): ").lower()
        if manual == "y":
            port = input("Enter COM port (e.g., COM3): ").strip()
        else:
            print("❌ Aborted - no ports available")
            sys.exit(1)
    else:
        port = select_port(ports)

    # Try merged binary first, then split binaries
    success = flash_merged(port)
    
    if success is False:  # File not found
        print("\n⚠️  merged.bin not found, flashing split binaries")
        success = flash_split(port)
    
    if success:
        print("\n✅ Flash completed successfully!")
        print("📱 You can now reset the device or disconnect/reconnect power.")
        
        # Offer to open serial monitor
        monitor = input("\n📡 Open serial monitor? (Y/n): ").lower()
        if monitor == "" or monitor == "y":
            # Ask for custom baud rate
            custom_baud = input(f"Enter baud rate (default {SERIAL_MONITOR_BAUD}): ").strip()
            baud = custom_baud if custom_baud else SERIAL_MONITOR_BAUD
            
            print("\n⏳ Resetting device and opening serial monitor...")
            serial_monitor(port, baud)
    else:
        print("\n❌ Flash operation failed!")
        sys.exit(1)


if __name__ == "__main__":
    main()
