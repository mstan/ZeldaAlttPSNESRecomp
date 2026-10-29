"""Verify world instrumentation survives regeneration of all width variants."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).with_name('apply_world_overrides.py')


class WorldOverridesTest(unittest.TestCase):
    def test_pit_native_entry_all_widths(self):
        with tempfile.TemporaryDirectory(prefix='alttp-world-pits-') as directory:
            root=Path(directory);target=root/'bank1b_test_v2.c'
            target.write_text('\n'.join(f'RecompReturn Overworld_GetPitDestination_M{m}X{x}(CpuState *cpu) {{ original(); return 0; }}' for m in (0,1) for x in (0,1)))
            def run(*args):
                return subprocess.run([sys.executable,str(SCRIPT),'--gen-dir',str(root),*args],capture_output=True,text=True)
            self.assertNotEqual(run('--check').returncode,0)
            self.assertEqual(run().returncode,0);first=target.read_bytes()
            self.assertEqual(first.count(b'ZeldaWorldPitBegin();'),4)
            self.assertEqual(run('--check').returncode,0)
            self.assertEqual(run().returncode,0);self.assertEqual(first,target.read_bytes())

    def test_dungeon_sampler_and_puzzle_native_returns(self):
        with tempfile.TemporaryDirectory(prefix='alttp-world-native-returns-') as directory:
            root=Path(directory);target=root/'bank06_test_v2.c'
            names=('bank_06_E886','HandlePegPuzzles','Overworld_AlterWeathervane','OpenGargoylesDomain','CreatePyramidHole')
            target.write_text('\n'.join(
                f'RecompReturn {name}_M{m}X{x}(CpuState *cpu) {{\n'
                '  if (yielding) { RecompStackPopYield(); return 1; }\n'
                '  { uint16 _ret_s = cpu->S;  /* RTL pop hardware return frame */\n'
                '    cpu->S += 3; RecompStackPop(); return 0; }\n}\n'
                for name in names for m in (0,1) for x in (0,1)))
            def run(*args):
                return subprocess.run([sys.executable,str(SCRIPT),'--gen-dir',str(root),*args],capture_output=True,text=True)
            self.assertNotEqual(run('--check').returncode,0)
            self.assertEqual(run().returncode,0);first=target.read_bytes()
            self.assertEqual(first.count(b'ZeldaWorldDungeonTileBegin();'),4)
            self.assertEqual(first.count(b'ZeldaWorldDungeonTileEnd();'),4)
            self.assertEqual(first.count(b'ZeldaWorldPuzzleEnd();'),16)
            self.assertEqual(run('--check').returncode,0)
            self.assertEqual(run().returncode,0);self.assertEqual(first,target.read_bytes())

    def test_secret_restores_at_native_return_not_yield(self):
        with tempfile.TemporaryDirectory(prefix='alttp-world-secrets-') as directory:
            root=Path(directory);target=root/'bank1b_test_v2.c'
            target.write_text('\n'.join(
                f'RecompReturn Overworld_RevealSecret_M{m}X{x}(CpuState *cpu) {{\n'
                '  if (yielding) { RecompStackPopYield(); return 1; }\n'
                '  { uint16 _ret_s = cpu->S;  /* RTS pop hardware return frame */\n'
                '    cpu->S += 2; RecompStackPop(); return 0; }\n}\n'
                for m in (0,1) for x in (0,1)))
            def run(*args):
                return subprocess.run([sys.executable,str(SCRIPT),'--gen-dir',str(root),*args],capture_output=True,text=True)
            self.assertNotEqual(run('--check').returncode,0)
            self.assertEqual(run().returncode,0);first=target.read_bytes()
            self.assertEqual(first.count(b'ZeldaWorldSecretBegin();'),4)
            self.assertEqual(first.count(b'ZeldaWorldSecretEnd();'),4)
            self.assertNotIn(b'ZeldaWorldSecretEnd(); }\n  if (yielding)',first)
            self.assertEqual(run('--check').returncode,0)
            self.assertEqual(run().returncode,0);self.assertEqual(first,target.read_bytes())

    def test_native_interaction_and_item_entries(self):
        with tempfile.TemporaryDirectory(prefix='alttp-world-interactions-') as directory:
            root = Path(directory)
            dialogue = root / 'bank05_test_v2.c'
            item = root / 'bank07_test_v2.c'
            dialogue.write_text('\n'.join(
                f'RecompReturn {name}_M{m}X{x}(CpuState *cpu) {{ return 0; }}'
                for name in ('Sprite_ShowSolicitedMessage','Sprite_ShowMessageOnContact',
                             'Sprite_ShowMessageUnconditional','Sprite_ShowMessageMinimal')
                for m in (0,1) for x in (0,1)))
            item.write_text('\n'.join(
                f'RecompReturn Link_ReceiveItem_M{m}X{x}(CpuState *cpu) {{ return 0; }}'
                for m in (0,1) for x in (0,1)))
            def run(*args):
                return subprocess.run([sys.executable,str(SCRIPT),'--gen-dir',str(root),*args],
                                      capture_output=True,text=True)
            self.assertNotEqual(run('--check').returncode,0)
            self.assertEqual(run().returncode,0)
            first = (dialogue.read_bytes(),item.read_bytes())
            self.assertEqual(first[0].count(b'ZeldaWorldInteractionBegin();'),16)
            self.assertEqual(first[1].count(b'ZeldaWorldItemReceiptBegin();'),4)
            self.assertEqual(run('--check').returncode,0)
            self.assertEqual(run().returncode,0)
            self.assertEqual(first,(dialogue.read_bytes(),item.read_bytes()))

    def test_all_widths_idempotence_and_check(self):
        with tempfile.TemporaryDirectory(prefix='alttp-world-hooks-') as directory:
            root = Path(directory)
            source = '\n'.join(
                f'RecompReturn {name}_M{m}X{x}(CpuState *cpu) {{\n  '+
                ('RecompStackPop();\n  ' if name in ('Sprite_Main','Ancilla_Main','LinkOam_Main') else '')+'return 0;\n}'
                for name in ('Sprite_ExecuteSingle', 'OverworldHandleTransitions', 'Sprite_Main', 'Sprite_HandleAbsorptionByPlayer', 'Bomb_CheckSpriteDamage', 'Medallion_CheckSpriteDamage', 'Ancilla_ExecuteOne', 'Ancilla_Main', 'Ancilla_CheckSpriteCollision', 'Overworld_UseEntrance', 'LinkOam_Main', 'Ancilla_CheckInitialTile_A', 'Overworld_OperateCameraScroll', 'Hookshot_ShouldIEvenBotherWithTiles', 'Unrelated')
                for m in (0, 1) for x in (0, 1)
            )
            target = root / 'bank06_test_v2.c'
            target.write_text(source)
            entrances = root / 'bank1b_test_v2.c'
            entrances.write_text('\n'.join(
                f'RecompReturn Overworld_UseEntrance_M{m}X{x}(CpuState *cpu) {{ return 0; }}'
                for m in (0, 1) for x in (0, 1)))
            player = root / 'bank0d_test_v2.c'
            player.write_text('\n'.join(
                f'RecompReturn LinkOam_Main_M{m}X{x}(CpuState *cpu) {{ '
                'RecompStackPopYield(); RecompStackPop(); return 0; }'
                for m in (0, 1) for x in (0, 1)))

            def run(*args):
                return subprocess.run([sys.executable, str(SCRIPT), '--gen-dir', str(root), *args],
                                      capture_output=True, text=True)

            self.assertNotEqual(run('--check').returncode, 0)
            self.assertEqual(run().returncode, 0)
            first = target.read_bytes()
            self.assertEqual(entrances.read_bytes().count(b'ZeldaWorldEntranceBegin();'), 4)
            self.assertEqual(player.read_bytes().count(b'ZeldaWorldLinkBegin();'), 4)
            self.assertEqual(player.read_bytes().count(b'ZeldaWorldLinkEnd();'), 8)
            self.assertEqual(first.count(b'/* ZELDA-WORLD-ACTOR-PROJECTION */'), 52)
            self.assertEqual(first.count(b'/* ZELDA-WORLD-SPRITES-END */'), 12)
            for hook in ('ZeldaWorldActorBegin', 'ZeldaWorldTransitionBegin', 'ZeldaWorldTransitionEnd', 'ZeldaWorldRewardBegin', 'ZeldaWorldBombDamageBegin', 'ZeldaWorldMedallionBegin', 'ZeldaWorldAncillaBegin','ZeldaWorldAncillasEnd','ZeldaWorldProjectileCollisionBegin','ZeldaWorldEntranceBegin','ZeldaWorldLinkBegin','ZeldaWorldLinkEnd','ZeldaWorldSpawnBegin','ZeldaWorldSpawnEnd','ZeldaWorldHookshotBounds'):
                self.assertEqual(first.count((hook + '();').encode()), 4)
            self.assertEqual(run('--check').returncode, 0)
            self.assertEqual(run().returncode, 0)
            self.assertEqual(first, target.read_bytes())
            self.assertIn(b'Unrelated_M0X0(CpuState *cpu) {\n  return 0;', first)

    def test_collision_result_at_hardware_return_only(self):
        with tempfile.TemporaryDirectory(prefix='alttp-world-results-') as directory:
            root = Path(directory)
            target = root / 'bank08_test_v2.c'
            names = ('Ancilla_CheckSpriteCollision', 'Ancilla_CheckSpriteCollision_Single',
                     'Ancilla_CheckBasicSpriteCollision', 'Ancilla_CheckBasicSpriteCollision_Single')
            target.write_text('\n'.join(
                f'RecompReturn {name}_M{m}X{x}(CpuState *cpu) {{\n'
                '  if (yielding) { RecompStackPopYield(); return 1; }\n'
                '  { uint16 _ret_s = cpu->S;  /* RTS pop hardware return frame */\n'
                '    cpu->S += 2; RecompStackPop(); return 0; }\n}\n'
                for name in names for m in (0, 1) for x in (0, 1)))
            def run(*args):
                return subprocess.run([sys.executable, str(SCRIPT), '--gen-dir', str(root), *args],
                                      capture_output=True, text=True)
            self.assertNotEqual(run('--check').returncode, 0)
            self.assertEqual(run().returncode, 0)
            first = target.read_bytes()
            self.assertEqual(first.count(b'ZeldaWorldProjectileCollisionEnd();'), 16)
            self.assertNotIn(b'ZeldaWorldProjectileCollisionEnd(); }\n  if (yielding)', first)
            self.assertEqual(run('--check').returncode, 0)
            self.assertEqual(run().returncode, 0)
            self.assertEqual(first, target.read_bytes())


if __name__ == '__main__':
    unittest.main()
