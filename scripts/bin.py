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


def test_connection(port):
    """Test connection to the ESP32-C6"""
    print("\n🔍 Testing connection to ESP32-C6...")
    success = run_esptool([
        "--chip", CHIP,
        "--port", port,
        "--baud", BAUD,
        "chip_id",
    ], retry_on_fail=True)
    
    if success:
        print("✅ Connection successful!")
        return True
    else:
        print("❌ Connection failed!")
        return False


def erase_flash(port):
    """Erase the entire flash memory"""
    print("\n🗑️  Erasing flash memory...")
    print("⚠️  IMPORTANT: Make sure the device is in bootloader mode!")
    print("   If erase fails, manually enter bootloader mode:")
    print("   1. Hold BOOT button (GPIO9)")
    print("   2. Press and release RESET button")
    print("   3. Release BOOT button")
    print("   4. Press any key to continue...")
    input()
    
    # Try at normal baud rate first
    success = run_esptool([
        "--chip", CHIP,
        "--port", port,
        "--baud", BAUD,
        "erase-flash",
    ], retry_on_fail=False)
    
    # If failed, try with lower baud rate (more reliable)
    if not success:
        print("\n⚠️  Retrying with lower baud rate (115200)...")
        print("   Put device in bootloader mode again if needed...")
        import time
        time.sleep(2)
        
        success = run_esptool([
            "--chip", CHIP,
            "--port", port,
            "--baud", "115200",  # Lower baud rate is more reliable for erase
            "erase-flash",
        ], retry_on_fail=False)
    
    if success:
        print("✅ Flash erased successfully")
        return True
    else:
        print("❌ Flash erase failed")
        print("\n💡 TIP: Flash erase requires stable connection.")
        print("   Try: Close all serial monitors, use a good USB cable,")
        print("   and ensure device is properly in bootloader mode.")
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
        "write_flash",
        "--flash_mode", FLASH_MODE,
        "--flash_freq", FLASH_FREQ,
        "--flash_size", FLASH_SIZE,
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

    # Test connection first (optional)
    test = input(f"\n🔍 Test connection first? (recommended) (Y/n): ").lower()
    if test == "" or test == "y":
        if not test_connection(port):
            cont = input("\nConnection test failed. Continue anyway? (y/N): ").lower()
            if cont != "y":
                print("❌ Aborted")
                sys.exit(1)

    # Ask if user wants to erase flash first
    erase = input(f"\n⚠️  Erase flash before flashing? (y/N): ").lower()
    
    confirm = input(f"\nFlash ESP32-C6 on {port}? (y/N): ").lower()
    if confirm != "y":
        print("❌ Aborted")
        sys.exit(0)

    # Erase flash if requested
    if erase == "y":
        if not erase_flash(port):
            print("\n⚠️  Flash erase failed!")
            skip = input("Continue with flashing anyway? (y/N): ").lower()
            if skip != "y":
                print("❌ Aborted.")
                sys.exit(1)
            print("\n⏩ Skipping erase, proceeding with flash...")

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
