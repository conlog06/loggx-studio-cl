# Referenz für den C++-Port: Test-Mischung erzeugen und mit dem Original-Demucs
# (PyTorch) trennen – ohne Zufallsverschiebung, Segmente mit 25 % Überlappung.
import sys
import numpy as np
import soundfile as sf
import torch
from demucs.pretrained import get_model
from demucs.apply import apply_model

out = sys.argv[1]
sr = 44100
t = np.arange(int(sr * 20)) / sr
rng = np.random.default_rng(7)
beat = 60 / 124
kick = np.zeros_like(t); hat = np.zeros_like(t); bass = np.zeros_like(t); voc = np.zeros_like(t)
for k in range(int(20 / beat)):
    s = int(k * beat * sr); n = min(len(t) - s, int(0.25 * sr)); tt = np.arange(n) / sr
    kick[s:s + n] += np.sin(2 * np.pi * (50 + 90 * np.exp(-tt * 30)) * tt) * np.exp(-tt * 9)
    s2 = int((k + 0.5) * beat * sr); n2 = min(len(t) - s2, int(0.05 * sr))
    if n2 > 0: hat[s2:s2 + n2] += rng.normal(0, 1, n2) * np.exp(-np.arange(n2) / sr * 80) * 0.3
notes = [41, 41, 44, 39]
for k in range(int(20 / (beat * 2))):
    s = int(k * beat * 2 * sr); n = min(len(t) - s, int(beat * 2 * sr)); tt = np.arange(n) / sr
    f = 440 * 2 ** ((notes[k % 4] - 69) / 12)
    bass[s:s + n] += np.tanh(2 * np.sin(2 * np.pi * f * tt)) * 0.4
mel = [69, 72, 74, 72, 67, 69]
for k in range(int(20 / beat)):
    s = int(k * beat * sr); n = min(len(t) - s, int(beat * 0.9 * sr)); tt = np.arange(n) / sr
    f = 440 * 2 ** ((mel[k % 6] - 69) / 12) * (1 + 0.01 * np.sin(2 * np.pi * 5.5 * tt))
    v = sum(np.sin(2 * np.pi * f * h * tt) / h * np.exp(-((f * h - 800) / 600) ** 2) for h in range(1, 12))
    voc[s:s + n] += v * np.minimum(1, tt * 20) * 0.5
mix = np.stack([kick * 0.8 + hat + bass + voc * 0.9, kick * 0.8 + hat * 0.7 + bass + voc]).astype(np.float32)
mix /= np.abs(mix).max() * 1.2
sf.write(f"{out}/demucs_ref_mix.wav", mix.T, sr, subtype="FLOAT")

model = get_model("htdemucs")
model.eval()
with torch.no_grad():
    x = torch.from_numpy(mix)
    ref = x.mean(0)
    xn = (x - ref.mean()) / ref.std()
    stems = apply_model(model, xn[None], shifts=0, split=True, overlap=0.25, progress=False)[0]
    stems = stems * ref.std() + ref.mean()
for i, name in enumerate(model.sources):
    sf.write(f"{out}/demucs_ref_{name}.wav", stems[i].numpy().T, sr, subtype="FLOAT")
print("Quellen:", model.sources, "Segment:", float(model.segment))
