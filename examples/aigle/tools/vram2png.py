import sys
from PIL import Image
PAL=[(0,0,0),(255,0,0),(0,255,0),(255,255,0),(0,0,255),(255,0,255),(0,255,255),(255,255,255),
     (128,128,128),(255,128,128),(128,255,128),(255,255,128),(128,128,255),(255,128,255),(128,255,255),(255,128,0)]
d=open(sys.argv[1],'rb').read(); F,C=d[:8000],d[8000:]
im=Image.new('RGB',(320,200))
for o in range(8000):
    y,xb=divmod(o,40)
    for b in range(8):
        on=F[o]>>(7-b)&1
        im.putpixel((xb*8+b,y),PAL[(C[o]>>4) if on else (C[o]&15)])
im.resize((640,400),Image.NEAREST).save(sys.argv[2])
