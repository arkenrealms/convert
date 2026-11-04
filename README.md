# Arken Convert

Seven MCP tools for image conversion. No account, wallet or cloud service required.
Files stay on the machine running the server.

## Build

Requires CMake 3.25+, a C++20 compiler and a POSIX host. Native qualification is
performed on macOS; Windows support is not included.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

## Connect

Select one existing physical folder in the client configuration. The server can
read files and create outputs there; it cannot change that folder through a tool call.

```json
{
  "mcpServers": {
    "convert": {
      "command": "/absolute/path/to/convert/build/bin/arken-convert-commands",
      "args": ["--stdio", "--root", "/absolute/path/to/images"]
    }
  }
}
```

Discover with `tools/list`, then call a tool with relative paths. For example:

```json
{"name":"png_to_jpeg","arguments":{"source_path":"photo.png","output_path":"photo.jpg","jpeg_quality":95,"alpha_policy":"flatten_white"}}
```

## Tools and limits

- `convert_image`: PNG, JPEG, BMP, TGA or supported DDS to PNG, JPEG, BMP or TGA.
- `png_to_jpeg`, `jpeg_to_png`, `bmp_to_png`, `bmp_to_jpeg`, `tga_to_png`, `dds_to_png`.

The generic tool also takes `input_format` and `output_format`. JPEG output accepts
`jpeg_quality` (1–100, default 95) and `alpha_policy` (`preserve` or `flatten_white`).
Transparency is never discarded without explicit consent. Other outputs preserve
admitted alpha and do not accept JPEG options.

Input and output are each limited to 8 MiB; images to 1,048,576 pixels and 4096
pixels per dimension. Paths must stay inside the selected folder. Symlinks,
parent traversal and nonregular inputs are rejected. Output parents must already
exist. Existing files are never overwritten, including on repeat calls.

Results include the relative output path, media type, dimensions, byte count and
SHA-256. A rare finalization failure after publication explicitly reports that the
output exists: inspect its path and digest before retrying.

The stdio server uses MCP 2025-11-25. `--catalog` prints its generated catalogue;
`--version` exits without opening a folder. Hosted upload/download, payments,
Cloudflare, revenue and aggregate services are separate monorepo integrations.

Waygate preserves selected source history. Its native refresh derives dependencies
from the build and independently qualifies updates before applying them. Third-party
notices remain with their owners; this package grants no new first-party licence.

## HEIC development prerequisites

The pinned libheif 1.23.7 / libde265 1.1.3 decoder and its qualification project
are included for ongoing HEIC work. They do not add HEIC to the tool catalogue.

```sh
cmake -S capsule/arken_image_codecs_v1/tests/heif_dependencies -B build-heif -DCMAKE_BUILD_TYPE=Release
cmake --build build-heif --target arken-heif-dependency-check -j 4
ctest --test-dir build-heif --output-on-failure
```

Recipes admit macOS, Linux and the separately qualified browser target. Archive
pins, decoder selection, normative deblocking/SAO and memory-only browser patches
are preserved. Portable exact-file replacements live under
`sdk/arken_sdk_dev/external_packages/recipes/patches/generated/`;
the small original diffs remain as authoring references. LGPL notices stay in the
verified upstream packages. Actual platform and product-format qualification
must precede advertising HEIC support.
