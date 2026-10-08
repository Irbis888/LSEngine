# Vendored enkiTS

Source: https://github.com/dougbinks/enkiTS

Commit: `404a3bf8f855039dfff2052184d6308655286c07`

The C++ source files, README and zlib license are copied unchanged from this
commit. Only `src/TaskScheduler.cpp` needs to be compiled for the C++ API.

`enkiTS.vcxproj`, its filters and `enkiTS.props` are local Visual Studio
integration files. Try2 references the static library project and imports its
header search path. The upstream source files remain unchanged.
