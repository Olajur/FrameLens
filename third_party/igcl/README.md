# Intel Graphics Control Library (IGCL) — vendored

Source: https://github.com/intel/drivers.gpu.control-library, commit `b6c462933502e13d1537dd5024949a51be30e63d 2026-08-18`.

Files: `include/igcl_api.h`, `Source/cApiWrapper.cpp` (unmodified), `License.txt`, `third-party-software.txt`.

**License:** Intel Software License Agreement (see `License.txt`), **not** MIT. It allows using and
redistributing these files solely for use on Intel platforms, keeping Intel's notice. FrameLens's own
code stays MIT and only calls IGCL for Intel GPUs (`src/sensors/IgclBackend.cpp`). The runtime
(`ControlLib.dll`) is part of the Intel graphics driver and is loaded from System32 only; it is not
shipped with FrameLens.
