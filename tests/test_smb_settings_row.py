"""Settings > Network > SMB shares: the GUI editor page and its SMB2.CNF text core."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
settings = (root / "src/dash_settings.c").read_text()
shares = (root / "src/smb_shares.c").read_text()
cnf = (root / "src/smb_cnf.c").read_text()
header = (root / "include/smb_shares.h").read_text()

# one row on the Network page, only in SMB builds, opening the editor page
net = settings.split("static const SetItem net_items[] = {", 1)[1].split("};", 1)[0]
assert re.search(r'#ifdef SMB\s*\n\s*\{ST_PAGE, P_SMB, "SMB shares"', net), net

# the page offers the field rows and the save/delete/text actions
page = settings.split("static const SetItem smb_items[] = {", 1)[1].split("};", 1)[0]
for sid in ("S_SMBED_PICK", "S_SMBED_NAME", "S_SMBED_KIND", "S_SMBED_IP", "S_SMBED_PORT",
            "S_SMBED_FOLDER", "S_SMBED_USER", "S_SMBED_PASS", "S_SMBED_SAVE",
            "S_SMBED_DELETE", "S_SMBED_TEXT"):
    assert sid in page, sid
assert "[P_SMB] = PAGE(" in settings

# opening the page refreshes the draft from the file
enter = settings.split('it->type == ST_PAGE && (in->pressed & (DASH_IN_OK | DASH_IN_RIGHT))', 1)[1]
assert "smbedLoadPick();" in enter.split("openPage(it->id);", 1)[0]

# The selected block is merged into SMB2.CNF with heap buffers, validated,
# written to the primary path (and synced to SYS-CONF if both exist), and reloaded.
save = settings.split("static void smbedSave(void)", 1)[1].split("\n}\n", 1)[0]
assert "smbCnfBuildBlock(" in save and "smbedWriteFile(block)" in save
assert ", smbed.name);" in save and ", block);" not in save
write = settings.split("static int smbedWriteFile(const char *block)", 1)[1].split("\n}\n", 1)[0]
assert "malloc(SMB_CNF_MAX_SIZE + 512)" in write and "static char" not in write
# Parse the original share identity before editing; validate the resulting
# primary and independently preflight SYS-CONF before writing either file.
assert write.index("smbedReadText(path") < write.index("smbConfigParse(work, &cfg);")
assert write.index("smbConfigParse(work, &cfg);") < write.index("smbCnfUpsertText(")
assert write.index("smbCnfUpsertText(") < write.index("smbConfigParse(work, &cfg);", write.index("smbCnfUpsertText("))
assert write.index("smbedReadText(sysconf") < write.index("dashWriteFile(path, out, len)")
assert "smbedBuildSysconfPath(sysconf" in write
assert "smbSharesReload()" in write
assert "smbed.partial_pending = 1;" in write
assert "smbed.kind = sh->config_version" in settings
assert "sh->config_version != original_config_version" in write

# the text-editor row keeps the template seeding and reloads afterwards
case = settings.split("case S_SMBED_TEXT:", 1)[1].split("break;", 1)[0]
assert "smbSharesConfigured(" in case
assert "smbedBuildSysconfPath(" in case and "configEnsureSysconfDir(" in case
assert "dashTextEditor(" in case
assert case.index("dashTextEditor(") < case.index("smbSharesReload()")

# the status column shows the draft values without network IO
status = settings.split("static const char *actionStatus(int id)", 1)[1].split("\n}\n", 1)[0]
for sid in ("S_SMBED_PICK", "S_SMBED_NAME", "S_SMBED_KIND", "S_SMBED_SAVE"):
    assert f"case {sid}:" in status, sid

# the template is a valid config: the host-side parser must accept it
template = re.search(r'smb_template\[\] =\s*((?:\s*"(?:[^"\\]|\\.)*"\s*)+);', settings, re.S)
assert template, "no SMB2.CNF template"
text = "".join(eval(part) for part in re.findall(r'"(?:[^"\\]|\\.)*"', template.group(1)))
assert text.count("URL = ") >= 1 and "NAME = " in text and "VERSION = 1" in text
# every example is commented out: a fresh file defines no share and connects nowhere
assert all(line.startswith("#") or not line.strip() for line in text.splitlines()), text

# the template only goes into a file that does not exist yet
create = case.split("smb_template[] =", 1)[1]
# A read failure cannot justify truncation; creation is stat-guarded and verified.
seed = settings.split("static int smbedSeedTemplateIfAbsent(", 1)[1].split("\n}\n", 1)[0]
assert "genGetStat(path, &st) != -ENOENT" in seed
assert "dashWriteFile(path, template_text, size)" in seed
assert "smbedSeedTemplateIfAbsent(path, smb_template" in create
assert "FIO_O_TRUNC" not in create

# the upsert core exists in the host-tested text file and is declared in the header
assert "int smbCnfBuildBlock(" in cnf and "int smbCnfUpsertText(" in cnf
assert "int smbCnfUpsertText(" in header and "int smbCnfBuildBlock(" in header

# reload retires driver sessions and forgets the parse, so the next smb:/ open reads the edited file
assert "int smbSharesReload(void);" in header
disconnect = shares.split("static int smbDisconnectAllSessions(void)", 1)[1].split("\n}\n", 1)[0]
assert "SMB2_DEVCTL_DISCONNECT_ALL" in disconnect and "SMB_DEVCTL_LOGOFF" in disconnect
reload = shares.split("int smbSharesReload(void)", 1)[1].split("\n}\n", 1)[0]
assert "smbDisconnectAllSessions()" in reload
assert reload.index("smbDisconnectAllSessions()") < reload.index("smb_cfg_loaded = 0;")
assert "return 0;" in reload and "return 1;" in reload

print("PASS: Settings > Network > SMB shares edits SMB2.CNF by GUI or text, and reloads it")
