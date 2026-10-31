idf_component_register(SRCS "box_decode_profile.c" "box_perf.c" "box_direct.c" "box_pixels.c" "main.c" "probe_ui.c" "usb_descriptors.c" "probe_auth.c" "probe_identity.c" "probe_network.c" "probe_dns.c" "probe_discovery.c" "probe_receiver.c" "probe_trace.c" INCLUDE_DIRS "."
    REQUIRES esp_app_format airplay_capture_core ed25519 probe_crypto box_bsp sub0_decoder esp_tinyusb iap2_core auth_software_provider auth_port iap2_mfi_auth esp_wifi esp_netif esp_event esp_http_server esp_timer lwip)
# esp_tinyusb owns normal descriptor strings. Interpose just the documented
# Microsoft OS 1.0 index, which lies beyond its fixed string array.
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=tud_descriptor_string_cb")
