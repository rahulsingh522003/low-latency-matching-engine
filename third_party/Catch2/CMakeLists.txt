add_library(Catch2 STATIC catch_amalgamated.cpp)
add_library(Catch2::Catch2 ALIAS Catch2)

target_compile_features(Catch2 PUBLIC cxx_std_14)
target_include_directories(Catch2 SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(Catch2 PROPERTIES CXX_EXTENSIONS OFF)
