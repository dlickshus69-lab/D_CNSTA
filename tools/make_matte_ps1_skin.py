# usage: python3 make_matte_ps1_skin.py <PS1 skin Crash.pak> <output Crash.pak> [gloss 0-255, default 20]
"""Make a matte PS1 Crash: in the PS1 skin's Crash.pak (every member stored raw), flatten the cloth normal map (BC5,
128/128) and set the cloth's gloss to `GLOSS` with no metal or emissive (BC1 composites). Same sizes, patched in place."""
import struct, sys, shutil
src, dst, GLOSS = sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 20
shutil.copyfile(src, dst)
f = open(dst, 'r+b')
h = f.read(56)
count = struct.unpack('<I', h[12:16])[0]
names_start, = struct.unpack('<Q', h[40:48]); nsize, = struct.unpack('<I', h[48:52])
f.seek(names_start); nb = f.read(nsize)
f.seek(56 + count * 4); recs = f.read(count * 16)
index = {}
for i in range(count):
    p, = struct.unpack('<I', nb[i*4:i*4+4]); e = nb.index(b'\0', p); e2 = nb.index(b'\0', e + 1)
    index[nb[e+1:e2].decode('latin1')] = i
def member(name):
    i = index[name]
    s, hi, length, cmd = struct.unpack('<IIII', recs[i*16:i*16+16])
    assert cmd == 0xffffffff, name + ' is compressed'
    return s + ((hi & 1) << 32), length
def pixels(name):
    at, length = member(name)
    f.seek(at); head = f.read(16 + 8 * 16)
    assert head[:4] == b'\x01ZGI', name
    secs = [struct.unpack('<4I', head[16+i*16:32+i*16]) for i in range(8)]
    data = [s for s in secs if s[2] or s[3]][-1]
    assert data[2] + data[3] <= length
    return at + data[2], data[3]
def fill(name, block):
    at, size = pixels(name)
    assert size % len(block) == 0, (name, size)
    f.seek(at); f.write(block * (size // len(block)))
    print('patched', name, size, 'bytes')
def bc1(r, g, b):
    c = ((r * 31 // 255) << 11) | ((g * 63 // 255) << 5) | (b * 31 // 255)
    return struct.pack('<HH', c, c) + b'\0\0\0\0'
fill('textures/NormalMap,textures@!actors!Playable!Crash!CrashCloth_N`tga,111.igz', bytes([128, 128, 0, 0, 0, 0, 0, 0] * 2))
fill('textures/Composite3,textures@!actors!Playable!Crash!CrashCloth_{B,G,M}`tga,111.igz', bc1(0, GLOSS, 0))
fill('textures/Composite3,textures@!{default_e`png,actors!Playable!Crash!CrashCloth_{G,M}`tga},111.igz', bc1(0, GLOSS, 0))
f.close()
