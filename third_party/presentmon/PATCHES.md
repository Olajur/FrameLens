# Vendored PresentData — local changes

Source: https://github.com/GameTechDev/PresentMon (MIT, © Intel Corporation; NVIDIA for NVIDIA-specific files).
Upstream commit: see `UPSTREAM_COMMIT`. Only `PresentData/` is vendored.

## Changes to upstream files (keep this list minimal)

1. `PresentData/TraceLogging.cpp` — `#include "ETW\Nvidia_PCL.h"` → `"ETW/Nvidia_PCL.h"` (portable path separator).
2. `PresentData/GpuTrace.hpp` — `"etw/Microsoft_Windows_DxgKrnl.h"` → `"ETW/..."` (case-correct path).
3. Removed `PresentData.vcxproj*` (FrameLens builds with CMake).

## Added files (FrameLens-owned, replace upstream CommonUtilities)

- `IntelPresentMon/CommonUtilities/log/Log.h` — minimal logging shim (no cereal/boost).
- `IntelPresentMon/CommonUtilities/Exception.h` — minimal exception shim.
- `IntelPresentMon/CommonUtilities/PrecisionWaiter.h` — Sleep-based waiter (only used for ETL-file pacing, which FrameLens never does) + QPC helpers.
- `compat/CompatShims.cpp` — implementations.

## Copied unchanged from upstream CommonUtilities (MIT)

- `Hash.h`, `Meta.h`, `SampleStatistics.h`

## Updating

Replace `PresentData/` from a new upstream tag, re-apply changes 1–2, rebuild with MSVC. If the build fails, the usual cause is a new CommonUtilities symbol used by PresentData — add it to the shims.
