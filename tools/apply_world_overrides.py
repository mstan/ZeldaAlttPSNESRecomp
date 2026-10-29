#!/usr/bin/env python3
"""Keep the world actor projection at every generated AOT entry variant.

The interpreted entry is covered by the game's PC observer. No gameplay
instructions or return-frame contracts are replaced by this instrumentation.
"""
import argparse,re
from pathlib import Path
MARK='/* ZELDA-WORLD-ACTOR-PROJECTION */'
CALLS={'Sprite_ExecuteSingle':'ZeldaWorldActorBegin','OverworldHandleTransitions':'ZeldaWorldTransitionBegin','Sprite_Main':'ZeldaWorldTransitionEnd','Sprite_HandleAbsorptionByPlayer':'ZeldaWorldRewardBegin','Bomb_CheckSpriteDamage':'ZeldaWorldBombDamageBegin','Ancilla_ExecuteOne':'ZeldaWorldAncillaBegin','Ancilla_CheckSpriteCollision':'ZeldaWorldProjectileCollisionBegin'}
CALLS['Overworld_UseEntrance']='ZeldaWorldEntranceBegin'
CALLS['LinkOam_Main']='ZeldaWorldLinkBegin'
CALLS['Ancilla_CheckInitialTile_A']='ZeldaWorldSpawnBegin'
CALLS['Overworld_OperateCameraScroll']='ZeldaWorldSpawnEnd'
CALLS['Hookshot_ShouldIEvenBotherWithTiles']='ZeldaWorldHookshotBounds'
CALLS['Medallion_CheckSpriteDamage']='ZeldaWorldMedallionBegin'
CALLS['Link_ReceiveItem']='ZeldaWorldItemReceiptBegin'
CALLS['Overworld_RevealSecret']='ZeldaWorldSecretBegin'
CALLS['bank_06_E886']='ZeldaWorldDungeonTileBegin'
CALLS.update(HandlePegPuzzles='ZeldaWorldPegBegin',Overworld_AlterWeathervane='ZeldaWorldWeathervaneBegin',
             OpenGargoylesDomain='ZeldaWorldGargoyleBegin',CreatePyramidHole='ZeldaWorldPyramidBegin')
for name in ('Sprite_ShowSolicitedMessage','Sprite_ShowMessageOnContact',
             'Sprite_ShowMessageUnconditional','Sprite_ShowMessageMinimal'):
 CALLS[name]='ZeldaWorldInteractionBegin'
ENDS={'Sprite_Main':'ZeldaWorldSpritesEnd','Ancilla_Main':'ZeldaWorldAncillasEnd','LinkOam_Main':'ZeldaWorldLinkEnd'}
END_MARK='/* ZELDA-WORLD-SPRITES-END */'
POP=re.compile(r'\bRecompStackPop(?:Yield)?\(\);')
TOKENS=re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/|[{}]',re.S)
RESULT_MARK='/* ZELDA-WORLD-WEAPON-RESULT */'
RTS=re.compile(r'(?m)^[ ]*\{ uint16 _ret_s = cpu->S;  /\* RT[SL] pop hardware return frame \*/')
RESULTS={'Ancilla_CheckSpriteCollision','Ancilla_CheckSpriteCollision_Single',
         'Ancilla_CheckBasicSpriteCollision','Ancilla_CheckBasicSpriteCollision_Single'}
RETURN_HOOKS={name:('/* ZELDA-WORLD-WEAPON-RESULT */','ZeldaWorldProjectileCollisionEnd') for name in RESULTS}
RETURN_HOOKS['Overworld_RevealSecret']=('/* ZELDA-WORLD-SECRET-RESULT */','ZeldaWorldSecretEnd')
RETURN_HOOKS['bank_06_E886']=('/* ZELDA-WORLD-DUNGEON-TILE-RESULT */','ZeldaWorldDungeonTileEnd')
for name in ('HandlePegPuzzles','Overworld_AlterWeathervane','OpenGargoylesDomain','CreatePyramidHole'):
 RETURN_HOOKS[name]=('/* ZELDA-WORLD-PUZZLE-RESULT */','ZeldaWorldPuzzleEnd')
PATTERN=re.compile(r'RecompReturn ('+'|'.join(re.escape(n) for n in sorted(set(CALLS)|set(ENDS)|set(RETURN_HOOKS)))+r')_M[01]X[01]\(CpuState \*cpu\) \{')

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
 paths=set(a.gen_dir.glob('bank0[256789de]*_v2.c'))|set(a.gen_dir.glob('bank1b*_v2.c'))
 for path in sorted(paths):
  text=path.read_text(encoding='utf-8');matches=list(PATTERN.finditer(text));count+=len(matches)
  for match in reversed(matches):
   end=body_end(text,match.end());body=text[match.end():end]
   if match.group(1) in RETURN_HOOKS:
    result_mark,result_hook=RETURN_HOOKS[match.group(1)]
    returns=len(RTS.findall(body));results=body.count(result_mark)
    if results!=returns:
     if results:raise SystemExit(f'partial world weapon result: {path}')
     if a.check:raise SystemExit(f'missing world weapon result: {path}')
     body=RTS.sub(lambda m:result_mark+f'\n  {{ extern void {result_hook}(void); {result_hook}(); }}\n'+m.group(),body);changed+=returns
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
