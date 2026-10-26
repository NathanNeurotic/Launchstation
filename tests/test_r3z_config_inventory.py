"""Keep the reviewed configuration inventory and reachable UI map current."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
subprocess.run([sys.executable, str(root / 'scripts/audit_r3z_settings.py')],
               cwd=root, check=True)
