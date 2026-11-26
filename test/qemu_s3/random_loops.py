# Random loops in the shape src/vectorize.cpp takes: a script and its C
# reference per seed (see run.py). ./random_loops.py SEED out.sc out.c
import random, sys
def gen(seed):
    R=random.Random(seed)
    W=R.choice([40,64,77,128]); H=6
    arrays=[('ga',2),('gb',2),('gc',1),('gd',2)]  # 2D: [H, W]; 1D: [W*2]
    def dims(a): return 2 if dict(arrays)[a]==2 else 1
    dst=R.choice(['ga','gc','gd'])
    temps=[]
    rows={'r0':R.randint(0,H-1),'r1':R.randint(1,H-2)}
    def rowexpr():
        k=R.randint(0,3)
        if k==0: return str(R.randint(0,H-1))
        if k==1: return R.choice(list(rows))
        if k==2: return 'r1 + 1'
        return 'r1 - 1'
    dstrow=rowexpr() if dims(dst)==2 else None
    srcs=[]
    for n in range(R.randint(1,3)):
        a=R.choice([x for x,_ in arrays if x!=dst])
        srcs.append((a,rowexpr() if dims(a)==2 else None))
    def ref(depth):
        a,row=R.choice(srcs)
        k=R.randint(-3,3)
        idx='i' if k==0 else ('i + %d'%k if k>0 else 'i - %d'%-k)
        return '%s[%s, %s]'%(a,row,idx) if row is not None else '%s[%s]'%(a,idx)
    def expr(d):
        c=R.random()
        if d>=3 or c<0.3:
            t=R.random()
            if t<0.6: return ref(d)
            if t<0.8 and temps: return R.choice(temps)
            return str(R.randint(0,9))
        op=R.choice(['+','+','-','&','|','==','!=','<','>','<=','>='])
        return '(%s %s %s)'%(expr(d+1),op,expr(d+1))
    body=[]
    for t in range(R.randint(0,2)):
        name='t%d'%t; e=expr(0); temps.append(name); body.append(('uint8_t %s = %s;'%(name,e)))
    # the destination may be read at its own index
    e=expr(0)
    if R.random()<0.4:
        own='%s[%s, i]'%(dst,dstrow) if dims(dst)==2 else '%s[i]'%dst
        e='(%s %s %s)'%(e,R.choice(['+','&','|','-']),own)
    store=('%s[%s, i]'%(dst,dstrow) if dims(dst)==2 else '%s[i]'%dst)+' = '+e+';'
    lo=R.choice([3,4,5,9,20]); hi=R.choice([lo+1,lo+7,lo+15,lo+16,lo+17,lo+33,W-3,W-4,W-5])
    hi=max(lo+1,min(hi,W-3))
    fill='''  for (int h = 0; h < %d; h++) {
    ga[h] = (h * 37 + %d) %% 256;
    gb[h] = (h * 53 + %d) %% 7;
    gd[h] = (h * 11 + %d) %% 3;
  }
  for (int h = 0; h < %d; h++) {
    gc[h] = (h * 29 + %d) %% 256;
  }
'''%(H*W,R.randint(0,99),R.randint(0,99),R.randint(0,99),2*W,R.randint(0,99))
    sc='''external void emit(int v);
uint8_t ga[%d, %d];
uint8_t gb[%d, %d];
uint8_t gc[%d];
uint8_t gd[%d, %d];
void f(int r0, int r1)
{
  for (int i = %d; i < %d; i++) {
%s  }
}
int main()
{
%s  f(%d, %d);
  for (int h = 0; h < %d; h++) {
    emit(ga[h] + gd[h] * 256);
  }
  for (int h = 0; h < %d; h++) {
    emit(gc[h]);
  }
}
'''%(H,W,H,W,2*W,H,W,lo,hi,''.join('    %s\n'%b for b in body+[store]),fill,rows['r0'],rows['r1'],H*W,2*W)
    import re
    def toC(x):
        x=re.sub(r'(\w+)\[([^\[\],]+), ([^\[\]]+)\]',lambda m:'%s[%s][%s]'%(m.group(1),m.group(2),m.group(3)),x)
        return x
    cbody=''.join('    %s\n'%toC(b) for b in body+[store])
    c='''#include <stdio.h>
#include <stdint.h>
void emit(int v){printf("EMIT %%d\\n",v);}
uint8_t ga[%d][%d], gb[%d][%d], gc[%d], gd[%d][%d];
void f(int r0,int r1){ for(int i=%d;i<%d;i++){
%s}}
int main(){ uint8_t *pa=&ga[0][0],*pb=&gb[0][0],*pd=&gd[0][0];
'''%(H,W,H,W,2*W,H,W,lo,hi,cbody)
    c+=fill.replace('ga[h]','pa[h]').replace('gb[h]','pb[h]').replace('gd[h]','pd[h]').replace('int h','int h')
    c+='''  f(%d,%d);
  for(int h=0;h<%d;h++) emit(pa[h]+pd[h]*256);
  for(int h=0;h<%d;h++) emit(gc[h]);
  printf("DONE\\n"); return 0;}
'''%(rows['r0'],rows['r1'],H*W,2*W)
    return sc,c
if __name__=='__main__':
    sc,c=gen(int(sys.argv[1])); open(sys.argv[2],'w').write(sc); open(sys.argv[3],'w').write(c)
