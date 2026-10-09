"""Verify optional controller support in the linked ELFs, before stripping."""
import argparse
import subprocess


def symbols(nm, elf):
    output = subprocess.check_output([nm, "--defined-only", str(elf)], text=True)
    return {line.split()[-1] for line in output.splitlines() if line.split()}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("nm")
    parser.add_argument("standard")
    parser.add_argument("ds34")
    args = parser.parse_args()
    required = {"padPollingThread", "ds34usb_init", "ds34bt_init",
                "ds34usb_irx", "size_ds34usb_irx", "ds34bt_irx", "size_ds34bt_irx"}
    standard = symbols(args.nm, args.standard)
    enabled = symbols(args.nm, args.ds34)
    if required & standard:
        raise SystemExit(f"Standard ELF unexpectedly contains DS34: {required & standard}")
    if required - enabled:
        raise SystemExit(f"DS34 ELF is missing controller components: {required - enabled}")
    for variant in (standard, enabled):
        if not {"setupPad", "clearPadPressState"} <= variant:
            raise SystemExit("Controller lifecycle API missing from a variant")
    print("PASS: standard excludes DS34; enabled ELF links polling, USB/BT clients and embedded drivers")
