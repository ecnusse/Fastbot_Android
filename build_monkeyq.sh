#!/bin/zsh

./gradlew clean makeJar
~/Library/Android/sdk/build-tools/28.0.3/dx --dex --min-sdk-version=26 --output=monkeyq.jar ~/Desktop/coding/Fastbot_Android/monkey/build/libs/monkey.jar
