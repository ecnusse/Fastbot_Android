
cd "native"

rm -r  CMakeFiles/
cmake -DCMAKE_TOOLCHAIN_FILE="D:\Android\Sdk\ndk\25.2.9519653\build\cmake\android.toolchain.cmake" -DANDROID_ABI=armeabi-v7a -DCMAKE_BUILD_TYPE=Release
nmake
rm -r  CMakeFiles/

cmake -DCMAKE_TOOLCHAIN_FILE="D:\Android\Sdk\ndk\25.2.9519653\build\cmake\android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DCMAKE_BUILD_TYPE=Release
nmake
rm -r  CMakeFiles/

cmake -DCMAKE_TOOLCHAIN_FILE="D:\Android\Sdk\ndk\25.2.9519653\build\cmake\android.toolchain.cmake" -DANDROID_ABI=x86  -DCMAKE_BUILD_TYPE=Release
nmake
rm -r  CMakeFiles/

cmake -DCMAKE_TOOLCHAIN_FILE="D:\Android\Sdk\ndk\25.2.9519653\build\cmake\android.toolchain.cmake" -DANDROID_ABI=x86_64  -DCMAKE_BUILD_TYPE=Release
nmake
rm -r  CMakeFiles/
