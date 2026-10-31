idf_component_register(
    SRCS "airplay_rtsp_codec.c" "airplay_rtsp_pairing.c" "airplay_bplist.c"
         "airplay_mfi_sap.c"
    INCLUDE_DIRS "include"
    REQUIRES probe_crypto airplay_pairing auth_port esp_common mbedtls
)
