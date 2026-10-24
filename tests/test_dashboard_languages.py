"""Check table compatibility and execute the production dashboard resolver.

UI labels opt in; the text renderer must never translate application metadata
or filenames. This test checks catalogs and resolution, not target font layout.
"""
from pathlib import Path
import ast
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
files = ('ENG', 'SPA', 'FRE', 'ITA', 'POL', 'POR', 'PTBR', 'GER', 'HUN')
pattern = r'lang\((\d+),\s*(\w+),\s*("(?:[^"\\]|\\.)*")\)'
tables = []
for name in files:
    entries = [(int(m[1]), m[2], ast.literal_eval(m[3]))
               for m in re.finditer(pattern, (root / 'Lang' / (name + '.LNG')).read_text())]
    assert [entry[0] for entry in entries] == list(range(len(entries))), name
    tables.append(entries)
assert all([(n, key) for n, key, _ in entries] ==
           [(n, key) for n, key, _ in tables[0]] for entries in tables)

# Appended IDs preserve the 468 legacy/external-LNG positions.
assert tables[0][467][1] == 'New_to_Old'
assert all(key.startswith('Dashboard_') for _, key, _ in tables[0][468:])
formats = re.compile(r'%(?:[-+ #0]*)(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|[hljztL])?[diouxXfFeEgGaAcspn%]')
def signature(text):
    return [(m[0].count('*'), re.sub(r'^%[-+ #0\d.*]*', '%', m[0]))
            for m in formats.finditer(text) if m[0] != '%%']
for table in tables:
    for english, translated in zip(tables[0][468:], table[468:]):
        assert translated[2], translated
        assert signature(english[2]) == signature(translated[2]), (english, translated)

mapping = (root / 'Lang/DASHBOARD.inc').read_text()
keys = [ast.literal_eval(m[1]) for m in re.finditer(r'\{("(?:[^"\\]|\\.)*"), LANG_', mapping)]
assert keys == sorted(set(keys))
settings = (root / 'src/dash_settings.c').read_text()
labels = set(re.findall(r'\{ST_\w+,\s*\w+,\s*"([^"]+)"', settings))
assert labels <= set(keys) | {'Neutrino', 'POPStarter'}, labels - set(keys)
assert 'localizeDashboardText' not in (root / 'src/dash_font.c').read_text()

source = (root / 'src/lang.c').read_text()
function = source[source.index('const char *localizeDashboardText('):]
code = '#include <assert.h>\n#include <stddef.h>\n#include <stdio.h>\n#include <string.h>\n'
code += 'typedef struct { const char *String; } Language;\nenum {\n'
code += ''.join(f'LANG_{key} = {n},\n' for n, key, _ in tables[0]) + 'LANG_COUNT };\n'
for name in files:
    path = (root / 'Lang' / (name + '.LNG')).as_posix()
    code += f'Language table_{name}[] = {{\n#define lang(id, name, value) {{value}},\n#include "{path}"\n#undef lang\n}};\n'
code += 'Language Lang_String[LANG_COUNT];\n'
code += function.replace('#include "../Lang/DASHBOARD.inc"', mapping)
code += '''
int main(void) {
    Language *tables[] = {table_ENG, table_SPA, table_FRE, table_ITA,
        table_POL, table_POR, table_PTBR, table_GER, table_HUN};
    assert(!strcmp(localizeDashboardText("Files"), "Files"));
    for (int language = 0; language < 9; language++) {
        memcpy(Lang_String, tables[language], sizeof(Lang_String));
        assert(!strcmp(localizeDashboardText("Home"),
            tables[language][LANG_Dashboard_Home].String));
        assert(!strcmp(localizeDashboardText("Copy"),
            tables[language][LANG_Copy].String));
        assert(!strcmp(localizeDashboardText("Remaining size: %.2f MB"),
            tables[language][LANG_Dashboard_Remaining_size_2f_MB].String));
        assert(!strcmp(localizeDashboardText("mass0:/Files/BOOT.ELF"), "mass0:/Files/BOOT.ELF"));
        assert(localizeDashboardText(NULL) == NULL);
        Lang_String[LANG_Dashboard_Home].String = "external override";
        assert(!strcmp(localizeDashboardText("Home"), "external override"));
        Lang_String[LANG_Dashboard_Home].String = NULL;
        assert(!strcmp(localizeDashboardText("Home"), "Home"));
    }
    puts("PASS: nine catalogs, legacy IDs, formats, live language/override resolution and unchanged user paths");
}
'''
with tempfile.TemporaryDirectory(prefix='dashboard-languages-') as folder:
    path = Path(folder)
    (path / 'test.c').write_text(code)
    compiler = shutil.which('gcc') or shutil.which('clang')
    assert compiler, 'Host C compiler required'
    subprocess.run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
