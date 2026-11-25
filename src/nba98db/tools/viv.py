import struct,sys
def entries(path):
    d=open(path,'rb').read()
    magic=d[:4]
    tot=struct.unpack('<I',d[4:8])[0]; n,hdr=struct.unpack('>II',d[8:16])
    out=[];p=16
    for i in range(n):
        off,sz=struct.unpack('>II',d[p:p+8]);p+=8
        e=d.index(b'\0',p);nm=d[p:e].decode('latin1');p=e+1
        out.append((nm,off,sz))
    return magic,out,d
if __name__=='__main__':
    for f in sys.argv[1:]:
        m,e,_=entries(f);print(f.split('/')[-1],m,len(e),[x[0] for x in e[:8]],'...',[x[0] for x in e[-3:]])
