from pathlib import Path
import shutil, zipfile

root = Path(r"D:\ProjectVSComunity\Pedals the capybara\Pedals the capybara")
# Fix cn_comp log10 (Lua has no math.log10)
p = root / 'Demos/plugins/cn_comp/cn_comp.lua'
t = p.read_text(encoding='utf-8')
old = '''      local envDb=20*math.log10(env+1e-12)
      local thrDb=20*math.log10(thr+1e-12)
      local outDb=thrDb + (envDb-thrDb)/ratio
      gain=db2lin(outDb-envDb)'''
new = '''      local envDb=20*math.log(env+1e-12)/math.log(10)
      local thrDb=20*math.log(thr+1e-12)/math.log(10)
      local outDb=thrDb + (envDb-thrDb)/ratio
      gain=db2lin(outDb-envDb)'''
if old in t:
    p.write_text(t.replace(old, new), encoding='utf-8')
    print('fixed log10')
else:
    print('log10 pattern not found or already fixed')

# Rebuild .pctp from source dirs and pack
pack_dir = root / 'Demos/plugins/ComfortablyNumbRig'
if pack_dir.exists():
    shutil.rmtree(pack_dir)
pack_dir.mkdir(parents=True)
for d in ['cn_comp','cn_muff','cn_mod','cn_delay','cn_amp']:
    pdir = root / 'Demos/plugins' / d
    pctp = root / 'Demos' / f'{d}.pctp'
    if pctp.exists():
        pctp.unlink()
    with zipfile.ZipFile(pctp, 'w', zipfile.ZIP_DEFLATED) as z:
        for f in sorted(pdir.rglob('*')):
            if f.is_file():
                z.write(f, f.relative_to(pdir).as_posix())
    shutil.copy2(pctp, pack_dir / pctp.name)
(pack_dir / 'pack.ini').write_text(
    "NAME=Comfortably Numb Rig (Inspired)\n"
    "DESCRIPTION=Rig inspirado para solo de Comfortably Numb: comp, sustain/fuzz, modulação, delay e amp. DSP original; não afiliado a Pink Floyd/BOSS/EHX/Hiwatt.\n"
    "AUTHOR=Pedals the Capybara\n"
    "VERSION=1.0\n",
    encoding='utf-8'
)
pp = root / 'Packs/ComfortablyNumbRig.pp'
if pp.exists():
    pp.unlink()
with zipfile.ZipFile(pp, 'w', zipfile.ZIP_DEFLATED) as z:
    for f in sorted(pack_dir.rglob('*')):
        if f.is_file():
            z.write(f, f.relative_to(pack_dir).as_posix())
shutil.copy2(pp, root / 'Packs/ComfortablyNumbSolo.pp')
print('ok')
