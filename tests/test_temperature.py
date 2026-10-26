"""Exercise the R3Z sensor poll/cache and dashboard clock temperature display."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
title = (root / "src/main_title.c").read_text()
ui = (root / "src/dash_ui.c").read_text()
header = (root / "include/dash_internal.h").read_text()


def function(source, signature):
    start = source.index(signature)
    pos = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos] + "\n"


code = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t u64;
static int sensor_raw=40*128,sensor_ok=1,sensor_status,sensor_queries;
static int sceCdApplySCmd(unsigned char cmd,const void *in,int size,void *out) {
    assert(cmd==0x03 && size==1 && *(const unsigned char *)in==0xEF);
    unsigned char *bytes=out;sensor_queries++;
    bytes[0]=sensor_status;bytes[1]=sensor_raw>>8;bytes[2]=sensor_raw&255;
    return sensor_ok;
}
'''
end = title.index("} MenuTitleTempState;") + len("} MenuTitleTempState;")
code += title[title.rfind("typedef struct", 0, end):end] + "\n"
code += "static MenuTitleTempState title_temp;\n"
code += re.search(r"^#define MENU_TITLE_TEMP_POLL_MS .+$", title, re.M).group() + "\n"
for name in ("static int queryTemperatureCelsius(", "static int updateTemp(", "int menuTitleGetTemperatureRaw("):
    code += function(title, name)
code += header[header.index("enum DashClockMode"):header.index("enum DashWallMode")]
code += r'''
static struct { int temp_fahrenheit; } dashCfg;
static struct { int clock,clock_pos,clock_hours,clock_seconds,clock_date; } dashTheme;
static int clock_valid=1;
static int menuTitleGetClockFields(int *y,int *mo,int *d,int *h,int *mi,int *s,int *u,int *fmt) {
    *y=2026;*mo=10;*d=25;*h=23;*mi=59;*s=58;*u=0;*fmt=0;return clock_valid;
}
static void dashStrCopy(char *out,const char *in,int size) { snprintf(out,size,"%s",in); }
#define DASH_SAFE_R 592
#define DASH_SAFE_L 48
#define DASH_FS_BODY 0.8f
#define DASH_FS_LABEL 0.9f
#define DASH_FS_SMALL 0.66f
#define DASH_FS_CAPTION 0.58f
#define DASH_C_TEXT 1
#define DASH_C_TEXT2 2
#define DASH_C_TEXT3 3
static int dashVisibleSectionCount(void) { return 4; }
static float font_factor=1.15f;
static float dashTextWidth(const char *s,float scale) { return strlen(s)*10*scale*font_factor; }
static struct { char text[80];float x,y,scale; } drawn[8];
static int draws;
static void record(float x,float y,const char *text,float scale) {
    assert(draws<8);snprintf(drawn[draws].text,sizeof(drawn[draws].text),"%s",text);
    drawn[draws].x=x;drawn[draws].y=y;drawn[draws++].scale=scale;
}
static void dashTextRight(float x,float y,const char *text,float scale,int color) { (void)color;record(x-dashTextWidth(text,scale),y,text,scale); }
static float dashTextS(float x,float y,const char *text,float scale,int color) { (void)color;record(x,y,text,scale);return x+dashTextWidth(text,scale); }
'''
for macro in ("HEADER_TAB_X", "HEADER_TAB_W", "HEADER_TAB_PAD"):
    code += re.search(r"^#define " + macro + r" .+$", ui, re.M).group() + "\n"
for name in ("void menuTitleFormatClockTime(", "void menuTitleFormatClockDate("):
    code += function(title, name)
for name in ("static void appendClockTemperature(", "static int clockText(", "void dashDrawClock(",
             "static float footerClockWidth(", "static void drawFooterClock("):
    # Footer functions also have forward declarations.
    signature = name
    begin = ui.index(signature)
    if ui.index(";", begin) < ui.index("{", begin):
        begin = ui.index(signature, begin + len(signature))
    code += function(ui[begin:], signature)
code += r'''
static void show(const char *expected) {
    char text[48]="23:59:58";int before=sensor_queries;
    appendClockTemperature(text,sizeof(text));assert(!strcmp(text,expected));
    assert(sensor_queries==before); /* units and drawing use the cached raw value */
}
static void resetSensor(void) { memset(&title_temp,0,sizeof(title_temp));sensor_queries=0;sensor_ok=1;sensor_status=0; }
int main(void) {
    int raw=-1;
    assert(MENU_TITLE_TEMP_POLL_MS==5000);
    assert(!menuTitleGetTemperatureRaw(&raw));show("23:59:58");
    assert(updateTemp(0,0) && sensor_queries==1);
    assert(menuTitleGetTemperatureRaw(&raw) && raw==40*128);
    show("23:59:58  40.0\260C");
    for(u64 tick=0;tick<5000;tick+=16)assert(!updateTemp(tick,0));
    assert(sensor_queries==1);sensor_raw=41*128;
    assert(updateTemp(5000,0) && sensor_queries==2);
    dashCfg.temp_fahrenheit=1;show("23:59:58  105.8\260F");
    /* Convert the original sample before rounding, including fractional carries. */
    const int samples[]={0,128,37*128+64,40*128,100*128,40*128-1};
    const char *c[]={"23:59:58  0.0\260C","23:59:58  1.0\260C","23:59:58  37.5\260C","23:59:58  40.0\260C","23:59:58  100.0\260C","23:59:58  40.0\260C"};
    const char *f[]={"23:59:58  32.0\260F","23:59:58  33.8\260F","23:59:58  99.5\260F","23:59:58  104.0\260F","23:59:58  212.0\260F","23:59:58  104.0\260F"};
    for(unsigned i=0;i<sizeof(samples)/sizeof(samples[0]);i++) {
        sensor_raw=samples[i];updateTemp(6000+i,1);
        dashCfg.temp_fahrenheit=0;show(c[i]);dashCfg.temp_fahrenheit=1;show(f[i]);
    }
    /* Initial unsupported/error responses stay hidden with no repeated probes. */
    for(int kind=0;kind<2;kind++) {
        resetSensor();sensor_ok=kind;sensor_status=kind?0x80:0;
        updateTemp(10,0);assert(!menuTitleGetTemperatureRaw(&raw));show("23:59:58");
        updateTemp(5010,0);updateTemp(100010,0);assert(sensor_queries==1);
    }
    /* A transient failure after a supported read hides the value until retry. */
    resetSensor();updateTemp(10,0);sensor_ok=0;assert(updateTemp(5010,0));
    assert(!menuTitleGetTemperatureRaw(&raw));sensor_ok=1;updateTemp(10009,0);assert(sensor_queries==2);
    assert(updateTemp(10010,0) && sensor_queries==3);
    updateTemp(1,0);assert(sensor_queries==4); /* timer restart */
    sensor_raw=40*128;updateTemp(2,1);dashCfg.temp_fahrenheit=0;
    dashTheme.clock=DASH_CLOCK_STANDARD;dashTheme.clock_seconds=1;dashTheme.clock_date=DASH_DATE_SYSTEM;
    char time[48],date[24];assert(clockText(time,sizeof(time),date,sizeof(date)));
    assert(!strcmp(time,"23:59:58  40.0\260C") && !strcmp(date,"2026/10/25"));
    dashTheme.clock_seconds=0;dashTheme.clock_hours=DASH_HOURS_12;dashCfg.temp_fahrenheit=1;
    clockText(time,sizeof(time),date,sizeof(date));assert(!strcmp(time,"11:59 PM  104.0\260F"));
    /* Long 12-hour clock/temperature text respects the navigation strip. */
    dashTheme.clock_seconds=1;
    for(int mode=DASH_CLOCK_STANDARD;mode<=DASH_CLOCK_SYSTEM;mode++) {
        dashTheme.clock=mode;draws=0;dashDrawClock();assert(draws==2);
        assert(drawn[0].x>=420-0.01f && fabsf(drawn[0].x+dashTextWidth(drawn[0].text,drawn[0].scale)-DASH_SAFE_R)<0.01f);
        assert(strstr(drawn[0].text,"  104.0\260F"));
    }
    dashTheme.clock_pos=DASH_CLOCKPOS_FOOTER;
    float width=footerClockWidth(time,sizeof(time),date,sizeof(date));
    draws=0;drawFooterClock(400,time,date);assert(draws==2);
    assert(fabsf(drawn[1].x+dashTextWidth(drawn[1].text,drawn[1].scale)-DASH_SAFE_L-width)<0.01f);
    dashTheme.clock=DASH_CLOCK_OFF;draws=0;dashDrawClock();assert(!draws);
    assert(footerClockWidth(time,sizeof(time),date,sizeof(date))==0);
    clock_valid=0;clockText(time,sizeof(time),date,sizeof(date));assert(strstr(time,"--:--:--  104.0\260F") && !date[0]);
    puts("PASS: R3Z SCMD support/hidden failures, 5-second polling, cached precise C/F conversion, degree/spacing, header clearance and footer sizing");
}
'''
with tempfile.TemporaryDirectory(prefix="launchstation-temperature-") as folder:
    path = Path(folder)
    (path / "test.c").write_text(code)
    cc = shutil.which("gcc") or shutil.which("clang")
    assert cc, "Host compiler required"
    subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", str(path / "test.c"), "-lm", "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True, timeout=10)
