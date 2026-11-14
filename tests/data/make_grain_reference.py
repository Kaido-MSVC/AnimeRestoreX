"""Reference values for tst_inference_core testGrainMatchesUpstream (film grain, v2.1 P4).

nunif 0d3d92c nunif/utils/rgb_noise.py apply_rgb_noise, written in numpy float32 (no torch needed):

    output = rgb ** gamma
    correlated_noise = noise * output
    light_decay = ((1.0 - output) * light_decay_strength + (1.0 - light_decay_strength)) ** gamma
    weight = light_decay * strength
    output = (output + correlated_noise * weight).clamp(0, 1) ** (1.0 / gamma)

then quantize256 ((x * 255).round().clamp(0, 255), round half to even).
Inputs: rgb = k / 255 (k = 0..255), noise = (j - 8) / 4 (j = 0..16), strength in STRENGTHS.
Output: one line per strength, "<strength> <4352 values>" in (k, j) order, j fastest.

Usage: python -I make_grain_reference.py > grain_reference.txt
"""
import numpy as np

STRENGTHS = ["0.025", "0.1", "0.5"]  # UI 0.05 / 0.20 (default) / 1.00, times 0.5 for still images
f32 = np.float32


def apply_rgb_noise(rgb, noise, strength, gamma=2.2, light_decay_strength=0.8):
    output = np.power(rgb, f32(gamma))
    correlated_noise = noise * output
    light_decay = (f32(1.0) - output) * f32(light_decay_strength) + f32(1.0 - light_decay_strength)
    light_decay = np.power(light_decay, f32(gamma))
    weight = light_decay * f32(strength)
    output = output + correlated_noise * weight
    return np.power(np.clip(output, f32(0), f32(1)), f32(1.0 / gamma))


def quantize256(x):
    return np.clip(np.round(x * f32(255.0)), 0, 255).astype(np.uint8)


rgb = (np.arange(256, dtype=np.float32) / f32(255.0))[:, None]
noise = ((np.arange(17, dtype=np.float32) - f32(8.0)) / f32(4.0))[None, :]
for s in STRENGTHS:
    q = quantize256(apply_rgb_noise(rgb, noise, f32(float(s))).astype(np.float32))
    print(s, " ".join(str(int(v)) for v in q.ravel()))
