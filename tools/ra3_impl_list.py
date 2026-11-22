import re,sys,glob,os
root=sys.argv[1]
impl=set()
for f in glob.glob(os.path.join(root,'*.cpp'))+glob.glob(os.path.join(root,'*.h')):
    defs={}
    for line in open(f,errors='ignore'):
        m=re.match(r'\s*#define\s+(\w+)\s+"([^"]+)"',line)
        if m: defs[m.group(1)]=m.group(2); continue
        for m in re.finditer(r'(IMP_STD|IMP_C|IMP_ID|registerData|registerFn)\s*\(\s*([^,]+?)\s*,\s*([^,\)]+)',line):
            kind,d,n=m.groups()
            d=d.strip()
            if d.startswith('"'): dll=d.strip('"')
            elif d in defs: dll=defs[d]
            else: continue
            n=n.strip()
            name=n.strip('"')
            impl.add((dll.lower(),name))
for d,n in sorted(impl): print(f"{d}!{n}")
