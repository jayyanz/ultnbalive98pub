import struct
def blocks(f):
    cnt=struct.unpack_from('<I',f,8)[0]; out={}
    for i in range(cnt):
        nm=f[16+i*8:20+i*8].decode('latin1'); off=struct.unpack_from('<I',f,20+i*8)[0]
        out[nm]=off
    return out
def image(f,off):
    """returns (w,h,pixels rgb list) for 8-bit 0x7b images with attached palette"""
    t=f[off]; nxt=int.from_bytes(f[off+1:off+4],'little'); w,h=struct.unpack_from('<HH',f,off+4)
    if t!=0x7b: return None
    px=f[off+16:off+16+w*h]
    pal=None; p=off+nxt
    while nxt:
        pt=f[p]; n2=int.from_bytes(f[p+1:p+4],'little'); pw=struct.unpack_from('<H',f,p+4)[0]
        if pt in(0x22,0x24,0x2a,0x2d,0x23):
            raw=f[p+16:]
            if pt==0x24: pal=[(raw[i*3],raw[i*3+1],raw[i*3+2]) for i in range(pw)]
            elif pt==0x2a: pal=[(raw[i*4+2],raw[i*4+1],raw[i*4]) for i in range(pw)]
            elif pt==0x22: pal=[(raw[i*3]*4,raw[i*3+1]*4,raw[i*3+2]*4) for i in range(pw)]
            elif pt==0x2d:
                pal=[]
                for i in range(pw):
                    v=raw[i*2]|raw[i*2+1]<<8; pal.append((((v>>10)&31)*8,((v>>5)&31)*8,(v&31)*8))
            else: pal=[(raw[i*3+2],raw[i*3+1],raw[i*3]) for i in range(pw)]
            break
        if not n2: break
        p+=n2; nxt=n2
    return t,w,h,px,pal
