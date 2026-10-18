"""R3Z fourth classic File Browser display mode: executable host matrix.

Tests the exact production enum/predicates and guards classic/browser callsites.
The native Files tab remains deliberately unchanged while PR #178 owns its UI.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
header = (root / "include/filer_shared.h").read_text(encoding="utf-8")
classic = (root / "src/filer_browser.c").read_text(encoding="utf-8")
listing = (root / "src/filer.c").read_text(encoding="utf-8")
sort = (root / "src/gui_sort.c").read_text(encoding="utf-8")
copy = (root / "src/filer_copy.c").read_text(encoding="utf-8")
dashboard = (root / "src/dash_browser.c").read_text(encoding="utf-8")

first = header.index("enum {\n\tFILE_SHOW_NAME = 0")
end = header.index("extern int file_sort;", first)
helpers = header[first:end]

program = r"""
#include <assert.h>
""" + helpers + r"""
int file_show = FILE_SHOW_NAME_DETAILS;
int main(void)
{
    int i;
    const int titles[] = {0, 0, 1, 1};
    const int details[] = {0, 1, 1, 0};
    for (i = 0; i < 4; i++) {
        file_show = i;
        assert(fileShowTitles() == titles[i]);
        assert(fileShowDetails() == details[i]);
    }
    /* R3Z button behavior: Circle toggles filename/no-details and
     * Square toggles title/no-details, without changing the sort. */
    file_show = FILE_SHOW_NAME_DETAILS;
    file_show = file_show == FILE_SHOW_TITLE ? FILE_SHOW_TITLE_DETAILS : FILE_SHOW_TITLE;
    assert(file_show == FILE_SHOW_TITLE);
    file_show = file_show == FILE_SHOW_TITLE ? FILE_SHOW_TITLE_DETAILS : FILE_SHOW_TITLE;
    assert(file_show == FILE_SHOW_TITLE_DETAILS);
    file_show = file_show == FILE_SHOW_NAME ? FILE_SHOW_NAME_DETAILS : FILE_SHOW_NAME;
    assert(file_show == FILE_SHOW_NAME);
    file_show = file_show == FILE_SHOW_NAME ? FILE_SHOW_NAME_DETAILS : FILE_SHOW_NAME;
    assert(file_show == FILE_SHOW_NAME_DETAILS);
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="r3z-classic-modes-") as td:
    src = Path(td) / "matrix.c"
    output = Path(td) / "matrix"
    src.write_text(program, encoding="utf-8")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    str(src), "-o", str(output)], check=True)
    subprocess.run([str(output)], check=True)

assert "int file_show = FILE_SHOW_NAME_DETAILS;" in listing
assert "if (fileShowTitles() || (file_sort == 2))" in listing
assert "t = fileShowTitles();" in sort
assert "if (fileShowTitles() || setting->PSU_HugeNames)" in copy

for snippet in (
    "file_show = (file_show == FILE_SHOW_NAME) ? FILE_SHOW_NAME_DETAILS : FILE_SHOW_NAME;",
    "file_show = (file_show == FILE_SHOW_TITLE) ? FILE_SHOW_TITLE_DETAILS : FILE_SHOW_TITLE;",
    "if (fileShowTitles() && (elisaFnt == NULL)",
    "return (fileShowTitles() && (elisaFnt != NULL))",
    "else if (fileShowTitles() && files[top + i].title[0] != 0)",
    "LNG(Filename), file_show == FILE_SHOW_NAME_DETAILS ? LNG(_plus_Details) :",
    "LNG(Game_Title), file_show == FILE_SHOW_TITLE_DETAILS ? LNG(_plus_Details) :",
    "(i == 2 && !fileShowTitles()) || (i == 3 && fileShowTitles())",
):
    assert snippet in classic, snippet

assert classic.count("if (fileShowDetails())") >= 2

# PR #178 owns native Files layout. Its established modes retain their
# original 0, 1 and 2 numeric values, and no UI/storage worker is modified.
assert 'static int fb_show = 1;' in dashboard
assert '"File names", "Names and details", "Game titles and details"' in dashboard
assert "file_show = fb_show;" in dashboard or "file_show = fbShowsTitles(fb_show) ? 2 : fb_show;" in dashboard

print("R3Z classic 4 display modes: compiled C matrix and source guards pass")
