"""Settings > Network edits SMB2.CNF; the file is otherwise invisible in the UI."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
settings = (root / "src/dash_settings.c").read_text()
shares = (root / "src/smb_shares.c").read_text()
header = (root / "include/smb_shares.h").read_text()

# one row on the Network page, only in SMB builds, naming the file
net = settings.split("static const SetItem net_items[] = {", 1)[1].split("};", 1)[0]
assert re.search(r'#ifdef SMB\s*\n\s*\{ST_ACTION, S_SMB_SHARES, "SMB shares \(SMB2\.CNF\)"', net), net

# the action opens the file in use, or seeds a new one beside IPCONFIG.DAT
case = settings.split("case S_SMB_SHARES:", 1)[1].split("break;", 1)[0]
assert "smbSharesConfigPath()" in case
assert "configBuildSysconfPath(" in case and "configEnsureSysconfDir(" in case
assert "dashTextEditor(" in case
assert case.index("dashTextEditor(") < case.index("smbSharesReload()")

# the status column says whether anything is configured, without network IO
status = settings.split("static const char *actionStatus(int id)", 1)[1].split("\n}\n", 1)[0]
assert "case S_SMB_SHARES:" in status
assert "smbSharesConfigured(" in status

# the template is a valid config: the host-side parser must accept it
template = re.search(r'smb_template\[\] =\s*((?:\s*"(?:[^"\\]|\\.)*"\s*)+);', settings, re.S)
assert template, "no SMB2.CNF template"
text = "".join(eval(part) for part in re.findall(r'"(?:[^"\\]|\\.)*"', template.group(1)))
assert text.count("URL = ") >= 1 and "NAME = " in text and "VERSION = 1" in text
assert all(line.startswith(("#", "NAME", "USERNAME", "PASSWORD", "URL", "VERSION")) or not line.strip()
           for line in text.splitlines()), text

# reload forgets the parse, so the next smb:/ open reads the edited file
assert "void smbSharesReload(void);" in header
reload = shares.split("void smbSharesReload(void)", 1)[1].split("\n}\n", 1)[0]
assert "smb_cfg_loaded = 0;" in reload

print("PASS: Settings > Network > SMB shares edits SMB2.CNF and reloads it")
