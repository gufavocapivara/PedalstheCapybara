from pathlib import Path
import shutil, zipfile

root = Path(r"D:\ProjectVSComunity\Pedals the capybara\Pedals the capybara")
boss_assets = root / 'Demos/assets/boss'
plugins = {
 'boss_md2': {
   'name':'BOSS MD-2 Mega Distortion (Inspired)',
   'desc':'Inspired by BOSS MD-2 controls: Level, Tone/Bottom, Dist, Gain Boost. Original DSP model, not schematic-accurate or affiliated with BOSS/Roland.',
   'script':'boss_md2.lua','asset':'md2'
 },
 'boss_ds1': {
   'name':'BOSS DS-1 Distortion (Inspired)',
   'desc':'Inspired by BOSS DS-1 controls: Level, Tone, Dist. Original DSP model, not schematic-accurate or affiliated with BOSS/Roland.',
   'script':'boss_ds1.lua','asset':'ds1'
 },
 'boss_sd1': {
   'name':'BOSS SD-1 Super OverDrive (Inspired)',
   'desc':'Inspired by BOSS SD-1 controls: Level, Tone, Drive. Original DSP model, not schematic-accurate or affiliated with BOSS/Roland.',
   'script':'boss_sd1.lua','asset':'sd1'
 },
 'boss_bd2': {
   'name':'BOSS BD-2 Blues Driver (Inspired)',
   'desc':'Inspired by BOSS BD-2 controls: Level, Tone, Gain. Original DSP model, not schematic-accurate or affiliated with BOSS/Roland.',
   'script':'boss_bd2.lua','asset':'bd2'
 },
 'boss_mt2': {
   'name':'BOSS MT-2 Metal Zone (Inspired)',
   'desc':'Inspired by BOSS MT-2 controls: Level, Dist, High, Low, Mid, Mid Freq. Original DSP model, not schematic-accurate or affiliated with BOSS/Roland.',
   'script':'boss_mt2.lua','asset':'mt2'
 },
}
pack_dir = root / 'Demos/plugins/BossInspiredPack'
if pack_dir.exists():
    shutil.rmtree(pack_dir)
pack_dir.mkdir(parents=True)

for folder, meta in plugins.items():
    pdir = root / 'Demos/plugins' / folder
    adir = pdir / 'assets'
    adir.mkdir(parents=True, exist_ok=True)
    src = boss_assets / meta['asset']
    for f in ['face.png','knob.png','led.png','footswitch.png','label.png']:
        shutil.copy2(src / f, adir / f)
    ptc = (
        f"NAME={meta['name']}\n"
        f"DESCRIPTION={meta['desc']}\n"
        "AUTHOR=Pedals the Capybara\n"
        "VERSION=1.0\n"
        f"SCRIPT={meta['script']}\n"
        "ICON=assets/face.png\n"
    )
    (pdir / 'plugin.ptc').write_text(ptc, encoding='utf-8')
    pctp = root / 'Demos' / f"{folder}.pctp"
    if pctp.exists():
        pctp.unlink()
    with zipfile.ZipFile(pctp, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('plugin.ptc', ptc)
        z.writestr(meta['script'], (pdir / meta['script']).read_text(encoding='utf-8'))
        for f in ['face.png','knob.png','led.png','footswitch.png','label.png']:
            z.write(adir / f, f'assets/{f}')
    shutil.copy2(pctp, pack_dir / pctp.name)

(pack_dir / 'pack.ini').write_text(
    "NAME=BOSS Inspired Pack\n"
    "DESCRIPTION=Inspired models based on documented BOSS pedal controls (MD-2, DS-1, SD-1, BD-2, MT-2). Original DSP approximations; not affiliated with BOSS/Roland.\n"
    "AUTHOR=Pedals the Capybara\n"
    "VERSION=1.0\n",
    encoding='utf-8'
)

pp = root / 'Packs/BossInspired.pp'
if pp.exists():
    pp.unlink()
with zipfile.ZipFile(pp, 'w', zipfile.ZIP_DEFLATED) as z:
    for path in sorted(pack_dir.rglob('*')):
        if path.is_file():
            z.write(path, path.relative_to(pack_dir).as_posix())
print('ok', pp)
