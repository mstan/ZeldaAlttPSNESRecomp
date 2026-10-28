"""Verify world instrumentation survives regeneration of all width variants."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).with_name('apply_world_overrides.py')


class WorldOverridesTest(unittest.TestCase):
    def test_all_widths_idempotence_and_check(self):
        with tempfile.TemporaryDirectory(prefix='alttp-world-hooks-') as directory:
            root = Path(directory)
            source = '\n'.join(
                f'RecompReturn {name}_M{m}X{x}(CpuState *cpu) {{\n  '+
                ('RecompStackPop();\n  ' if name in ('Sprite_Main','Ancilla_Main','LinkOam_Main') else '')+'return 0;\n}'
                for name in ('Sprite_ExecuteSingle', 'OverworldHandleTransitions', 'Sprite_Main', 'Sprite_HandleAbsorptionByPlayer', 'Bomb_CheckSpriteDamage', 'Ancilla_ExecuteOne', 'Ancilla_Main', 'Ancilla_CheckSpriteCollision', 'Overworld_UseEntrance', 'LinkOam_Main', 'Unrelated')
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
            self.assertEqual(first.count(b'/* ZELDA-WORLD-ACTOR-PROJECTION */'), 36)
            self.assertEqual(first.count(b'/* ZELDA-WORLD-SPRITES-END */'), 12)
            for hook in ('ZeldaWorldActorBegin', 'ZeldaWorldTransitionBegin', 'ZeldaWorldTransitionEnd', 'ZeldaWorldRewardBegin', 'ZeldaWorldBombDamageBegin', 'ZeldaWorldAncillaBegin','ZeldaWorldAncillasEnd','ZeldaWorldProjectileCollisionBegin','ZeldaWorldEntranceBegin','ZeldaWorldLinkBegin','ZeldaWorldLinkEnd'):
                self.assertEqual(first.count((hook + '();').encode()), 4)
            self.assertEqual(run('--check').returncode, 0)
            self.assertEqual(run().returncode, 0)
            self.assertEqual(first, target.read_bytes())
            self.assertIn(b'Unrelated_M0X0(CpuState *cpu) {\n  return 0;', first)


if __name__ == '__main__':
    unittest.main()
