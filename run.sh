./gradlew clean makeJar
java -jar D:\Android\Sdk\build-tools\28.0.3\lib\dx.jar --dex --output=D:\Mike\AndroidStudioProjects\Fastbot_Android\monkeyq.jar D:\Mike\AndroidStudioProjects\Fastbot_Android\monkey\build\libs\monkey.jar

adb shell CLASSPATH=/sdcard/monkeyq.jar:/sdcard/framework.jar:/sdcard/fastbot-thirdpart.jar exec app_process /system/bin com.android.commands.monkey.Monkey -p it.feio.android.omninotes.alpha --agent reuseq --act-blacklist-file  /sdcard/abl.strings --running-minutes 10 --throttle 20 --bugreport --output-directory /sdcard/fastbot_report -v -v -v

Copy-Item -Path "D:\Mike\AndroidStudioProjects\Fastbot_Android\libs\*" -Destination "D:\Mike\PycharmProjects\KeaPlus\kea2\assets\fastbot_libs" -Recurse -Force