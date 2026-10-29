# Arken Convert MCP

Paid, bounded image conversion through MCP. This package contains the conversion
service and its dependencies, not the Convert website, Canvas, or Prism.

The service exposes upload preparation, generic image conversion, and named
PNG/JPEG/BMP/TGA/DDS conversions. Immutable upload options, private file/result
capabilities, payment recovery, journal reports, and usage attribution stay in
their existing service owners. The cloud limits are distinct from the larger
on-device conversion profiles.

## Native build

Use CMake 3.25+ and a C++20 compiler:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Link `arken::convert_mcp`. The native checks cover service contracts, image output,
payment transitions, recovery, and journal policy. They do not contact a payment
provider or spend funds.

Worker packaging and runtime qualification remain separate from native library
acceptance. A source package or successful native build is not deployment
authorization. No credentials are included, and no new first-party license is
granted by this repository.
