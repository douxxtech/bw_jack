# bw_jack

ALSA audio backend for [BotWave](https://github.com/dpipstudio/botwave).

Instead of the default `bw_custom` backend (which streams via GPIO FM + RDS), `bw_jack` routes audio directly through the system's ALSA/JACK output. Originally built to pipe audio into a walkie-talkie on a Raspberry Pi 3B, but works with anything ALSA can talk to.

<div align="center">
<img src="./talkie_broadcast.png" width=300>

> Audio being broadcasted with a talkie walkie using bw_jack
</div>

## Build

```bash
gcc bw_jack.c -o bw_jack -lasound -lsndfile
```

## Install

### 1. Place the binary

```bash
sudo mkdir -p /opt/BotWave/backends/bw_jack
sudo cp bw_jack /opt/BotWave/backends/bw_jack/bw_jack
```

## Install

Place the binary somewhere accessible:

```bash
sudo mkdir -p /opt/BotWave/backends/bw_jack
sudo cp bw_jack /opt/BotWave/backends/bw_jack/bw_jack
```

## Configure

Set `backend_path` to point BotWave at the new binary. Pick whichever method suits you:

**Via `bw-local` shell:**
```
set backend_path /opt/BotWave/backends/bw_jack/bw_jack
```

**Via `.env` or config file:**
```env
backend_path=/opt/BotWave/backends/bw_jack/bw_jack
```
> Any file passed with `--config CONFIG` when running BotWave works here too.

It's also recommended to enable talk mode while getting things set up:
```
set talk true
```

Then just run:
```
start <any audio file>
```

That's it, the path is now stored. However, if you went with the `set` method, you'll need to run the command on every new session. It is recommended to use a config file for a permanent setup. 

## Usage

`bw_jack` is invoked by BotWave automatically, but you can also run it standalone:

```
bw_jack -audio <file|-> [-rate N] [-channels N] [-loop] [-raw]
```

| Flag | Default | Description |
|---|---|---|
| `-audio <path>` | *(required)* | Audio file to play, or `-` to read from stdin |
| `-rate N` | `48000` | Sample rate (stdin/raw mode only) |
| `-channels N` | `2` | Channel count (stdin/raw mode only) |
| `-loop` | off | Loop the file indefinitely |
| `-raw` | off | Treat file as raw S16LE PCM (skip libsndfile) |

**Examples:**

```bash
# Play a WAV file
./bw_jack -audio track.wav

# Loop an MP3
./bw_jack -audio jingle.mp3 -loop

# Stream raw PCM from stdin (e.g. from ffmpeg)
ffmpeg -i input.mp3 -f s16le -ar 48000 -ac 2 - | ./bw_jack -audio -
```

## Device

Automatically detects the `bcm2835 Headphones` card by name. 
Falls back to `plughw:0,0` if not found. To target a different card, change `DEVICE_NAME` at 
the top of `bw_jack.c` before building. You can also change `FALLBACK` if needed.

## Dependencies

- `libasound2-dev` (ALSA)
- `libsndfile1-dev` (audio file decoding)

```bash
sudo apt install libasound2-dev libsndfile1-dev
```

## License
Licensed under [GPLv3.0](LICENSE)