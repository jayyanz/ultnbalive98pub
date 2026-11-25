import struct,sys
def read(path):
    d=open(path,'rb').read()
    ver,y,m,dd,n,hl,rl=struct.unpack('<BBBBIHH',d[:12])
    fields=[];off=1;p=32
    while d[p]!=0x0D:
        name=d[p:p+11].split(b'\0')[0].decode('latin1');t=chr(d[p+11]);ln=d[p+16];dc=d[p+17]
        fields.append((name,t,ln,dc,off));off+=ln;p+=32
    recs=[]
    for i in range(n):
        r=d[hl+i*rl:hl+(i+1)*rl]
        recs.append((r[0:1],{f[0]:r[f[4]:f[4]+f[2]] for f in fields}))
    return dict(ver=ver,date=(1900+y,m,dd),n=n,hl=hl,rl=rl,fields=fields,recs=recs,raw=d)
if __name__=='__main__':
    for p in sys.argv[1:]:
        x=read(p);print('==',p,'ver %02x'%x['ver'],x['date'],'recs',x['n'],'reclen',x['rl'])
        for f in x['fields']: print('  %-11s %s %3d.%d @%d'%f)
