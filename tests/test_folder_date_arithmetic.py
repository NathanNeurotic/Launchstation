"""Compare production folder-date carry/borrow against Gregorian arithmetic."""
from calendar import monthrange
import ctypes
from datetime import datetime, timedelta
from pathlib import Path
import random
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
actions = (root / "src/filer_actions.c").read_text()
title = (root / "src/main_title.c").read_text()


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


code = r'''
#include <stdint.h>
#include <string.h>
typedef uint64_t u64;
typedef struct {
    unsigned char Resv2, Sec, Min, Hour, Day, Month;
    unsigned short Year;
} sceMcStDateTime;
typedef struct { sceMcStDateTime *timestamp; int position, reserve_tuna; } FilerDateEditor;
static int local_offset, date_format;
static int getOsdLocalOffsetMinutes(void) { return local_offset; }
static void menuTitleGetClockFormat(int *use12, int *format) {
    if(use12) *use12=0;
    if(format) *format=date_format;
}
'''
code += re.search(r"^#define MENU_TITLE_PS2_RTC_BASE_OFFSET_MINUTES .+$", title, re.M).group() + "\n"
start = title.index("typedef struct")
code += title[start:title.index("} MenuTitleClockState;") + len("} MenuTitleClockState;")] + "\n"
for signature in ("static int daysInMonth(", "static void advanceClockSeconds(",
                  "static void shiftClockMinutes(", "static int isValidTimestamp(",
                  "static int convertTimestampMinutes(",
                  "int menuTitleConvertTimestampToLocalTime(", "int menuTitleConvertTimestampFromLocalTime("):
    code += function(title, signature)
start = actions.index("enum {\n\tCUSTOM_DATE_HOUR")
code += actions[start:actions.index("};", start) + 2] + "\n"
for signature in ("static int isCustomDateLeapYear(", "static int getCustomDateDaysInMonth(",
                  "static int isCustomFolderTimestampInRange(",
                  "static int convertCustomTimestampToLocalTime(", "static int convertCustomTimestampFromLocalTime(",
                  "static void normalizeCustomFolderTimestamp(", "static void normalizeCustomLocalTimestamp(",
                  "static int stepCustomLocalDateField(", "static void adjustCustomDateField(",
                  "static int getCustomDateFieldForDisplayPosition(", "void filerDateEditorAdjust("):
    code += function(actions, signature)
code += r'''
void adjust(sceMcStDateTime *timestamp, int field, int delta, int reserve, int offset) {
    local_offset=offset;
    adjustCustomDateField(timestamp,field,delta,reserve);
}
void adjustDisplay(sceMcStDateTime *timestamp, int position, int delta, int reserve, int offset, int format) {
    FilerDateEditor editor={timestamp,position,reserve};
    local_offset=offset; date_format=format;
    filerDateEditorAdjust(&editor,delta);
}
'''


class Timestamp(ctypes.Structure):
    _fields_ = [(name, ctypes.c_ubyte) for name in ("Resv2", "Sec", "Min", "Hour", "Day", "Month")]
    _fields_ += [("Year", ctypes.c_ushort)]


def stamp(value):
    return Timestamp(0xA5, value.second, value.minute, value.hour, value.day, value.month, value.year)


def as_date(value):
    return datetime(value.Year, value.Month, value.Day, value.Hour, value.Min, value.Sec)


def step(value, field, delta):
    if field in (0, 1, 2, 5):
        seconds = {0: 3600, 1: 60, 2: 1, 5: 86400}[field]
        return value + timedelta(seconds=seconds * delta)
    if field == 3:
        year, month = value.year + delta, value.month
    else:
        year, month = divmod(value.year * 12 + value.month - 1 + delta, 12)
        month += 1
    return value.replace(year=year, month=month, day=min(value.day, monthrange(year, month)[1]))


compiler = shutil.which("gcc") or shutil.which("cc")
assert compiler, "Host C compiler required"
checks = 0
with tempfile.TemporaryDirectory(prefix="folder-date-arithmetic-") as folder:
    c = Path(folder) / "check.c"
    library = Path(folder) / "check.so"
    c.write_text(code)
    subprocess.run([compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
                    str(c), "-o", str(library)], check=True)
    lib = ctypes.CDLL(str(library))
    lib.adjust.argtypes = [ctypes.POINTER(Timestamp)] + [ctypes.c_int] * 4
    lib.adjust.restype = None
    lib.adjustDisplay.argtypes = [ctypes.POINTER(Timestamp)] + [ctypes.c_int] * 5
    lib.adjustDisplay.restype = None

    def check(raw, field, delta, reserve=0, offset=540):
        global checks
        shift = timedelta(minutes=offset - 540)
        try:
            changed = step(raw + shift, field, delta) - shift
            if not 1 <= changed.year <= 2099:
                changed = raw
        except (ValueError, OverflowError):
            changed = raw
        if reserve and changed == datetime(2099, 12, 31, 23, 59, 59):
            changed = changed.replace(second=58)
        actual = stamp(raw)
        lib.adjust(ctypes.byref(actual), field, delta, reserve, offset)
        assert as_date(actual) == changed, (raw, field, delta, reserve, offset, as_date(actual), changed)
        assert actual.Resv2 == 0xA5
        checks += 1

    examples = [datetime(2000, 1, 1), datetime(1999, 12, 1), datetime(1999, 12, 31, 23, 59, 59),
                datetime(2000, 1, 31), datetime(2000, 2, 28), datetime(2000, 2, 29),
                datetime(2000, 3, 1), datetime(1900, 2, 28), datetime(1900, 3, 1),
                datetime(2004, 2, 29), datetime(2023, 3, 1), datetime(2024, 2, 29, 23, 59, 59),
                datetime(2024, 4, 30, 23, 59, 59), datetime(2024, 5, 1),
                datetime(2099, 12, 31, 23, 59, 58)]
    for offset in (-720, -480, 0, 345, 540, 840):
        for dst in (0, 60):
            local_offset = offset + dst
            shift = timedelta(minutes=local_offset - 540)
            for local in examples:
                raw = local - shift
                if not 1 <= raw.year <= 2099:
                    continue
                for field in range(6):
                    for delta in (-1, 1):
                        check(raw, field, delta, offset=local_offset)
    rng = random.Random(0)
    for _ in range(300):
        year, month = rng.randint(2, 2098), rng.randint(1, 12)
        value = datetime(year, month, rng.randint(1, monthrange(year, month)[1]),
                         rng.randint(0, 23), rng.randint(0, 59), rng.randint(0, 59))
        for field in range(6):
            for delta in (-1, 1):
                check(value, field, delta, offset=rng.choice((-720, -420, 0, 330, 540, 765, 900)))

    # Time-unit steps at the stored endpoints remain valid even when the local
    # date crosses into year zero or 2100, which Python cannot always represent.
    for offset in (-720, 540, 900):
        for raw, delta in ((datetime(1, 1, 1), -1), (datetime(2099, 12, 31, 23, 59, 59), 1)):
            for field in (0, 1, 2, 5):
                actual = stamp(raw)
                lib.adjust(ctypes.byref(actual), field, delta, 0, offset)
                assert as_date(actual) == raw
    for field in range(6):
        for delta in (-1, 1):
            check(datetime(2099, 12, 31, 23, 59, 58), field, delta, reserve=1)
    unchanged = stamp(datetime(2000, 1, 1))
    lib.adjust(ctypes.byref(unchanged), 4, 0, 0, 540)
    assert as_date(unchanged) == datetime(2000, 1, 1)

    # Native/classic editors share the same arithmetic in every display order.
    orders = ((0, 1, 2, 3, 4, 5), (0, 1, 2, 4, 5, 3), (0, 1, 2, 5, 4, 3))
    raw = datetime(2000, 1, 1)
    for fmt, order in enumerate(orders):
        for position, field in enumerate(order):
            for delta in (-1, 1):
                actual = stamp(raw)
                lib.adjustDisplay(ctypes.byref(actual), position, delta, 0, 540, fmt)
                assert as_date(actual) == step(raw, field, delta)

print(f"PASS: {checks} Gregorian carry/borrow comparisons, leap/month/year boundaries, timezones/DST, timestamp limits, reserved Tuna date and all display field orders")
