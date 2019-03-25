if (NOT ("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU"))
message(FATAL_ERROR "Unknown compiler ${CMAKE_C_COMPILER}, please add optimization flags for it into CompilerFlags.cmake")
endif ()

if (NOT ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU"))
message(FATAL_ERROR "Unknown compiler ${CMAKE_CXX_COMPILER}, please add optimization flags for it into CompilerFlags.cmake")
endif ()

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -DNDEBUG -g -ffast-math -march=native")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DNDEBUG -g -ffast-math -march=native")

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -O0")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
