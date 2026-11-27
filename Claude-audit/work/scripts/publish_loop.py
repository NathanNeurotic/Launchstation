"""Republish the interim reports every N minutes until tmp/STOP_PUBLISH exists or max hours pass.

Usage: python -I publish_loop.py <work_root> <minutes> <max_hours>
"""
import os
import subprocess
import sys
import time

root = os.path.abspath(sys.argv[1])
every = float(sys.argv[2]) * 60
deadline = time.time() + float(sys.argv[3]) * 3600
stop = os.path.join(root, "tmp", "STOP_PUBLISH")
log = os.path.join(root, "tmp", "publish.log")

while time.time() < deadline and not os.path.exists(stop):
    time.sleep(every)
    if os.path.exists(stop):
        break
    r = subprocess.run([sys.executable, "-I", os.path.join(root, "scripts", "publish.py"), root, "RiptOPL", "Launchstation"],
                       capture_output=True, text=True, encoding="utf-8", errors="replace")
    with open(log, "a", encoding="utf-8") as fh:
        fh.write("== %s\n%s%s\n" % (time.strftime("%Y-%m-%d %H:%M:%S"), r.stdout, r.stderr))
print("publish loop ended")
