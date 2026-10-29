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
payment transitions, recovery, journal policy, x402, public admission, provider
signatures and production startup. Paid-request qualification currently uses
the pinned macOS OpenSSL crypto package; it does not include deployment tools
or live network probes. Other service-only builds can disable the checks with
`-DARKEN_CONVERT_MCP_CHECKS=OFF`.
The checks do not contact a payment provider or spend funds.

## Worker modules

Resolve the pinned native WASI toolchain, then build the service and journal
report modules. This does not require Node.js or Python.

```sh
cmake -S sdk/arken_sdk_dev/cmake/native_browser_cpp -B build-toolchain -DBUILD_TESTING=OFF
cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/build-toolchain/toolchain.cmake" -DARKEN_CONVERT_MCP_CHECKS=OFF
cmake --build build-wasm --parallel 2 --target \
  arken-file-conversion-service-module arken-conversion-journal-report-module
```

After the native checks have generated `build/payment-packets.json`, run the
network-denied Worker protocol and Durable Object restart checks:

```sh
cmake -S capsule/arken_file_conversion_service_v1/tests/driver -B build-worker \
  -DCONVERSION_WASM="$PWD/build-wasm/capsules/arken_file_conversion_service_v1/arken-file-conversion-service-module.wasm" \
  -DCONVERSION_PAYMENT_PACKETS="$PWD/build/payment-packets.json" \
  -DCONVERSION_PAYMENT_ACTORS="$PWD/build/payment-actor-fixtures.json" \
  -DCONVERSION_X402_FIXTURES="$PWD/build/paid-fixtures/x402-fixtures.json" \
  -DCONVERSION_RECONCILIATION_FIXTURES="$PWD/build/paid-fixtures/reconciliation-fixtures.json" \
  -DCONVERSION_PUBLIC_ADMISSION_FIXTURES="$PWD/build/paid-fixtures/public-admission-fixtures.json" \
  -DCONVERSION_PROVIDER_FIXTURES="$PWD/build/paid-fixtures/provider-fixtures.json" \
  -DCONVERSION_STARTUP_FIXTURES="$PWD/build/paid-fixtures/startup-fixtures.json" \
  -DCONVERSION_SOURCE_PACKAGE_TOOL="$PWD/build/arken-convert-source-package" \
  -DCONVERSION_REPORT_MODULE="$PWD/build-wasm/capsules/arken_file_conversion_service_v1/arken-conversion-journal-report-module.wasm" \
  -DCONVERSION_PAYMENT_TESTS="$PWD/build/arken-conversion-payment-tests"
ctest --test-dir build-worker --output-on-failure
```

This checks native/Wasm protocol parity and paid-flow persistence across two
Workerd processes using real local Durable Object storage. Facilitator, chain
and object-store effects use fixtures; this is not proof of a real purchase.
The current-source journal check also exercises protected reporting, pagination,
revocation, quota actors and unchanged ledger storage across restart. Its package
and read-only report evidence are separate from predecessor-backed deployment
qualification. The source attempt-guard check verifies missing/unavailable/denied
budgets before body consumption, preserves named exports and responses, and
reserves admitted requests once, including invalid calls. Native financial-policy
checks run too. The shared budget service remains a required external binding.
Local checks do not grant deployment authority or verify live policy, resource
costs or profit. Guard evidence must be outside temporary directories; set
`CONVERSION_GUARD_EVIDENCE_ROOT` to a durable directory if your build is temporary.
Nothing is deployed. No credentials are included, and this repository does not
grant a new license to first-party code. Preserve the existing runtime notice and
the pinned dependencies' license files when distributing modules.

## Updating this repository

Waygate generates the file inventory from actual build dependencies and the
native package writer's runtime input report. Do not edit
the generated file lists or copy the whole monorepo into this package.

The separately built native `arken-waygate-refresh` tool uses the monorepo's
`capsule/arken_waygate_service_v1/recipes/refresh/convert.json` profile. Supply a
clean, committed source revision and a fresh output directory. It builds and
tests both the source and an isolated candidate before preparing an update.

```sh
arken-waygate-refresh --source "$UDE" --commit "$REV" \
  --profile capsule/arken_waygate_service_v1/recipes/refresh/convert.json \
  --output "$RUN" --cc "$CC" --cxx "$CXX" \
  --destination "$PWD" --application-output "$APPLY_RUN" \
  --expected-head "$(git rev-parse HEAD)" --previous-prepared "$PREVIOUS_RUN"
```

For the first refresh of an existing repo, replace `--previous-prepared` with
`--previous-recipe` and `--previous-commit` from its accepted Waygate baseline.
Later runs use the preceding prepared directory. Retain those receipts and their
qualification artifacts. Failed qualification never applies an update; a stale
destination or independent mapped edit is rejected rather than overwritten.

Reverse contributions use the same prepared mapping through the explicit
`--operation reverse` command. They require a frozen standalone commit and the
monorepo's expected HEAD. A reverse import is not a deployment or a claim that
the contributed code has passed a new build. No command pushes automatically.
