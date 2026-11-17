cmake_minimum_required(VERSION 3.25)
project(Tessera VERSION 0.5.5 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT: one exe, no runtime DLLs to carry beside it.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

add_compile_options(/W4 /permissive- /utf-8 /EHsc /Zc:__cplusplus
                    /O2 /GL /Gw /Gy
                    /sdl /GS /guard:cf)

add_link_options(/LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO /DEBUG:NONE
                 /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /CETCOMPAT /guard:cf
                 # 0x800 = LOAD_LIBRARY_SEARCH_SYSTEM32: a DLL planted beside the portable exe is never loaded.
                 /DEPENDENTLOADFLAG:0x800)

set(TESSERA_CORE Src/Tessera/Layout.cpp Src/Tessera/Config.cpp)

add_executable(Tessera WIN32
    Src/Tessera/Main.cpp Src/Tessera/Overlay.cpp Src/Tessera/Editor.cpp Src/Tessera/Settings.cpp ${TESSERA_CORE}
    Src/Tessera/Tessera.rc)
target_compile_definitions(Tessera PRIVATE TESSERA_VERSION="${PROJECT_VERSION}")
target_link_libraries(Tessera PRIVATE user32 gdi32 shell32 dwmapi advapi32 comdlg32 shcore)

add_executable(LayoutTests Tests/LayoutTests.cpp ${TESSERA_CORE})
add_executable(UiTest Tests/UiTest.cpp ${TESSERA_CORE})
target_link_libraries(UiTest PRIVATE user32 dwmapi)
add_executable(HiddenRun Tests/HiddenRun.cpp)
target_link_libraries(HiddenRun PRIVATE user32)
