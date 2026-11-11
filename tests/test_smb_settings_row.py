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

# The selected block is merged into the share blocks (an old SMB2.CNF's on the
# first save, never writing SMB2.CNF), validated, kept for LAUNCHSTATION.CNF,
# saved with the settings and reloaded.
save = settings.split("static void smbedSave(void)", 1)[1].split("\n}\n", 1)[0]
assert "smbCnfBuildBlock(" in save and "smbedWriteFile(block)" in save
assert ", smbed.name);" in save and ", block);" not in save
write = settings.split("static int smbedWriteFile(const char *block)", 1)[1].split("\n}\n", 1)[0]
assert "malloc(SMB_CNF_MAX_SIZE + 512)" in write and "static char" not in write
assert "smbSharesBlocks()" in write and "dashWriteFile" not in write and "genOpen" not in write
assert write.index("smbCnfUpsertText(") < write.index("smbConfigParse(work, &cfg);")
assert write.index("smbConfigParse(work, &cfg);") < write.index("smbSharesSetBlocks(out)")
assert write.index("smbSharesSetBlocks(out)") < write.index("saveSharesCnf()") < write.index("smbSharesReload()")
# a failed write puts the old blocks back and reports no success
assert "smbSharesSetBlocks(old);" in write and "ret = -5;" in write
# share saves write the committed settings and wait for the write
helper = settings.split("static int saveSharesCnf(void)\n{", 1)[1].split("\n}\n", 1)[0]
assert "memcpy(setting, &snap_classic, sizeof(SETTING));" in helper
assert helper.index("dashSettingsSaveCnf()") < helper.index("dashWorkerAwaitAll(") < helper.index("cfg_failed[CFG_SAVE_CNF]")

# the text-editor row seeds the blocks (old SMB2.CNF or the template), saves
# LAUNCHSTATION.CNF, edits it and takes the edited blocks back
case = settings.split("case S_SMBED_TEXT:", 1)[1].split("\t\t\tbreak;\n\t\t}", 1)[0]
assert "smbSharesConfigured(" in case and "smbSharesSetBlocks(" in case
assert "genOpen" not in case and "dashWriteFile" not in case
assert case.index("saveSharesCnf()") < case.index("dashTextEditor(LoadedConfigPath)")
assert case.index("dashTextEditor(") < case.index("smbSharesSetSection(text)") < case.index("smbSharesReload()")

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

# the memory-card SMB server login sits on the Network page and in LAUNCHSTATION.CNF
assert re.search(r'#ifdef ETH[^#]*S_SMBD_USER[^#]*S_SMBD_PASS', net), net
config = (root / "src/config.c").read_text()
assert '"SMB_Server_User = %s' in config and '"SMB_Server_User")' in config
# the share blocks are the last thing written, after every setting
build = config.split("char *configBuildSaveText(int *out_size)", 1)[1].split("\n}\n", 1)[0]
assert build.index("PathPad[%02d]") < build.index("SMB_CNF_SECTION") < build.index("*out_size = CNF_size;")
assert "smbSharesSetSection(RAM_p);" in config

print("PASS: Settings > Network > SMB shares edits the shares in LAUNCHSTATION.CNF by GUI or text, and reloads them")
