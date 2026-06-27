add_library(catch2_with_main STATIC
    extras/catch_amalgamated.cpp
)

add_library(Catch2::Catch2WithMain ALIAS catch2_with_main)

target_include_directories(catch2_with_main
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/extras"
)

target_compile_features(catch2_with_main PUBLIC cxx_std_20)