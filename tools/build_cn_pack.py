from pathlib import Path
import shutil, zipfile

root = Path(r"D:\ProjectVSComunity\Pedals the capybara\Pedals the capybara")
boss_assets = root / 'Demos/assets/boss'
cn_dir = root / 'Demos/plugins/comfortably_numb_solo'
cn_dir.mkdir(parents=True, exist_ok=True)
(cn_dir / 'assets').mkdir(exist_ok=True)
for f in ['face.png','knob.png','led.png','footswitch.png','label.png']:
    src = boss_assets / 'comfortably_numb_solo' / f
    if src.exists():
        shutil.copy2(src, cn_dir / 'assets' / f)
cn_ptc = (
    "NAME=Comfortably Numb Solo (Inspired)\n"
    "DESCRIPTION=Inspired lead tone preset for Pink Floyd's Comfortably Numb final solo. Amp-style block with gain/EQ/presence and short lead delay. Original preset, not affiliated.\n"
    "AUTHOR=Pedals the Capybara\n"
    "VERSION=1.0\n"
    "SCRIPT=comfortably_numb_solo.lua\n"
    "ICON=assets/face.png\n"
)
(cn_dir / 'plugin.ptc').write_text(cn_ptc, encoding='utf-8')
cn_pctp = root / 'Demos/comfortably_numb_solo.pctp'
if cn_pctp.exists():
    cn_pctp.unlink()
with zipfile.ZipFile(cn_pctp, 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('plugin.ptc', cn_ptc)
    z.writestr('comfortably_numb_solo.lua', (cn_dir / 'comfortably_numb_solo.lua').read_text(encoding='utf-8'))
    for f in ['face.png','knob.png','led.png','footswitch.png','label.png']:
        p = cn_dir / 'assets' / f
        if p.exists():
            z.write(p, f'assets/{f}')
cn_pp = root / 'Packs/ComfortablyNumbSolo.pp'
if cn_pp.exists():
    cn_pp.unlink()
with zipfile.ZipFile(cn_pp, 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('pack.ini', 'NAME=Pink Floyd Comfortably Numb\nDESCRIPTION=Inspired lead tone preset for the final solo of Comfortably Numb. Original preset/model; not affiliated with Pink Floyd.\nAUTHOR=Pedals the Capybara\nVERSION=1.0\n')
    z.write(cn_pctp, cn_pctp.name)
print('ok', cn_pp)
