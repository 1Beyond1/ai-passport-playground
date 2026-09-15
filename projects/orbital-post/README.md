English | [简体中文](README.zh_CN.md)

# Orbital Post

A tiny offline passport for PIP-01, a pixel courier collecting letters from six imaginary places. No account, network, music files or cloud service is needed. This is a short interactive vignette, not a procedural game or real radio receiver.

## Controls

- Passport / collection: UP and DOWN switch pages; OK enters destinations.
- Destinations: UP and DOWN select a place; OK starts a five-second delivery.
- Arrival: read the letter; OK opens your stamp collection.
- Hold OK anywhere to cancel and return to the passport.

The courier blinks and hops. Each first delivery saves a stamp in the ordinary NVS namespace `orbit_post`. Writes run in a worker, not the button callback. If storage initialization fails, collection is session-only. In-flight saves can be lost if power is removed immediately. This version has no automatic screen sleep and does not redefine the physical power switch. No sound or real wireless scanning occurs.

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

## Logic test

```sh
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_orbit_state.c main/orbit_state.c -o test_orbit
./test_orbit
```

Tests cover navigation, timer rollover, cancellation, duplicate delivery and collecting all six stamps. Visual clipping, physical key response, battery life and persistence after power cycling require device checks.
