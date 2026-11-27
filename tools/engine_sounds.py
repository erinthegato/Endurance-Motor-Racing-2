"""The engine sounds (DATA/audio/engine_*.raw) and the GT3 shift whine (DATA/sfx/gt_shift.raw).

Made from ElevenLabs Sound Effects v3 takes (flow "EMR 2.0 engine sounds"): a steady high-rev loop for
each engine, a hybrid whine laid under every Hypercar, a muffled low rumble under every LMGT3, all made
seamless 3 s loops, their loudness evened out and pushed LOUD (RMS ~0.42, a soft limiter under 0.97).
The shift: the V12 take's pure tone swept up ~45 % and cut off by a gear clack.

  python3 tools/engine_sounds.py      (run in a folder holding wav/<take>.f32: 22050 Hz mono float32,
                                       decoded from the takes; writes out/*.raw)
"""
import numpy as np
from scipy.signal import butter, sosfiltfilt
SR = 22050
W = lambda n: np.fromfile(f'wav/{n}.f32', dtype='<f4').astype(np.float64)
def band(a, lo=None, hi=None, order=4):
    if lo and hi: sos = butter(order, [lo, hi], 'bandpass', fs=SR, output='sos')
    elif lo: sos = butter(order, lo, 'highpass', fs=SR, output='sos')
    else: sos = butter(order, hi, 'lowpass', fs=SR, output='sos')
    return sosfiltfilt(sos, a)
def flatten(a, win=0.04):
    # even out the loudness: the dips and swells of a take divided out (a steady loop for the game to pitch)
    n = int(SR * win); e = np.sqrt(np.convolve(a * a, np.ones(n) / n, 'same')) + 1e-6
    k = int(SR * 0.12); e = np.convolve(e, np.ones(k) / k, 'same')
    g = np.clip(np.median(e) / e, 0.25, 6.0)
    return a * g
def loopify(a, secs=3.0, x=0.25):
    # a seamless loop: the segment's tail cross-faded into its head (equal power)
    L, X = int(SR * secs), int(SR * x)
    start = max(0, (len(a) - L - X) // 2)
    s = a[start:start + L + X].copy()
    if len(s) < L + X: s = np.concatenate([s, a[:L + X - len(s)]])
    t = np.linspace(0, np.pi / 2, X)
    head = s[:X] * np.sin(t) + s[L:L + X] * np.cos(t)
    return np.concatenate([head, s[X:L]])
rms = lambda a: np.sqrt((a * a).mean())
def loud(a, target=0.33):
    # LOUD: up to an RMS of 0.33 (about -9.6 dBFS), a soft limiter keeps the peaks under full scale
    a = a - a.mean(); a = a / rms(a) * target
    return 0.97 * np.tanh(a * 1.6)                   # peaks never past 0.97
def write(name, a):
    np.clip(a * 32767, -32767, 32767).astype('<i2').tofile(f'out/{name}.raw')
    print(f'{name:22s} {len(a)/SR:.2f} s  rms {rms(a):.3f}  peak {np.abs(a).max():.3f}')

# the hybrid whine (all Hypercars): the V12 take's pure high tone, 1.4-7 kHz
whine = loopify(flatten(band(W('v12_0'), 1400, 7000)))
whine /= rms(whine)
# the muffled rumble (all LMGT3s): the rumble take under 220 Hz
rumble = loopify(flatten(band(W('gt_rumble_2'), hi=220)))
rumble /= rms(rumble)

HYPER = {'hyper_v6': 'hyper_v6_3', 'hyper_v8_na': 'hyper_v8_na_1', 'hyper_v8_turbo': 'hyper_v8_turbo_0', 'v12': 'v12_2'}
GT = {'gt_v6_turbo': 'gt_v6_turbo_1', 'flat6': 'flat6_3', 'i6_turbo': 'i6_turbo_0', 'v8_flatplane': 'v8_flatplane_3',
      'gt_v8_turbo': 'gt_v8_turbo_2', 'v8_na': 'v8_na_0'}
for out, take in HYPER.items():
    e = loopify(flatten(W(take)))
    if out == 'v12':        # the V12 take is nearly all scream: a hybrid V8's body under it
        body = loopify(flatten(W('hyper_v8_na_1'))); e = e / rms(e) + 0.6 * body / rms(body)
    e /= rms(e)
    write('engine_' + out, loud(e + 0.55 * whine))
for out, take in GT.items():
    e = loopify(flatten(W(take))); e /= rms(e)
    write('engine_' + out, loud(e + 0.75 * rumble))

# the GT3 shift: a straight-cut gear whine that rises and is cut off by the clack of the next gear
n = int(SR * 0.42)
src = band(W('v12_0'), 1400, 7000); src = flatten(src)[SR // 2:SR // 2 + 2 * n]
rate = np.linspace(1.0, 1.45, n)                    # the pitch climbs ~45 % through the whine
pos = np.cumsum(rate); w = np.interp(pos, np.arange(len(src)), src)
env = np.minimum(1, np.arange(n) / (SR * 0.025)) * np.linspace(0.75, 1.0, n)
env[-int(SR * 0.012):] *= np.linspace(1, 0, int(SR * 0.012))    # cut off
w = w * env / rms(w)
c = W('gt_shift_3'); i = int(np.argmax(np.abs(c))); clack = band(c[max(0, i - 200):i + int(SR * 0.09)], lo=300)
clack /= np.abs(clack).max()
out = np.zeros(n + len(clack)); out[:n] += w; out[n - 150:n - 150 + len(clack)] += 1.6 * clack
out[-int(SR * 0.03):] *= np.linspace(1, 0, int(SR * 0.03))
out = out / np.abs(out).max() * 0.95
write('gt_shift', out)
