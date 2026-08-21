#!/usr/bin/env python3
"""Generate and validate ODPAR's living system/function integration graph."""
from __future__ import annotations
import argparse, json, pathlib, re, shutil, subprocess, sys
from collections import defaultdict

ROOT=pathlib.Path(__file__).resolve().parents[1]
MANIFEST=ROOT/'docs/architecture/SYSTEM_GRAPH_MANIFEST.json'
OUT_JSON=ROOT/'docs/SYSTEM_GRAPH.json'; OUT_MD=ROOT/'docs/SYSTEM_GRAPH.md'
OUT_DOT=ROOT/'docs/SYSTEM_GRAPH.dot'; OUT_SVG=ROOT/'docs/SYSTEM_GRAPH.svg'; FUNC_DOT=ROOT/'docs/FUNCTION_GRAPH.dot'


def text(rel:str)->str: return (ROOT/rel).read_text(encoding='utf-8')
def die(msg:str)->None: print('graph-check failed: '+msg,file=sys.stderr); raise SystemExit(1)

def c_defs(src:str)->set[str]:
    # Deliberately conservative: function definitions at line start ending in an opening brace.
    rx=re.compile(r'(?m)^[ \t]*(?:static[ \t]+)?(?:const[ \t]+)?(?:[A-Za-z_][A-Za-z0-9_]*[ \t\*]+)+([A-Za-z_][A-Za-z0-9_]*)[ \t]*\([^;{}]*\)[ \t]*\{')
    return {m.group(1) for m in rx.finditer(src) if m.group(1) not in {'if','for','while','switch'}}

def main()->int:
    ap=argparse.ArgumentParser(); ap.add_argument('--check',action='store_true'); args=ap.parse_args()
    manifest=json.loads(MANIFEST.read_text())
    systems={s['id']:s for s in manifest['systems']}
    errors=[]
    for sid,s in systems.items():
        for f in s['files']:
            if not (ROOT/f).exists(): errors.append(f'{sid}: missing file {f}')
    for e in manifest['required_connections']:
        if e['from'] not in systems or e['to'] not in systems: errors.append(f"bad edge {e['from']} -> {e['to']}"); continue
        blob='\n'.join(text(f) for f in e['files'] if (ROOT/f).exists())
        for token in e.get('all',[]):
            if token not in blob: errors.append(f"{e['from']} -> {e['to']}: missing evidence {token}")
        any_tokens=e.get('any',[])
        if any_tokens and not any(t in blob for t in any_tokens): errors.append(f"{e['from']} -> {e['to']}: missing any evidence {any_tokens}")

    c_files=sorted((ROOT/'engine/src').glob('*.c'))+sorted((ROOT/'engine/vendor').glob('*.c'))
    rel=lambda p:str(p.relative_to(ROOT)).replace('\\','/')
    file_defs={rel(p):c_defs(p.read_text(encoding='utf-8')) for p in c_files}
    owner={}
    duplicates=defaultdict(list)
    for f,defs in file_defs.items():
        for fn in defs:
            if fn in owner: duplicates[fn].extend([owner[fn],f])
            else: owner[fn]=f
    calls=[]; module_edges=defaultdict(int)
    for p in c_files:
        f=rel(p); src=p.read_text(encoding='utf-8')
        for callee in set(re.findall(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\(',src)):
            dst=owner.get(callee)
            if dst and dst!=f:
                calls.append((f,dst,callee)); module_edges[(f,dst)]+=1

    header=text('engine/include/odpar_game.h'); exports=text('engine/odpar_territorial_domain.exports.map'); dart=text('app/flutter/lib/src/native/odg_bindings.dart')
    public=set(re.findall(r'\b(odg_[A-Za-z0-9_]+)\s*\(',header))
    exported=set(re.findall(r'^\s*(odg_[A-Za-z0-9_]+);\s*$',exports,re.M))
    dart_lookups=set(re.findall(r"lookupFunction\s*<.*?>\s*\(\s*'([^']+)'",dart,re.S))
    if public!=exported:
        errors.append('public C/export map mismatch missing='+','.join(sorted(public-exported))+' extra='+','.join(sorted(exported-public)))
    bad_dart=sorted(dart_lookups-public)
    if bad_dart: errors.append('Dart looks up non-public symbols: '+','.join(bad_dart))

    # Map files to declared systems; a source file should have an architectural owner.
    file_systems=defaultdict(list)
    for sid,s in systems.items():
        for f in s['files']: file_systems[f].append(sid)
    unowned=[f for f in file_defs if f.startswith('engine/src/') and f!='engine/src/android_bridge.c' and not file_systems.get(f)]
    if unowned: errors.append('unowned C modules: '+','.join(unowned))

    result={
      'schema_version':1,'manifest_schema':manifest['schema_version'],
      'summary':{'systems':len(systems),'c_modules':len(file_defs),'functions':len(owner),'cross_module_calls':len(calls),'public_c_symbols':len(public),'dart_lookups':len(dart_lookups),'required_connections':len(manifest['required_connections']),'gaps':len(errors)},
      'systems':list(systems.values()),'required_connections':manifest['required_connections'],
      'module_edges':[{'from':a,'to':b,'call_count':n} for (a,b),n in sorted(module_edges.items())],
      'functions_by_file':{f:sorted(v) for f,v in sorted(file_defs.items())},
      'gaps':errors
    }
    OUT_JSON.write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n')

    md=['# ODPAR — Living System Graph','',
        '> Generated from the current source tree. Do not hand-edit this file; edit `docs/architecture/SYSTEM_GRAPH_MANIFEST.json` or the code and run `make graph`.','',
        f"**{len(systems)} systems · {len(file_defs)} C modules · {len(owner)} functions · {len(calls)} cross-module calls · {len(public)} public C symbols · {len(errors)} detected gaps.**",'',
        '## Rule for future changes','',
        'A new species/item/tool/terrain/weather/interaction is not complete when it merely compiles. Its identity, capabilities, registry references, simulation authority, persistence (when stateful), renderer/host exposure (when observable), validation and tests must form an intentional path in this graph. Numeric ABI IDs are append-only and must never be recycled.','',
        '## Systems','']
    for sid,s in systems.items(): md.append(f"- **{sid}** — {s['label']} — `"+'`, `'.join(s['files'])+'`')
    md += ['','## Required integration edges','']
    for e in manifest['required_connections']:
        evidence=e.get('all',[])+e.get('any',[])
        md.append(f"- **{e['from']} → {e['to']}** — {e['why']} — evidence: `"+'`, `'.join(evidence)+'`')
    md += ['','## Detected gaps','']
    md += [f'- ❌ {x}' for x in errors] if errors else ['- ✅ None. All declared integration invariants have evidence.']
    md += ['','## Machine-readable companions','', '- `SYSTEM_GRAPH.json`: systems, generated function inventory, module edges and gaps.', '- `SYSTEM_GRAPH.dot`: system/integration diagram.', '- `FUNCTION_GRAPH.dot`: generated C module/function call edges.', '- `SYSTEM_GRAPH.svg`: rendered diagram when Graphviz is available.','']
    OUT_MD.write_text('\n'.join(md))

    colors={'flutter_host':'#dfefff','wasm_host':'#fff2d9','spine':'#eeeeee','registry':'#e8ffe8','surface':'#e8f7ff','simulation':'#ffe7e7'}
    dot=['digraph ODPAR {','  rankdir=LR;','  graph [fontname="sans-serif", bgcolor="white"];','  node [shape=box, style="rounded,filled", fillcolor="#f7f7f7", fontname="sans-serif"];','  edge [fontname="sans-serif", fontsize=9];']
    for sid,s in systems.items(): dot.append(f'  "{sid}" [label="{sid}\\n{s["label"]}", fillcolor="{colors.get(sid,"#f7f7f7")}"];')
    for e in manifest['required_connections']: dot.append(f'  "{e["from"]}" -> "{e["to"]}" [label="{e["why"].replace(chr(34),chr(39))}"];')
    dot.append('}'); OUT_DOT.write_text('\n'.join(dot)+'\n')

    fd=['digraph ODPAR_Functions {','  rankdir=LR;','  node [shape=box, fontsize=8];']
    for (a,b),n in sorted(module_edges.items()): fd.append(f'  "{a}" -> "{b}" [label="{n}"];')
    fd.append('}'); FUNC_DOT.write_text('\n'.join(fd)+'\n')
    if shutil.which('dot'):
        subprocess.run(['dot','-Tsvg',str(OUT_DOT),'-o',str(OUT_SVG)],check=True)
    if errors:
        for x in errors: print('GAP:',x,file=sys.stderr)
        if args.check: return 1
    print(f"SYSTEM GRAPH OK systems={len(systems)} modules={len(file_defs)} functions={len(owner)} calls={len(calls)} public={len(public)} gaps={len(errors)}")
    return 0

if __name__=='__main__': raise SystemExit(main())
