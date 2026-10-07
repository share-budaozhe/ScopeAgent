# -*- coding: utf-8 -*-
"""
schema_test.py — schema 单元测试（无需网络，可直接运行）
用法：python schema_test.py
"""
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import schema


def check(cond, msg):
    print("[PASS]" if cond else "[FAIL]", msg)
    if not cond:
        global failed
        failed += 1


failed = 0

# T1 越界钳制
cfg, warn = schema.validate_config({'frequency_hz': 999999999, 'waveform': 'bogus', 'sec_per_div': -5})
check(cfg['frequency_hz'] == 10000000.0, "频率越界钳制为 10MHz")
check(cfg['waveform'] == 'sine', "非法波形回退 sine")
check(cfg['sec_per_div'] == 0.0000005, "时基越界钳制为 500ns/div")
check(len(warn) >= 3, "产生钳制告警")

# T2 部分字段继承 base
cfg2, _ = schema.validate_config({'amplitude_vpp': 5}, base={'frequency_hz': 2000, 'waveform': 'square'})
check(cfg2['frequency_hz'] == 2000 and cfg2['waveform'] == 'square', "缺失字段继承当前状态")

# T3 默认配置完整
check(sorted(schema.default_config().keys()) == sorted(schema.CONFIG_FIELDS.keys()), "默认配置字段完整")

# T4 枚举白名单
cfg4, _ = schema.validate_config({'trigger_slope': 'sideways'})
check(cfg4['trigger_slope'] == 'rising', "非法触发斜率回退 rising")

# T5 时基一致性软告警（周期 1ms，时基 1s/div 偏慢）
_, warn5 = schema.validate_config({'frequency_hz': 1000, 'sec_per_div': 1.0})
check(any("时基" in w for w in warn5), "时基相对频率偏慢时给出建议")

print("\n%s (%d failures)" % ("ALL SCHEMA TESTS PASSED" if failed == 0 else "TESTS FAILED", failed))
sys.exit(0 if failed == 0 else 1)
