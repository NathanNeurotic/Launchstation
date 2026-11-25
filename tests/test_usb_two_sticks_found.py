"""Two USB sticks plugged in at once must both be found.

Checks the two changes this depends on:
  1. iop/usbd-ra/src/usbd.c: after a successful hcdInit(), a load without options disables
     both root ports and runs the root-hub change handler once under the driver lock, and
     writes no PORT_RESET of its own (the attach path resets one port at a time). Same
     change as RiptOPL PR 529.
  2. src/dash_devices.c: the paced USB mask rescan keeps running after the hotplug baseline
     is armed while fewer than two units are mounted, so a second stick that mounts late
     still joins the Devices list.
Every place Launchstation starts USBD passes no options.
"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]


def fail(msg):
    sys.exit('FAIL: ' + msg)


usbd = (root / 'iop/usbd-ra/src/usbd.c').read_text(encoding='utf-8')
if not re.search(r'if \(hcdInit\(\) == 0 && argc < 2\) \{\s*'
                 r'memPool\.ohciRegs->HcRhPortStatus\[0\] = BIT\(PORT_CONNECTION\);\s*'
                 r'memPool\.ohciRegs->HcRhPortStatus\[1\] = BIT\(PORT_CONNECTION\);\s*'
                 r'usbdLock\(\);\s*handleRhsc\(\);\s*usbdUnlock\(\);\s*\}', usbd):
    fail('usbd.c does not disable both root ports and run handleRhsc() after hcdInit()')
if usbd.count('hcdInit()') != 1 or 'BIT(PORT_RESET)' in usbd:
    fail('usbd.c should call hcdInit() once and leave port resets to the gated attach path')

init = (root / 'src/init.c').read_text(encoding='utf-8')
loads = re.findall(r'SifExecModuleBuffer\(usbd_irx, size_usbd_irx, (\w+), (\w+),', init)
loads += re.findall(r'emberStartIrx\("USBD", usbd_irx', init)
if not loads or any(l != ('0', 'NULL') for l in loads if isinstance(l, tuple)):
    fail('USBD is loaded with options somewhere: the root-port scan would not run there')
if 'SifExecModuleBuffer(module, size, 0, NULL, &ret);' not in init:
    fail('emberStartIrx no longer loads modules without options')

dash = (root / 'src/dash_devices.c').read_text(encoding='utf-8')
m = re.search(r'if \((filerUsbDriversLoaded\(\)[^{]*?)\)\s*\{\s*usb_rescan_next = Timer\(\) \+ \(dev_hotplug_armed \? '
              r'DEV_USB_RESCAN_ARMED_MS : DEV_USB_RESCAN_MS\);', dash, re.S)
if not m:
    fail('the USB rescan is not paced by DEV_USB_RESCAN_ARMED_MS once hotplug is armed')
if 'dev_hotplug_armed' in m.group(1):
    fail('the USB rescan still stops once hotplug is armed: a late second stick is never listed')
if '(filerUsbDriveMask() & 3) != 3' not in m.group(1):
    fail('the USB rescan must stop once both units are mounted')

print('OK: USBD scans both root ports at load; the Devices list keeps looking for a second stick')
