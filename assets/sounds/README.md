# Sounds

| File | Description |
|---|---|
| `coin.wav` | Coin pickup: two quick rising notes |
| `whoosh.wav` | Particle burst: a swept noise swell |

These sounds were synthesised for this project (sine tones, filtered noise and simple
envelopes; mono, 48 kHz, 16-bit WAV). They are released into the public domain (CC0):
use, change and ship them freely.

Play them with `app.audio().play(core::assetPath("sounds/<file>"))`. Supported formats are
WAV, FLAC, MP3 and OGG Vorbis. For music, put tracks in `assets/music/` and use
`app.audio().playMusic(...)`; music is streamed from disk rather than loaded into memory.
