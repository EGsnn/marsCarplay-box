# Authentication boundary

The public default registers no identity and returns `ESP_ERR_NOT_SUPPORTED`. It can build and run the display/network diagnostics, but cannot authenticate to a real iPhone. No certificate or private key is shipped or fetched.

To integrate an implementation you are authorized to use, pass `--auth-provider /absolute/path/provider.c` to `code/build.py`. Implement the three functions declared in `include/auth_software_provider.h`. During initialization, use `auth_port_register_provider()` with certificate/signing callbacks, protocol version and opaque context. Manage signing/key storage in that implementation or its hardware, outside this repository. Default component requirements include auth_port, log, mbedtls and I2C; additional dependencies require explicit local integration.

Do not commit keys, certificate assets or resulting identity-bearing binaries. Publication of source is separate from accessory certification. Historical version records describe local experiments and do not grant authentication assets or certification.
