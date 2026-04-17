set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

add_executable(rcv
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
    cmd_summary.cpp
    ${CMAKE_SOURCE_DIR}/src/analysis/latency.cpp
    ${CMAKE_SOURCE_DIR}/src/analysis/derived_counter.cpp
    ${CMAKE_SOURCE_DIR}/src/code/codeload.cpp
    ${CMAKE_SOURCE_DIR}/src/data/shaderdata.cpp
    ${CMAKE_SOURCE_DIR}/src/data/wavedata.cpp
    ${CMAKE_SOURCE_DIR}/src/util/jsonfilereader.cpp
)

target_include_directories(rcv PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/json/include
    ${CMAKE_BINARY_DIR}/src
)

target_link_libraries(rcv PRIVATE Threads::Threads)
target_compile_features(rcv PRIVATE cxx_std_20)

install(TARGETS rcv DESTINATION ${CMAKE_INSTALL_BINDIR})
