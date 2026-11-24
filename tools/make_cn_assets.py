import os, math, struct, zlib
from pathlib import Path
root=Path(r"D:\ProjectVSComunity\Pedals the capybara\Pedals the capybara")
out=root/'Demos/assets/cn_rig'
out.mkdir(parents=True, exist_ok=True)

def write_png(path,w,h,rgba):
    # rgba: bytes length w*h*4
    def chunk(t,d):
        c=struct.pack('>I',len(d))+t+d
        return c+struct.pack('>I',zlib.crc32(c[4:])&0xffffffff)
    raw=b''.join(b'\x00'+rgba[y*w*4:(y+1)*w*4] for y in range(h))
    data=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw,9))+chunk(b'IEND',b'')
    path.write_bytes(data)

def lerp(a,b,t): return a+(b-a)*t
def clamp(x,a,b): return a if x<a else b if x>b else x

def mk_face(name, top, bot, accent, label):
    w,h=512,640
    buf=bytearray(w*h*4)
    for y in range(h):
        t=y/(h-1)
        r=int(lerp(top[0],bot[0],t)); g=int(lerp(top[1],bot[1],t)); b=int(lerp(top[2],bot[2],t))
        for x in range(w):
            # vignette
            dx=(x-w/2)/(w/2); dy=(y-h/2)/(h/2)
            v=1.0-0.25*(dx*dx+dy*dy)
            rr=int(clamp(r*v,0,255)); gg=int(clamp(g*v,0,255)); bb=int(clamp(b*v,0,255))
            i=(y*w+x)*4; buf[i:i+4]=bytes([rr,gg,bb,255])
    # border
    for y in range(h):
        for x in range(w):
            if x<10 or x>=w-10 or y<10 or y>=h-10:
                i=(y*w+x)*4; buf[i:i+3]=bytes(accent)
    # simple label bar
    for y in range(40,90):
        for x in range(40,w-40):
            i=(y*w+x)*4; buf[i]=int(buf[i]*0.4); buf[i+1]=int(buf[i+1]*0.4); buf[i+2]=int(buf[i+2]*0.4)
    # text as simple blocky pixels (no font) - draw label initials
    # skip complex text; label.png not required by engine
    write_png(out/f'{name}_face.png',w,h,bytes(buf))

def mk_knob(name, accent):
    s=128; buf=bytearray(s*s*4)
    c=s/2; rad=46
    for y in range(s):
        for x in range(s):
            dx=x-c; dy=y-c; d=(dx*dx+dy*dy)**0.5
            i=(y*s+x)*4
            if d<=rad:
                t=d/rad
                v=int(lerp(70,25,t))
                buf[i:i+4]=bytes([v,v,v,255])
                if d>rad-4: buf[i:i+3]=bytes(accent)
            else:
                buf[i:i+4]=bytes([0,0,0,0])
    # pointer
    for r in range(10,40):
        x=int(c); y=int(c-r)
        for yy in range(y-2,y+3):
            for xx in range(x-2,x+3):
                if 0<=xx<s and 0<=yy<s:
                    i=(yy*s+xx)*4; buf[i:i+3]=bytes(accent); buf[i+3]=255
    write_png(out/f'{name}_knob.png',s,s,bytes(buf))

def mk_led(name, col):
    s=64; buf=bytearray(s*s*4); c=s/2; rad=18
    for y in range(s):
        for x in range(s):
            dx=x-c; dy=y-c; d=(dx*dx+dy*dy)**0.5; i=(y*s+x)*4
            if d<=rad:
                t=1-d/rad
                r=int(clamp(col[0]*t+40,0,255)); g=int(clamp(col[1]*t+40,0,255)); b=int(clamp(col[2]*t+40,0,255))
                buf[i:i+4]=bytes([r,g,b,255])
            else: buf[i:i+4]=bytes([0,0,0,0])
    write_png(out/f'{name}_led.png',s,s,bytes(buf))

def mk_foot(name):
    s=128; buf=bytearray(s*s*4); c=s/2
    for y in range(s):
        for x in range(s):
            dx=x-c; dy=y-c; d=(dx*dx+dy*dy)**0.5; i=(y*s+x)*4
            if d<=48:
                v=int(lerp(180,90,d/48))
                buf[i:i+4]=bytes([v,v,v,255])
                if d>44: buf[i:i+3]=bytes([220,220,220])
            elif d<=54:
                buf[i:i+4]=bytes([40,40,40,255])
            else:
                buf[i:i+4]=bytes([0,0,0,0])
    write_png(out/f'{name}_footswitch.png',s,s,bytes(buf))

kits=[
 ('comp',(40,48,58),(18,22,28),(180,210,255)),
 ('muff',(70,40,62),(28,16,26),(255,170,210)),
 ('mod',(38,64,86),(16,24,34),(140,220,255)),
 ('delay',(52,58,52),(20,24,20),(180,255,180)),
 ('amp',(62,66,78),(24,26,32),(220,220,255)),
]
for n,top,bot,acc in kits:
    mk_face(n,top,bot,acc,n)
    mk_knob(n,acc); mk_led(n,(255,80,60)); mk_foot(n)
print('ok')
