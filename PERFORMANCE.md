# Musica performance measurements

Development measurements after 1.0.1, recorded on 22 November 2026. Timings vary with hardware, Qt, operating-system caches, and library content.

## Local reference run

Fedora Linux 44, Intel Core i5-1334U, x86_64, Qt 6.11.2, Release build. The startup check used Qt's offscreen platform and software renderer with an isolated, empty library.

| Startup/process measurement | Recorded value |
| --- | ---: |
| Launch to first displayed frame | 651 ms |
| Resident memory, including shared library pages | 179.7 MiB |
| Idle app-process CPU over a 2.95-second sample | 0.12% of one CPU core |

CPU and memory counters describe the Musica process. External decoder subprocesses have their own resource usage. These startup measurements use an empty profile; library page queries are measured separately below.

## Synthetic large-library queries

The desktop test creates a temporary SQLite library with cached metadata, reopens it, searches and sorts songs, and fetches 100 widely spaced metadata pages. Fixture creation is outside the reported query timings.

| Songs | Title sort | Exact-title search | 100 page fetches | Cached track records afterward |
| ---: | ---: | ---: | ---: | ---: |
| 10,000 | 6 ms | 2 ms | 57 ms | 144 |
| 50,000 | 30 ms | 12 ms | 59 ms | 144 |

The cache count covers this one model. Other views, artwork, and audio buffers have separate caches. Gesture scrolling has separate functional UI tests; these timings describe metadata fetches. Battery-life, rendered frame-rate, physical sleep/Bluetooth, and NVDA/Orca measurements require real-device checks.

## Battery saver

Choose **Off**, **On battery power**, or **Always** in **Settings → Laptop and performance**. Automatic mode uses desktop power notifications when available.

- Decorative color transitions and new artwork-accent analysis are reduced.
- Periodic playback-position snapshots are limited to five per second.
- Audible-listening progress notifications are limited to ten per second. Pause, resume, seek, and finish boundaries remain immediate.
- Audio mixing, crossfade timing, and scrolling keep their normal scheduling.
- Process measurements refresh only while their settings section is visible. Measurements stay on the computer.

## Reproduce

Run the CMake-discovered `musica_desktop` and `musica_startup_measurement` checks using your CMake test integration. Query measurements appear as `PERFORMANCE` records in the desktop test output; the startup check writes `build/test-results/startup-metrics.json`.

For a standalone startup measurement, launch the built application with `--benchmark report.json`. It creates a temporary empty profile, samples startup and idle resource use, writes JSON, and exits. Normal settings and music remain in their existing profile.

To inspect a real collection, open **Settings → Laptop and performance**. Use the operating system's process monitor to include decoder subprocesses when measuring complete playback resource use.
