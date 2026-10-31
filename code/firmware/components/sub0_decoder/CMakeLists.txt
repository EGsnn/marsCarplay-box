idf_component_register(SRCS "box_decoder.cpp" INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "vendor/src" "vendor/include" REQUIRES heap freertos esp_timer)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_23)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wno-type-limits)
target_compile_definitions(${COMPONENT_LIB} PRIVATE NDEBUG=1)
