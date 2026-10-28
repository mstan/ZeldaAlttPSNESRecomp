#!/usr/bin/env python3
"""Keep the world actor projection at every generated AOT entry variant.

The interpreted entry is covered by the game's PC observer. No gameplay
instructions or return-frame contracts are replaced by this instrumentation.
"""
import argparse,re
from pathlib import Path
PATTERN=re.compile(r'RecompReturn (Sprite_ExecuteSingle|OverworldHandleTransitions|Sprite_Main|Sprite_HandleAbsorptionByPlayer|Bomb_CheckSpriteDamage|Ancilla_ExecuteOne|Ancilla_Main)_M[01]X[01]\(CpuState \*cpu\) \{')
MARK='/* ZELDA-WORLD-ACTOR-PROJECTION */'
CALLS={'Sprite_ExecuteSingle':'ZeldaWorldActorBegin','OverworldHandleTransitions':'ZeldaWorldTransitionBegin','Sprite_Main':'ZeldaWorldTransitionEnd','Sprite_HandleAbsorptionByPlayer':'ZeldaWorldRewardBegin','Bomb_CheckSpriteDamage':'ZeldaWorldBombDamageBegin','Ancilla_ExecuteOne':'ZeldaWorldAncillaBegin'}
ENDS={'Sprite_Main':'ZeldaWorldSpritesEnd','Ancilla_Main':'ZeldaWorldAncillasEnd'}
END_MARK='/* ZELDA-WORLD-SPRITES-END */'
POP=re.compile(r'\bRecompStackPop(?:Yield)?\(\);')
TOKENS=re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/|[{}]',re.S)

def body_end(text,start):
 depth=1
 for token in TOKENS.finditer(text,start):
  if token.group()=='{':depth+=1
  elif token.group()=='}':
   depth-=1
   if not depth:return token.start()
 raise ValueError('unterminated generated function')
def main():
 p=argparse.ArgumentParser();p.add_argument('--gen-dir',type=Path,default=Path('src/gen'));p.add_argument('--check',action='store_true');a=p.parse_args()
 count=changed=0
 for path in sorted(a.gen_dir.glob('bank0[268]*_v2.c')):
  text=path.read_text(encoding='utf-8');matches=list(PATTERN.finditer(text));count+=len(matches)
  for match in reversed(matches):
   end=body_end(text,match.end());body=text[match.end():end]
   if match.group(1) in ENDS:
    pops=len(POP.findall(body));ends=body.count(END_MARK)
    if ends!=pops:
     if ends:raise SystemExit(f'partial world sprite cleanup: {path}')
     if a.check:raise SystemExit(f'missing world sprite cleanup: {path}')
     hook=ENDS[match.group(1)]
     body=POP.sub(lambda m:END_MARK+f'\n  {{ extern void {hook}(void); {hook}(); }}\n  '+m.group(),body);changed+=pops
   if match.group(1) in CALLS and not body.lstrip().startswith(MARK):
    if a.check:raise SystemExit(f'missing world actor projection: {path}')
    hook=CALLS[match.group(1)]
    body='\n  '+MARK+f'\n  extern void {hook}(void);\n  {hook}();'+body;changed+=1
   text=text[:match.end()]+body+text[end:]
  if matches and not a.check:path.write_text(text,encoding='utf-8',newline='')
 print(f'world actor projection: {count} AOT entries, {changed} inserted; interpreter observer retained')
if __name__=='__main__':main()
