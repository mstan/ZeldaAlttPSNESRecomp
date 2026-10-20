"""Keep the adaptive cull hook tied to the intended branch in either timing form."""
import importlib.util
from pathlib import Path

import pytest

TOOL = Path(__file__).resolve().parents[1] / 'tools/apply_widescreen_overrides.py'
SPEC = importlib.util.spec_from_file_location('widescreen_overrides', TOOL)
tool = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tool)


@pytest.mark.parametrize('charge', ['8', '((g_memsel && (cpu->PB & 0x80)) ? 6 : 8)'])
def test_cull_hooks_all_variants_and_remains_idempotent(charge):
    source = ''.join(
        f'RecompReturn {tool.FUNCTION}_{mode}(CpuState *cpu) {{\n'
        f'    if (cpu->_flag_C == 1) {{ cpu->cycles += 1; cpu->master_cycles += {charge}; goto L_E476_M1X0; }}\n'
        '}\n' for mode in ('M0X0', 'M0X1', 'M1X0', 'M1X1'))
    result, changes = tool.inject(source)
    assert changes == 4
    assert result.count('ZeldaAdjustSpritePrepHorizontalCull(cpu);') == 4
    for mode in ('M0X0', 'M0X1', 'M1X0', 'M1X1'):
        assert f'/*{tool.MARKER}-{mode}*/' in result
    assert tool.inject(result) == (result, 0)


def test_unrelated_branch_is_not_treated_as_the_horizontal_cull():
    source = (f'RecompReturn {tool.FUNCTION}_M1X0(CpuState *cpu) {{\n'
              '    if (cpu->_flag_C == 1) { cpu->cycles += 1; cpu->master_cycles += 8; goto L_E475_M1X0; }\n}\n')
    with pytest.raises(SystemExit, match='could not locate horizontal cull branch'):
        tool.inject(source)
