import numpy as np
from viv import entries
from fsh import blocks,image
from paths import ART as A
_m,_e,_d=entries(A+'/ingame/players/PLRDATA.VIV'); IDX={n.upper():(o,s) for n,o,s in _e}
def skin_features(pkg):
    o,s=IDX[pkg.upper()]; f=_d[o:o+s]; b=blocks(f); t,w,h,px,pal=image(f,b['face'])
    a=np.frombuffer(px,np.uint8).reshape(h,w); P=np.array(pal,float)
    reg=a[int(h*.35):int(h*.8), int(w*.3):int(w*.7)].ravel(); rgb=P[reg]
    lum=rgb@[0.299,0.587,0.114]
    keep=(lum>np.percentile(lum,25))&(lum<np.percentile(lum,90)); rgb=rgb[keep]; lum=lum[keep]
    return [*np.median(rgb,0),np.median(lum),np.percentile(lum,30),np.percentile(lum,70)]
