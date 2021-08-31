@echo off
mkdir build
pushd build
cmake .. -G "Visual Studio 16 2019" -A x64 -DCMAKE_INSTALL_PREFIX=./build/install -DCLANG_TIDY_APPLY_FIXES=ON
popd