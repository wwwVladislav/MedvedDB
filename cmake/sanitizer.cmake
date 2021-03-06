if(ENABLE_SANITIZER)
    set(ASAN_COMPILER_FLAGS "-g -fsanitize=address -fno-omit-frame-pointer -fno-optimize-sibling-calls -fsanitize-address-use-after-scope")
    set(ASAN_LINKER_FLAGS   "-fsanitize=address")

    set(CMAKE_C_FLAGS             "${CMAKE_C_FLAGS}             ${ASAN_COMPILER_FLAGS}")
    set(CMAKE_CXX_FLAGS           "${CMAKE_CXX_FLAGS}           ${ASAN_COMPILER_FLAGS}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${ASAN_LINKER_FLAGS}")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${ASAN_LINKER_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS    "${CMAKE_EXE_LINKER_FLAGS}    ${ASAN_LINKER_FLAGS}")
endif()
