"""Check/render every serialized upstream setting family against reachable UI.

python scripts/audit_r3z_settings.py [--upstream PATH] [--write]
The optional checkout must be the pinned upstream revision; it detects inventory
drift rather than silently treating a newer upstream as the audited baseline.
"""
from pathlib import Path
import argparse
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = '06b06d000072d358c93d98104d64d2f081e47ba7'

# key: SETTING field, reachable control, runtime consumer. These are semantic
# mappings reviewed against the upstream serializer and downstream call paths.
ROWS = {
 'APP_GameID': ('app_gameid','S_GAMEID','src/main_actions.c'),
 'CDROM_Disable_GameID': ('cdrom_disable_gameid','S_GAMEID_DISC','src/main_actions.c; src/neutrino_iso.c; src/popstarter.c'),
 'CNF_Path': ('CNF_Path','S_CNF_PATH','src/config.c'),
 'Disable_EMBER': ('disable_ember','S_DISABLE_EMBER','src/main_actions.c'),
 'Disable_NEUTRINO': ('disable_neutrino','S_DISABLE_NEUTRINO','src/neutrino_iso.c'),
 'Disable_POPSTARTER': ('disable_popstarter','S_DISABLE_POPSTARTER','src/popstarter.c'),
 'FB_NoIcons': ('FB_NoIcons','S_FB_ICONS','src/filer_browser.c; src/filer_actions.c'),
 'FONT_FILE': ('font_file','S_FONT_FILE','src/draw_text.c; src/dash_font.c'),
 'GUI_Swap_Keys': ('swapKeys','S_CONFIRM','src/pad.c; src/pad_ds34.c; src/dash_ui.c'),
 'Hide_Hdd': ('Hide_Hdd','S_HIDE_HDD','src/filer.c; src/dash_devices.c'),
 'Hide_MCMMCE': ('Hide_MCMMCE','S_HIDE_MC','src/filer.c; src/dash_devices.c'),
 'Init_Delay': ('Init_Delay','S_INIT_DELAY','src/main_boot.c; src/main_menu.c'),
 'KBDMAP_FILE': ('kbdmap_file','S_KBDMAP_FILE','src/init.c'),
 'LANG_FILE': ('lang_file','S_LANG_FILE','src/lang.c'),
 'LK_%s_E1': ('LK_Path','ST_LKEY','src/main_boot.c; src/main_menu.c; src/dash_home.c'),
 'LK_%s_Title': ('LK_Title','ST_LKEY','src/main_menu.c; src/dash_home.c'),
 'LK_auto_Timer': ('timeout','S_TIMEOUT','src/main_menu.c; src/dash_main.c'),
 'Menu_Hide_Paths': ('Hide_Paths','S_HIDE_PATHS','src/main_menu.c'),
 'Menu_Show_Titles': ('Show_Titles','S_SHOW_TITLES','src/main_menu.c'),
 'Menu_Frame': ('Menu_Frame','S_MENU_FRAME','src/draw.c'),
 'Popup_Opaque': ('Popup_Opaque','S_POPUP_OPAQUE','src/gui_colors.c; classic popup renderers'),
 'NET_HOSTwrite': ('HOSTwrite','S_HOSTWRITE','src/init.c'),
 'NEUTRINO_ELF': ('neutrino_file','S_NEUTRINO_PATH','src/neutrino_iso.c'),
 'POPSTARTER_ELF': ('popstarter_file','S_POPS_PATH','src/popstarter_path.c'),
 'PSU_DateNames': ('PSU_DateNames','S_PSU_DATE','src/filer_copy.c'),
 'PSU_HugeNames': ('PSU_HugeNames','S_PSU_HUGE','src/filer_copy.c'),
 'PSU_NoOverwrite': ('PSU_NoOverwrite','S_PSU_NOOVER','src/filer_copy.c'),
 'PathPad[%02d]': ('PathPad','F_BOOKMARK','src/dash_browser.c; src/filer_browser.c'),
 'PathPad_Lock': ('PathPad_Lock','S_PATHPAD_LOCK','src/dash_browser.c; src/dash_settings.c'),
 'REBOOT_IOP_ELFLOAD': ('reboot_iop_elf_load','S_REBOOT_ELF','src/main_actions.c; src/popstarter.c'),
 'Screen_Offset_X': ('screen_x','S_SCREEN_X','src/draw_gs.c'),
 'Screen_Offset_Y': ('screen_y','S_SCREEN_Y','src/draw_gs.c'),
 'TV_mode': ('TV_mode','S_TVMODE','src/draw_gs.c'),
 'USBKBD_FILE': ('usbkbd_file','S_USBKBD_FILE','src/init.c'),
 'USBKBD_USED': ('usbkbd_used','S_USBKBD','src/init.c'),
 'VirtualKeyboardLayout': ('virtual_keyboard_layout','S_VKEY','src/dash_widgets.c; src/virtual_keyboard.c'),
 'language': ('language','S_LANGUAGE','src/lang.c'),
}
for i, colour in enumerate(['BACKGR','FRAME','SELECT','TEXT','GRAPH1','GRAPH2','GRAPH3','GRAPH4'],1):
    ROWS[f'GUI_Col_{i}_ABGR'] = ('color',f'S_C_COL0 + COLOR_{colour}','src/draw.c; classic renderers')
for name in ['Misc','Misc_PS2Disc','Misc_FileBrowser','Misc_PS2Browser','Misc_PS2Net',
             'Misc_PS2PowerOff','Misc_HddManager','Misc_Exploit_Installer','Misc_TextEditor',
             'Misc_Configure','Misc_ShowFont','Misc_Debug_Info','Misc_About_uLE',
             'Misc_Show_Build_Info','Misc_OSDSYS','Misc_Reboot_IOP']:
    ROWS[name] = (name,'CNF compatibility name','src/config.c; src/main_actions.c; src/dash_main.c')

def saved_keys(source):
    return set(re.findall(r'"([^"\n]+) = %', source))

def check(upstream=None):
    settings=(ROOT/'src/dash_settings.c').read_text()
    config=(ROOT/'src/config.c').read_text()
    colours=(ROOT/'src/gui_colors.c').read_text()
    arrays=dict(re.findall(r'static const SetItem (\w+)\[\] = \{(.*?)\n\};',settings,re.S))
    pages=dict(re.findall(r'\[(P_\w+)\] = PAGE\([^\n]*, (\w+)\),',settings))
    reached=set();pending=['P_ROOT'];ui=''
    while pending:
        page=pending.pop()
        if page in reached: continue
        reached.add(page);items=arrays[pages[page]];ui+=items
        pending+=re.findall(r'\{ST_PAGE, (P_\w+),',items)
    assert set(ROWS) <= saved_keys(config)|saved_keys(colours)
    for key,(field,control,consumer) in ROWS.items():
        assert re.search(r'\b'+field+r'\b',config+colours), (key,'missing persistence field')
        if control.startswith('S_'): assert control in ui,(key,'unreachable control')
        for path in re.findall(r'src/[\w_]+\.c',consumer): assert (ROOT/path).exists(),path
    # Optional full comparison against the original serializer, not a name sample.
    if upstream:
        sha=subprocess.check_output(['git','-C',str(upstream),'rev-parse','HEAD'],text=True).strip()
        assert sha==UPSTREAM,(sha,UPSTREAM)
        original=(upstream/'src/config.c').read_text(errors='replace')
        a=original.index('// Save LAUNCHELF.CNF');b=original.index('int loadConfig',a)
        inventory=saved_keys(original[a:b])|saved_keys((upstream/'src/gui_colors.c').read_text(errors='replace'))
        assert inventory==set(ROWS), {'unmapped':sorted(inventory-set(ROWS)),'obsolete':sorted(set(ROWS)-inventory)}
    return pages,arrays

def report():
    lines=['# Complete upstream configuration mapping','',f'Upstream snapshot: `{UPSTREAM}`.',
           '',f'All **{len(ROWS)} serialized key families** from upstream config.c and gui_colors.c are mapped below. '+
           'Indexed launch-key, title and bookmark entries are counted as families. CNF_version is schema metadata.',
           '', '| CNF key / family | Field | Reachable editor | Runtime consumer |', '|---|---|---|---|']
    for key,(field,control,consumer) in sorted(ROWS.items()):
        lines.append(f'| `{key}` | `{field}` | `{control}` | {consumer} |')
    lines += ['', 'Settings control IDs identify the actual entries in `src/dash_settings.c`. '+
              'The validation script traverses registered pages from the Settings root, so orphaned handler code does not count as an editor.',
              '', 'Classic-only frame, opacity, colours, launch labels and file-picker icon settings are now under '+
              '**Settings > System > Classic screen settings**. They retain their classic consumers and do not change dashboard themes.',
              '', 'Launch keys: Auto is in Automatic start & boot; Circle through START and optional Left/Right are in Launch keys; '+
              'ESR/OSDSYS paths are in Disc & image loaders. SELECT is reserved for Configure, as in the existing runtime. '+
              'LK_Flag is derived state, refreshed when a target is chosen/cleared and when CNF loads.',
              '', 'MISC prefix/command names are persisted compatibility strings, not missing upstream GUI options. '+
              'They remain editable in LAUNCHELF.CNF. Built-in commands themselves remain available through Settings, Tools and Files.',
              '', 'PathPad entries are edited through File Browser > Bookmarks. Theme controls remain in dashboard theme settings.',
              '', 'Historical read aliases (`app_gameid`, `Enable_GameID`, `cdrom_disable_gameid`, `Disable_Disc_GameID`) '+
              'map to their canonical saved keys. Menu_Pages and Menu_Title are ignored legacy inputs, not supported missing controls.',
              '', 'Downstream-only additions such as EMBER_DIR and NET_LegacyDrivers have existing Settings controls and are outside the pinned upstream key inventory.',
              '', 'Run `python scripts/audit_r3z_settings.py --upstream <pinned-checkout> --write` to regenerate and check the full upstream inventory. '+
              'Without --upstream it validates the committed mapping against current persistence and page reachability.',
              '', 'This is a source/configuration mapping and automated reachability check, not proof of each device on hardware.','']
    return '\n'.join(lines)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream',type=Path)
    parser.add_argument('--write',action='store_true')
    args=parser.parse_args();check(args.upstream)
    output=ROOT/'docs/R3Z-CONFIG-PARITY.md'
    if args.write: output.write_text(report(),encoding='utf-8')
    else: assert output.read_text(encoding='utf-8')==report(),'Configuration report is stale'
    print(f'PASS: {len(ROWS)} upstream persisted key families mapped, controls reachable, report current')
