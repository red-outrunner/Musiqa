#!/bin/bash
set -e

echo "Setting up shell profiles..."
for rc in ~/.zshrc ~/.bashrc; do
    if [ -f "$rc" ]; then
        if ! grep -q ".flutter-sdk/bin" "$rc"; then
            echo 'export PATH="$PATH:$HOME/.flutter-sdk/bin"' >> "$rc"
        fi
        if ! grep -q "ANDROID_HOME" "$rc"; then
            echo 'export ANDROID_HOME="$HOME/Android/Sdk"' >> "$rc"
            echo 'export PATH="$PATH:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools"' >> "$rc"
        fi
    fi
done

echo "Downloading Android command line tools..."
mkdir -p ~/Android/Sdk/cmdline-tools
cd ~/Android/Sdk/cmdline-tools
if [ ! -d "latest" ]; then
    curl -o cmdline-tools.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
    unzip -q cmdline-tools.zip
    mv cmdline-tools latest
    rm cmdline-tools.zip
fi

export ANDROID_HOME="$HOME/Android/Sdk"
export PATH="$PATH:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$HOME/.flutter-sdk/bin"

echo "Accepting Android licenses and installing packages..."
yes | sdkmanager --licenses >/dev/null
sdkmanager "platform-tools" "platforms;android-34" "build-tools;34.0.0" >/dev/null

echo "Accepting Flutter Android licenses..."
yes | flutter doctor --android-licenses >/dev/null

echo "Running flutter doctor..."
flutter doctor
