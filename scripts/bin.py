import subprocess
import sys
from pathlib import Path
import serial.tools.list_ports

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
                    import time
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
    else:
        print("\n❌ Flash operation failed!")
        sys.exit(1)


if __name__ == "__main__":
    main()
