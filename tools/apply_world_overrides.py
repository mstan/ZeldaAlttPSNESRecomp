#!/usr/bin/env python3
"""Keep the world actor projection at every generated AOT entry variant.

The interpreted entry is covered by the game's PC observer. No gameplay
instructions or return-frame contracts are replaced by this instrumentation.
"""
import argparse,re
from pathlib import Path
PATTERN=re.compile(r'RecompReturn (Sprite_ExecuteSingle|OverworldHandleTransitions|Sprite_Main)_M[01]X[01]\(CpuState \*cpu\) \{')
MARK='/* ZELDA-WORLD-ACTOR-PROJECTION */'
CALLS={'Sprite_ExecuteSingle':'ZeldaWorldActorBegin','OverworldHandleTransitions':'ZeldaWorldTransitionBegin','Sprite_Main':'ZeldaWorldTransitionEnd'}
def main():
 p=argparse.ArgumentParser();p.add_argument('--gen-dir',type=Path,default=Path('src/gen'));p.add_argument('--check',action='store_true');a=p.parse_args()
 count=changed=0
 for path in sorted(a.gen_dir.glob('bank0[26]*_v2.c')):
  text=path.read_text(encoding='utf-8');matches=list(PATTERN.finditer(text));count+=len(matches)
  for match in reversed(matches):
   if text[match.end():].lstrip().startswith(MARK):continue
   if a.check:raise SystemExit(f'missing world actor projection: {path}')
   hook=CALLS[match.group(1)]
   text=text[:match.end()]+'\n  '+MARK+f'\n  extern void {hook}(void);\n  {hook}();'+text[match.end():];changed+=1
  if matches and not a.check:path.write_text(text,encoding='utf-8',newline='')
 print(f'world actor projection: {count} AOT entries, {changed} inserted; interpreter observer retained')
if __name__=='__main__':main()
