#!/usr/bin/env python3
# Regenerates src/asm_pie.h and test/host/fixtures/pie_vectors.h from GNU as.
#   XTENSA_S3_BIN=<dir with xtensa-esp32s3-elf-as and -objcopy> ./gen_pie.py [instructions.txt]
# instructions.txt: one sample line per instruction (q, a and immediate
# operands are told apart by their first letter). For each operand bit the
# script assembles two lines that differ in that bit alone and records
# which instruction bit changed; immediates' ranges and steps come from
# trying every value from -4096 to 4096. Then random operand sets are
# encoded both ways and must agree.
import subprocess, tempfile, os, random, sys, json
T=os.environ.get('XTENSA_S3_BIN', os.path.expanduser('~/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20240906/xtensa-esp-elf/bin'))+'/'
AS=T+'xtensa-esp32s3-elf-as'; OC=T+'xtensa-esp32s3-elf-objcopy'
HERE=os.path.dirname(os.path.abspath(__file__)); ROOT=os.path.join(HERE,'..','..')
W=tempfile.mkdtemp()
cache={}
def asm_many(lines):
    """assembles lines; returns list of 24-bit codes or None per line"""
    out=[None]*len(lines); todo=[i for i,l in enumerate(lines) if l not in cache]
    if todo:
        src=os.path.join(W,'x.s'); obj=os.path.join(W,'x.o'); bn=os.path.join(W,'x.bin')
        # assemble each in its own .org-free stream: one per line, errors tell which fail
        open(src,'w').write('.text\n'+''.join(lines[i]+'\n' for i in todo))
        r=subprocess.run([AS,src,'-o',obj],capture_output=True,text=True)
        if r.returncode!=0:
            bad=set()
            for e in r.stderr.splitlines():
                p=e.split(':')
                if len(p)>2 and p[1].isdigit(): bad.add(int(p[1])-2)
            good=[i for k,i in enumerate(todo) if k not in bad]
            for k,i in enumerate(todo):
                if k in bad: cache[lines[i]]=None
            if good and len(good)<len(todo):
                for i,c in zip(good,asm_many([lines[i] for i in good])): cache[lines[i]]=c
            return [cache[l] for l in lines]
        subprocess.run([OC,'-O','binary','-j','.text',obj,bn],check=True)
        b=open(bn,'rb').read(); assert len(b)==3*len(todo),(lines,len(b))
        for k,i in enumerate(todo): cache[lines[i]]=b[3*k]|b[3*k+1]<<8|b[3*k+2]<<16
    return [cache[l] for l in lines]
def asm(l): return asm_many([l])[0]

def kinds(sample):
    ops=[o.strip() for o in sample.split(None,1)[1].split(',')] if ' ' in sample else []
    return [('q' if o[0]=='q' else 'a' if o[0]=='a' else 'i') for o in ops]

def line(m,vals): return m+(' '+','.join(vals) if vals else '')

def probe_imm(m,ks,idx):
    def fill(v):
        vals=['q0' if k=='q' else 'a0' if k=='a' else '0' for k in ks]; vals[idx]=str(v); return line(m,vals)
    cand=list(range(-4096,4097))
    ok=asm_many([fill(v) for v in cand])
    acc=[v for v,c in zip(cand,ok) if c is not None]
    assert acc,(m,idx)
    mn,mx=min(acc),max(acc)
    pos=[v for v in acc if v>0]; step=min(pos) if pos else 1
    assert all((v-mn)%step==0 for v in acc) and len(acc)==(mx-mn)//step+1,(m,idx,mn,mx,step)
    return mn,mx,step

def derive(sample):
    m=sample.split()[0]; ks=kinds(sample)
    rng=[]
    for i,k in enumerate(ks):
        if k=='q': rng.append((0,7,1))
        elif k=='a': rng.append((0,15,1))
        else: rng.append(probe_imm(m,ks,i))
    def enc(vals): return asm(line(m,[('q%d' if k=='q' else 'a%d' if k=='a' else '%d')%v for k,v in zip(ks,vals)]))
    def others(i,avoid):
        # values for the other operands, distinct registers, none in avoid
        vals=[]; used=set(avoid)
        for j,k in enumerate(ks):
            if j==i: vals.append(None); continue
            if k=='i':
                r=rng[j]; vals.append(0 if r[0]<=0<=r[1] else r[0]); continue
            v=next(x for x in range(rng[j][0],rng[j][1]+1) if x not in used); used.add(v); vals.append(v)
        return vals
    fields=[]; bases=[]
    for i,(mn,mx,st) in enumerate(rng):
        bias=0 if mn<=0<=mx else mn
        nb=0
        while bias+st*(1<<nb)<=mx: nb+=1
        if mn<0: nb+=1
        pos=[]
        for k in range(nb):
            found=None
            for x in range(1<<nb):
                if not (x>>k&1): continue
                xs=[x,x^(1<<k)]
                vv=[]
                for y in xs:
                    v=bias+st*(y if not (mn<0 and y>>(nb-1)&1) else y-(1<<nb))
                    vv.append(v)
                if not all(mn<=v<=mx for v in vv): continue
                o=others(i,vv if ks[i]!='i' else [])
                c=[]
                for v in vv:
                    t=list(o); t[i]=v; c.append(enc(t))
                if None in c: continue
                d=c[0]^c[1]; assert bin(d).count('1')==1,(sample,i,k,vv,hex(d)); found=d.bit_length()-1; bases.append((o,i,vv[1],c[1])); break
            assert found is not None,(sample,i,k)
            pos.append(found)
        fields.append(dict(kind=ks[i],min=mn,max=mx,step=st,bias=bias,pos=pos))
    e=dict(name=m,fields=fields,base=0)
    # base opcode: any accepted encoding minus its fields
    o,i,v,c=bases[0] if bases else (others(-1,[]),None,None,enc([]))
    t=list(o)
    if i is not None: t[i]=v
    e['base']=c^encode(e,t)
    return e

def encode(e,vals):
    c=e['base']
    for f,v in zip(e['fields'],vals):
        x=((v-f['bias'])//f['step'])&((1<<len(f['pos']))-1)
        for k,p in enumerate(f['pos']):
            if x>>k&1: c|=1<<p
    return c

samples=[l.strip() for l in open(sys.argv[1] if len(sys.argv)>1 else os.path.join(HERE,'instructions.txt')) if l.strip() and not l.startswith('#')]
table=[derive(s) for s in samples]
# verify against as: random operands
random.seed(1); vec=[]; rejected=[]
for e in table:
    for n in range(40):
        vals=[]
        for f in e['fields']:
            vals.append(f['min']+f['step']*random.randint(0,(f['max']-f['min'])//f['step']))
        l=line(e['name'],[('q%d' if f['kind']=='q' else 'a%d' if f['kind']=='a' else '%d')%v for f,v in zip(e['fields'],vals)])
        c=asm(l)
        if c is None:
            rejected.append(l); continue
        vec.append((l,c))
        assert c==encode(e,vals),(l,hex(c),hex(encode(e,vals)))
for e in table: print('%-22s %06x %s'%(e['name'],e['base'],' '.join('%s[%d..%d/%d]@%s'%(f['kind'],f['min'],f['max'],f['step'],f['pos']) for f in e['fields'])))
print(len(table),'instructions,',len(vec),'vectors verified;',len(rejected),'rejected by as:',sorted(set(r.split()[0] for r in rejected)))

HEAD="#pragma once\n#ifndef __ASM_PIE__\n#define __ASM_PIE__\n// The ESP32-S3's PIE (Processor Instruction Extensions): 128-bit q0-q7\n// registers, every mnemonic starting with ee. Generated from GNU as\n// (xtensa-esp32s3-elf-as, esp-14.2.0): each operand bit's position was\n// found by assembling encodings that differ in that bit alone, and the\n// immediates' ranges and steps by trying every value; 2208 random\n// operand combinations then encoded exactly as as does. Fields are often\n// split (q registers in bits 15, 20, 21; an immediate's sign in bit 22),\n// hence a list of bit positions per operand. All are 3 bytes.\n#include <stdint.h>\n\nenum\n{\n    PIE_Q = 0, // q0-q7\n    PIE_A = 1, // a0-a15\n    PIE_IMM = 2\n};\n\nstruct PieField\n{\n    uint8_t kind;\n    uint8_t nbits;\n    uint8_t step; // the immediate is a multiple of it, stored divided\n    int16_t min, max;\n    uint8_t pos[8]; // bit k of the field goes to instruction bit pos[k]\n};\n\nstruct PieOp\n{\n    const char *name;\n    uint32_t base;\n    uint8_t nops;\n    bool distinctQ; // as refuses the same q register twice (ee.vzip...)\n    PieField f[3];\n};\n\nstatic const PieOp pieOps[] = {"
TAIL='};\n\nstatic const PieOp *findPieOp(const char *name)\n{\n    for (unsigned i = 0; i < sizeof(pieOps) / sizeof(pieOps[0]); i++)\n        if (strcmp(pieOps[i].name, name) == 0)\n            return &pieOps[i];\n    return NULL;\n}\n\n// The instruction for already checked operand values.\nstatic uint32_t pieEncode(const PieOp *op, const int32_t *values)\n{\n    uint32_t code = op->base;\n    for (int i = 0; i < op->nops; i++)\n    {\n        const PieField &f = op->f[i];\n        uint32_t x = (uint32_t)(values[i] / f.step) & ((1u << f.nbits) - 1);\n        for (int k = 0; k < f.nbits; k++)\n            if (x >> k & 1)\n                code |= 1u << f.pos[k];\n    }\n    return code;\n}\n\n#endif\n'
rows=[]
for e in table:
    assert all(f['bias']==0 for f in e['fields'])
    fs=[]
    for f in e['fields']:
        pos=f['pos']+[0]*(8-len(f['pos']))
        fs.append('{%s, %d, %d, %d, %d, {%s}}'%({'q':'PIE_Q','a':'PIE_A','i':'PIE_IMM'}[f['kind']],len(f['pos']),f['step'],f['min'],f['max'],', '.join(map(str,pos))))
    rows.append('    {"%s", 0x%06x, %d, %s, {%s}},'%(e['name'],e['base'],len(e['fields']),'true' if 'zip' in e['name'] else 'false',', '.join(fs)))
open(os.path.join(ROOT,'src','asm_pie.h'),'w').write(HEAD.replace('2208 random','%d random'%len(vec))+'\n'+'\n'.join(rows)+'\n'+TAIL)
import collections
per=collections.defaultdict(list)
for l,c in vec: per[l.split()[0]].append((c,l))
sel=[x for k in per for x in per[k][:8]]
out=['// ee.* lines and their encodings by GNU as (xtensa-esp32s3-elf-as,','// esp-14.2.0): 8 random operand sets per instruction of src/asm_pie.h.',
     '// Generated by tools/pie/gen_pie.py; see the "PIE instructions" test in test_parser.cpp.',
     'static const struct { uint32_t code; const char *line; } pieVectors[] = {']
out+=['    {0x%06x, "%s"},'%(c,l) for c,l in sel]+['};']
open(os.path.join(ROOT,'test','host','fixtures','pie_vectors.h'),'w').write('\n'.join(out)+'\n')
