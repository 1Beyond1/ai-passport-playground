English | [简体中文](README.zh_CN.md)

# Pocket Universe / Orbital Post

A Chinese-first four-tool collection for the AI Passport. It includes a localized interstellar post office, wearable mood sign, random decision tools, and a reaction-time game. No account, network, music files, or cloud service is needed.

## Included tools

- Interstellar post office: collect six fictional letters and persistent postmarks.
- Mood sign: switch between six large, colorful wearable status cards.
- Randomizer: coin, six-sided die, and six short decision prompts.
- Reaction test: wait for green and measure button response time.

## Controls

- Home: UP and DOWN select; OK enters.
- Inside a tool: UP and DOWN change choices; OK performs the action.
- Hold OK to return home.
- After 90 seconds without input the backlight turns off; the next key only wakes it.

The courier blinks and hops. Each first delivery saves a stamp in the ordinary NVS namespace `orbit_post`. Writes run in a worker, not the button callback. If storage initialization fails, collection is session-only. In-flight saves can be lost if power is removed immediately. The application does not redefine the physical power switch. No sound or real wireless scanning occurs.

The subsetted 16 px and 28 px Chinese fonts are generated from Source Han Sans SC under SIL OFL 1.1; the license is included in [assets/fonts/SourceHanSansSC-OFL.txt](assets/fonts/SourceHanSansSC-OFL.txt).

## Build

Activate ESP-IDF **5.5.3**, then run from this directory:

```sh
idf.py build
idf.py -p YOUR_PORT flash
```

On a provisioned device with the supplied matching partition layout, application-only flashing preserves all existing data:

```sh
python -m esptool --chip esp32c3 --port YOUR_PORT --baud 115200 write_flash 0x10000 build/FoloToy-AI-Passport.bin
```

Never run erase-flash. `cardid` stays at `0x356000`; the application limit is 3 MB. Back up your current app before replacing it.

## Logic tests

```sh
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_orbit_state.c main/orbit_state.c -o test_orbit
./test_orbit
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_pocket_state.c main/pocket_state.c -o test_pocket
./test_pocket
```

Tests cover menu/tool navigation, random bounds, reaction timing and rollover, cancellation, duplicate delivery, and collecting all six stamps. Visual clipping, physical key response, battery life and persistence after power cycling require device checks.
