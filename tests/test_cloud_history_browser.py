"""Compile production Drive-history JSON parser with hostile/normal metadata.

Cloud history browsing must stay read-only, bounded, and confined to the
user-visible app-created backup folder tree. No PS2/network is required.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root / "src/cloud_google.c").read_text(encoding="utf-8")
ui = (root / "src/dash_tools.c").read_text(encoding="utf-8")
api = (root / "include/cloud_google.h").read_text(encoding="utf-8")


def function(text, signature):
    start = text.index(signature)
    pos = text.index("{", start) + 1
    depth = 1
    in_string = False
    escape = False
    while depth:
        c = text[pos]
        if in_string:
            if escape:
                escape = False
            elif c == "\\":
                escape = True
            elif c == '"':
                in_string = False
        elif c == '"':
            in_string = True
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        pos += 1
    return text[start:pos] + "\n"


assert "cloudGoogleListBackupHistory" in api
assert "#define CLOUD_GOOGLE_BROWSE_MAX 12" in api
assert "drive_find_child(&session, \"root\", \"WLE-MMCE-Backups\", 1" in src
assert "pageSize=%d" in src
assert '"GET", path' in src
assert "append_url(path, sizeof(path), at_path + n, page_token)" in src
assert "items[n].label = \"Browse cloud history...\"" in ui
assert "cloudBrowseHistory();" in ui
body = function(ui, "static void cloudBrowseHistory(void)")
assert "cloudGoogleListBackupHistory" in body
assert "cloudGoogleIsLinked()" in body
assert "dashWorkerAwaitAll" in body
assert "genRemove" not in body
assert "genOpen" not in body
assert "mcCard" not in body
assert "cloudHttpDownloadFd" not in body

header = api[api.index("#define CLOUD_GOOGLE_BROWSE_MAX"):
             api.index("int cloudGoogleListBackupHistory")]
c = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#define CLOUD_ID_MAX 192
static int genCmpFileExt(const char *name,const char *ext) {
    const char *dot=strrchr(name,'.');
    return dot && !strcasecmp(dot+1,ext);
}
'''
c += header
for signature in ("static int json_string(", "static int drive_safe_id(",
                  "static int drive_browse_parse("):
    c += function(src, signature)

c += r'''
int main(void) {
    CloudGoogleBrowsePage page;
    const char *sample=
        "{\"nextPageToken\":\"NEXT_2\", \"files\":["
        "{\"id\":\"abc_12\",\"name\":\"MemoryCards\","
        "\"mimeType\":\"application/vnd.google-apps.folder\"},"
        "{\"id\":\"Q-9\",\"name\":\"WLE-2026{test}.mcd\","
        "\"mimeType\":\"application/octet-stream\"},"
        "{\"id\":\"q1\",\"name\":\"WLE-2026.ps2\","
        "\"mimeType\":\"application/octet-stream\"},"
        "{\"id\":\"q2\",\"name\":\"FILE.PART\","
        "\"mimeType\":\"application/octet-stream\"}]}";
    memset(&page,0,sizeof(page));
    assert(drive_browse_parse(sample,&page)==0);
    assert(page.count==3);
    assert(page.items[0].is_folder);
    assert(!strcmp(page.items[0].name,"MemoryCards"));
    assert(!strcmp(page.items[1].name,"WLE-2026{test}.mcd"));
    assert(!page.items[1].is_folder);
    assert(!strcmp(page.items[2].id,"q1"));
    assert(json_string(sample,"nextPageToken",page.next_page_token,
                       sizeof(page.next_page_token)));
    assert(!strcmp(page.next_page_token,"NEXT_2"));

    memset(&page,0,sizeof(page));
    assert(drive_browse_parse("{\"files\":[]}",&page)==0 && page.count==0);
    memset(&page,0,sizeof(page));
    assert(drive_browse_parse("{\"files\":[{\"id\":\"bad.id\","
       "\"name\":\"MemoryCards\","
       "\"mimeType\":\"application/vnd.google-apps.folder\"}]}",&page)<0);
    memset(&page,0,sizeof(page));
    assert(drive_browse_parse("{\"files\":[{\"id\":\"a1\","
       "\"name\":\"BROKEN\"}]}",&page)<0);
    memset(&page,0,sizeof(page));
    assert(drive_browse_parse("{\"files\":[{\"id\":\"a1\","
       "\"name\":\"WLE-a.mcd\",\"mimeType\":\"text/plain\"}",&page)<0);
    memset(&page,0,sizeof(page));
    assert(drive_browse_parse("{\"not_files\":[]}",&page)<0);
    puts("PASS: cloud MMCE history metadata parsing stays bounded and read-only");
    return 0;
}
'''
cc = shutil.which("gcc") or shutil.which("clang")
assert cc, "Host C compiler required"
with tempfile.TemporaryDirectory(prefix="cloud-history-") as td:
    source = Path(td) / "history.c"
    executable = Path(td) / "history"
    source.write_text(c, encoding="utf-8")
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror",
                    str(source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
