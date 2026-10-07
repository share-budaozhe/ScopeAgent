# -*- coding: utf-8 -*-
"""
schema.py — DeviceConfig 契约定义与校验/钳制（纯标准库，无第三方依赖）

本文件是 Qt 侧与 Agent 侧共享的配置契约的唯一权威定义。
任何来自 LLM 的配置都必须经过 validate_config() 钳制后才能下发。
"""
import copy

# 虚拟示波器能力规格（供 Agent get_spec 工具使用）
SCOPE_SPEC = {
    "name": "虚拟示波器 VirtualScope-1000",
    "bandwidth_mhz": 1000,
    "max_sample_rate": "10 GS/s",
    "horizontal_divisions": 10,
    "vertical_divisions": 8,
    "waveforms": ["sine", "square", "triangle", "sawtooth", "noise"],
    "frequency_range": "1 Hz ~ 10 MHz",
    "amplitude_range": "0.01 Vpp ~ 100 Vpp",
    "sec_per_div_range": "500 ns/div ~ 1 s/div",
    "volts_per_div_range": "5 mV/div ~ 20 V/div",
    "trigger_slopes": ["rising", "falling"],
}

# 字段约束定义：type / 范围 / 默认值 / 枚举白名单
CONFIG_FIELDS = {
    "waveform":        {"type": "enum",   "values": ["sine", "square", "triangle", "sawtooth", "noise"], "default": "sine"},
    "frequency_hz":    {"type": "number", "min": 1.0,      "max": 10_000_000.0, "default": 1000.0},
    "amplitude_vpp":   {"type": "number", "min": 0.01,     "max": 100.0,        "default": 3.0},
    "dc_offset_v":     {"type": "number", "min": -100.0,   "max": 100.0,        "default": 0.0},
    "sec_per_div":     {"type": "number", "min": 0.000_000_5, "max": 1.0,       "default": 0.0005},
    "volts_per_div":   {"type": "number", "min": 0.005,    "max": 20.0,         "default": 1.0},
    "trigger_level_v": {"type": "number", "min": -100.0,   "max": 100.0,        "default": 0.0},
    "trigger_slope":   {"type": "enum",   "values": ["rising", "falling"],      "default": "rising"},
    "noise_level":     {"type": "number", "min": 0.0,      "max": 1.0,          "default": 0.02},
}

FIELD_ORDER = [
    "waveform", "frequency_hz", "amplitude_vpp", "dc_offset_v",
    "sec_per_div", "volts_per_div", "trigger_level_v", "trigger_slope", "noise_level",
]


def _clamp_number(value, lo, hi, default):
    """把 value 钳制到 [lo, hi]，非法值回退 default。"""
    try:
        v = float(value)
    except (TypeError, ValueError):
        return default
    if v < lo:
        return lo
    if v > hi:
        return hi
    return v


def validate_config(cfg, base=None):
    """校验并钳制一份配置。

    cfg:  待校验的 dict（可能来自 LLM，字段可缺失/越界）
    base: 基础配置（当前状态），缺失字段从其继承
    返回: (sanitized_dict, warnings_list)
    """
    warnings = []
    base = base or {}
    out = {}

    for name in FIELD_ORDER:
        spec = CONFIG_FIELDS[name]
        if name in cfg and cfg[name] is not None:
            raw = cfg[name]
            if spec["type"] == "enum":
                if raw in spec["values"]:
                    out[name] = raw
                else:
                    warnings.append("波形/触发字段 %s=%r 非法，已回退默认 %s" % (name, raw, spec["default"]))
                    out[name] = base.get(name, spec["default"])
            else:  # number
                before = raw
                out[name] = _clamp_number(raw, spec["min"], spec["max"], base.get(name, spec["default"]))
                try:
                    if float(before) != out[name]:
                        warnings.append("%s=%s 超出范围 [%s,%s]，已钳制为 %s" %
                                        (name, before, spec["min"], spec["max"], out[name]))
                except (TypeError, ValueError):
                    warnings.append("%s=%r 非法，已回退 %s" % (name, raw, out[name]))
        else:
            out[name] = base.get(name, spec["default"])

    # 一致性规则：时基应能展示至少 1~2 个周期（软约束，仅告警）
    try:
        period = 1.0 / out["frequency_hz"]
        if out["sec_per_div"] > period * 10:
            warnings.append("时基 %ss/div 相对频率 %sHz 偏慢，可能看不到完整周期，建议减小" %
                            (out["sec_per_div"], out["frequency_hz"]))
    except ZeroDivisionError:
        pass

    return out, warnings


def default_config():
    """返回默认配置。"""
    out, _ = validate_config({})
    return out


def config_summary(cfg):
    """生成给 LLM / 日志看的可读摘要。"""
    return ("波形=%s, 频率=%.4g Hz, 峰峰值=%.4g V, 直流=%.4g V, 时基=%.4g s/div, "
            "垂直=%.4g V/div, 触发=%.4g V(%s), 噪声=%.3g" %
            (cfg["waveform"], cfg["frequency_hz"], cfg["amplitude_vpp"], cfg["dc_offset_v"],
             cfg["sec_per_div"], cfg["volts_per_div"], cfg["trigger_level_v"],
             cfg["trigger_slope"], cfg["noise_level"]))
