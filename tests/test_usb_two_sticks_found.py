"""Two USB sticks plugged in at once must both be found.

Checks the two changes this depends on:
  1. iop/usbd-ra/src/usbd.c: after a successful hcdInit(), a load without options disables
     both root ports and runs the root-hub change handler once under the driver lock, and
     writes no PORT_RESET of its own (the attach path resets one port at a time). Same
     change as RiptOPL PR 529.
  2. src/dash_devices.c: once the hotplug baseline is armed, the USB mask is re-checked
     only when the Devices list is opened (openDir("") requests it), never on a timer. The
     boot-time 500 ms polling stops once two units are mounted. A stick that mounts late or
     is pulled out shows the next time the list is opened, and an idle dashboard does no USB I/O.
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
m = re.search(r'if \((\(usb_recheck_requested \|\| \(!dev_hotplug_armed && \(filerUsbDriveMask\(\) & 3\) != 3\)\)[^{]*?)\)\s*\{\s*'
              r'usb_recheck_requested = 0;\s*usb_rescan_next = Timer\(\) \+ DEV_USB_RESCAN_MS;', dash, re.S)
if not m:
    fail('after hotplug is armed the USB rescan must run only on a Devices-list request, and consume it')
if 'ARMED_MS' in dash:
    fail('no timed USB re-check may run once hotplug is armed')

browser = (root / 'src/dash_browser.c').read_text(encoding='utf-8')
if not re.search(r"static void openDir\(const char \*dir, const char \*select\)\n\{.*?"
                 r"if \(dir\[0\] == '\\0'\)\s*dashDevicesRequestUsbRecheck\(\);", browser, re.S):
    fail('opening the Devices list (openDir("")) must request one USB re-check')

print('OK: USBD scans both root ports at load; the Devices list re-checks USB when it is opened')
