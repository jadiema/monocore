# draws a level (from levels.h, through a tiny C dump) with the trajectory found by solve
import sys, re, subprocess
from PIL import Image, ImageDraw
src = open('../levels.h').read()
def level_info():
    c = r'''#include <stdio.h>
#include "../physics.h"
#include "../levels.h"
int main(){for(int i=0;i<GV_LEVELS;i++){const gv_level*l=&gv_levels[i];
printf("L %d %d %d %d %d %d %d\n",i+1,l->lx,l->ly,l->tx,l->ty,l->n_pull,l->n_push);
for(int k=0;k<l->n_walls;k++)printf("W %d %d %d %d %d\n",l->walls[k].x1,l->walls[k].y1,l->walls[k].x2,l->walls[k].y2,l->walls[k].kind);
for(int k=0;k<l->n_nopod;k++)printf("R %d %d %d %d\n",l->nopod[k].x0,l->nopod[k].y0,l->nopod[k].x1,l->nopod[k].y1);}}'''
    open('/tmp/li.c','w').write(c)
    subprocess.check_call(['cc','-I.','-o','/tmp/li','/tmp/li.c'])
    out = subprocess.check_output(['/tmp/li']).decode().split('\n')
    lv = {}; cur=None
    for line in out:
        t=line.split()
        if not t: continue
        if t[0]=='L': cur=int(t[1]); lv[cur]={'l':list(map(int,t[2:])),'w':[],'r':[]}
        elif t[0]=='W': lv[cur]['w'].append(list(map(int,t[1:])))
        elif t[0]=='R': lv[cur]['r'].append(list(map(int,t[1:])))
    return lv
lv = level_info()
imgs=[]
for n in sorted(lv):
    S=2; im=Image.new('RGB',(320*S,240*S),(6,8,24)); d=ImageDraw.Draw(im)
    L=lv[n]
    for r in L['r']: d.rectangle([r[0]*S,r[1]*S,r[2]*S,r[3]*S],fill=(40,10,20))
    for w in L['w']: d.line([w[0]*S,w[1]*S,w[2]*S,w[3]*S],fill=(220,220,240) if w[4]==0 else (40,220,255),width=4*S)
    lx,ly,tx,ty=L['l'][:4]
    d.ellipse([(lx-5)*S,(ly-5)*S,(lx+5)*S,(ly+5)*S],outline=(255,255,0),width=2)
    d.ellipse([(tx-7)*S,(ty-7)*S,(tx+7)*S,(ty+7)*S],fill=(170,60,255))
    try:
        pts=[]
        for line in open('/tmp/lv%02d.txt'%n):
            t=line.split()
            if t[0]=='T': pts.append((int(t[1])*S,int(t[2])*S))
            if t[0]=='P':
                x,y,sg,pl=map(int,t[1:]); col=(60,140,255) if sg>0 else (255,90,40)
                d.ellipse([(x-6)*S,(y-6)*S,(x+6)*S,(y+6)*S],fill=col,outline=(255,255,255) if pl else (90,90,90),width=2)
        if len(pts)>1: d.line(pts,fill=(255,240,120),width=2)
    except FileNotFoundError: pass
    d.text((6,6),'%d'%n,fill=(255,255,255))
    imgs.append(im)
W=3; H=(len(imgs)+W-1)//W
sheet=Image.new('RGB',(W*320*2+ (W-1)*8, H*240*2+(H-1)*8),(60,60,60))
for i,im in enumerate(imgs): sheet.paste(im,((i%W)*(640+8),(i//W)*(480+8)))
sheet=sheet.resize((sheet.width//2,sheet.height//2))
sheet.save(sys.argv[1] if len(sys.argv)>1 else '/tmp/levels.png')
