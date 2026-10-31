# Supply an authorized provider implementation outside this repository, or use the default stub.
set(MARS_AUTH_PROVIDER_SOURCE "" CACHE FILEPATH "External source implementing auth_software_provider.h")
if(MARS_AUTH_PROVIDER_SOURCE)
    if(NOT IS_ABSOLUTE "${MARS_AUTH_PROVIDER_SOURCE}" OR NOT EXISTS "${MARS_AUTH_PROVIDER_SOURCE}")
        message(FATAL_ERROR "MARS_AUTH_PROVIDER_SOURCE must be an existing absolute source path")
    endif()
    set(provider_source "${MARS_AUTH_PROVIDER_SOURCE}")
else()
    set(provider_source "auth_software_provider.c")
endif()
idf_component_register(SRCS "${provider_source}" INCLUDE_DIRS "include"
                       REQUIRES auth_port log mbedtls esp_driver_i2c)
