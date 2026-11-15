"""Execute the production BGM worker, optional sidecar and independent sound buses."""
from dash_sound_harness import run

run(r'''
reset_sound();sidecar_present=1;short_read=7;dashSoundInit();assert(!opens && !bgm_uploads);
dashCfg.bgm_vol=61;finish_all();assert(soundAlive() && bgm_loaded && bgm_channel==23 && gain[23]==61);
assert(opens==1 && bgm_uploads==1 && allocations==1 && plays==1 && !handles);
assert(!strcmp(last_path,"mass0:/BOOT/BGM.ADP"));
assert(((u8 *)bgm_data)[17]==6 && ((u8 *)bgm_data)[sidecar_size-15]==3);
for(int i=0;i<10;i++) { dashSoundMaintain(); }
assert(opens==1 && plays==1);
dashCfg.bgm=0;dashBgmStop();assert(gain[23]==0);
dashCfg.bgm=1;dashBgmStart();assert(gain[23]==61 && plays==1 && opens==1);
dashSoundSuspend();assert(gain[23]==0);dashSoundApplyVolume();dashBgmStart();assert(gain[23]==61);
dashCfg.sound_theme=1;dashSoundReloadTheme();assert(active);finish_all();
assert(allocations==1 && bgm_uploads==2 && plays==2 && bgm_channel==23);
reset_sound();dashCfg.sound=0;sidecar_present=1;dashSoundMaintain();finish_all();
assert(soundAlive() && bgm_channel==23 && gain[0]==0 && gain[23]==40);
reset_sound();dashCfg.bgm=0;dashSoundMaintain();finish_all();assert(soundAlive() && !opens);
dashCfg.bgm=1;stack_loaded=0;dashBgmStart();assert(!opens && !active && !bgm_tried);
stack_loaded=1;sidecar_present=1;dashSoundMaintain();assert(active);finish_all();assert(opens==1 && plays==1);
for(int failure=0;failure<7;failure++) {
    reset_sound();sidecar_present=1;
    if(failure==0) sidecar_size=31;
    if(failure==1) sidecar[0]='!';
    if(failure==2) sidecar[5]=2;
    if(failure==3) sidecar[8]=sidecar[9]=0;
    if(failure==4) sidecar[6]=3;
    if(failure==5) read_error=1;
    if(failure==6) close_error=1;
    dashSoundInit();finish_all();assert(soundAlive() && !bgm_loaded && !allocations && !handles);
    for(int i=0;i<10;i++) { dashSoundMaintain(); }
assert(opens==1 && !active);
}
reset_sound();sidecar_present=1;dashSoundInit();dashCfg.bgm=0;finish_all();assert(!plays);
puts("PASS: asynchronous optional BGM, fragmented reads, loop flags, live volume, theme reload, SFX independence, missing/invalid sidecars and close failures");
''')
