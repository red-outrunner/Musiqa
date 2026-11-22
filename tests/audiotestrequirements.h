#pragma once

#include <QAudioDevice>
#include <QMediaDevices>
#include <QtTest>

// Hosted Windows runners usually have no sound device. Keep software mixer
// tests active, and report hardware-dependent integration checks as skipped.
#define MUSICA_REQUIRE_AUDIO_OUTPUT() \
    do { \
        if (QMediaDevices::defaultAudioOutput().isNull()) \
            QSKIP("This playback integration check requires an audio output device; software mixer checks still run."); \
    } while (false)
