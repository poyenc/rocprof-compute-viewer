set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

add_executable(rocprof-compute-viewer-cli
    main.cpp
    headless_dispatcher.cpp
    session_cache.cpp
    cmd_info.cpp
    cmd_isa.cpp
    cmd_waves.cpp
    cmd_occupancy.cpp
    cmd_latency.cpp
    cmd_counters.cpp
    cmd_perfcounters.cpp
    ${CMAKE_SOURCE_DIR}/src/analysis/latency.cpp
    ${CMAKE_SOURCE_DIR}/src/analysis/derived_counter.cpp
    ${CMAKE_SOURCE_DIR}/src/code/codeload.cpp
    ${CMAKE_SOURCE_DIR}/src/data/shaderdata.cpp
    ${CMAKE_SOURCE_DIR}/src/data/wavedata.cpp
    ${CMAKE_SOURCE_DIR}/src/util/jsonfilereader.cpp
)

target_include_directories(rocprof-compute-viewer-cli PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json/include
    ${CMAKE_BINARY_DIR}/src
)

target_link_libraries(rocprof-compute-viewer-cli PRIVATE Threads::Threads)
target_compile_features(rocprof-compute-viewer-cli PRIVATE cxx_std_20)

install(TARGETS rocprof-compute-viewer-cli DESTINATION ${CMAKE_INSTALL_BINDIR})
