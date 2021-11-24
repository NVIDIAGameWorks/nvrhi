# NVRHI

[![Build Status](https://github.com/NVIDIAGameWorks/nvrhi/actions/workflows/build.yml/badge.svg)](https://github.com/NVIDIAGameWorks/nvrhi/actions/workflows/build.yml)

## Introduction

NVRHI (**NV**IDIA **R**endering **H**ardware **I**nterface) is a library that implements a common abstraction layer over multiple graphics APIs (GAPIs): Direct3D 11, Direct3D 12, and Vulkan 1.2. It works on Windows (x64 only) and Linux (x64 and ARM64).

Key features:

- Automatic tracking of resource states and barrier placement (optional).
- Automatic tracking of resource usage and lifetime, deferred and safe resource destruction.
- Convenient and efficient resource binding model with little runtime overhead.
- Easy direct interaction with the underlying GAPI when necessary.
- Easy portability of the rendering code between the supported GAPIs.
- Hidden sub-allocation of upload buffers and versioning of constant buffers.
- Parallel command list recording and multi-queue rendering.
- Supports all types of pipelines: Graphics, Compute, Ray Tracing, and Meshlet.
- Validation layer and resource reflection for easy debugging.

Various early versions of NVRHI have been used in various projects created at NVIDIA, including:

- [Asteroids demo](https://developer.nvidia.com/blog/using-turing-mesh-shaders-nvidia-asteroids-demo)
- [DLSS SDK](https://developer.nvidia.com/dlss)
- [VRWorks](https://developer.nvidia.com/vrworks)
- [VXGI](https://developer.nvidia.com/vxgi)
- [WaveWorks](https://developer.nvidia.com/waveworks)

## Requirements

* Windows or Linux (x64 or ARM64)
* CMake 3.10
* A C++ 17 compiler (Visual Studio 2019, GCC 8 or Clang 6)
* Windows SDK version 10.0.19041.0 or later for DX12 support

## Building NVRHI

NVRHI can be configured to be used a set of static libraries in CMake-based projects, or as a single dynamic library.

To include NVRHI into a CMake project as static libraries:

1. Add this repository as a submodule.
2. Add a `add_subdirectory(nvrhi)` directive to the parent CMakeLists.txt.
3. Add dependencies to the necessary targets: 
	* `nvrhi` for the interface headers, common utilities, and validation;
	* `nvrhi_d3d11` for DX11 (enabled when `NVRHI_WITH_DX11` is `ON`);
	* `nvrhi_d3d12` for DX12 (enabled when `NVRHI_WITH_DX12` is `ON`); and
	* `nvrhi_vk` for Vulkan (enabled when `NVRHI_WITH_VULKAN` is `ON`).

To build NVRHI as a shared library (DLL or .so):

1. Clone this repository recursively (including submodules).
2. Generate the project with CMake:
	* Set the `NVRHI_BUILD_SHARED` variable to `ON`.
	* Make sure to set the target platform to a 64-bit one. 32-bit builds are not supported.
3. Build and install as normal.

## Using NVRHI in Applications

See the [programming guide](doc/ProgrammingGuide.md) and the [tutorial](doc/Tutorial.md).

## Shader Compiler

NVRHI includes an optional tool for compiling shaders and generating shader permutations. The shader compiler is normally built together with NVRHI when the `NVRHI_WITH_SHADER_COMPILER` variable is `ON`. It can be used from CMake rules by its target name `shaderCompiler` or directly from its install location using the executable name `nvrhi-scomp`.

The NVRHI shader compiler is just a front-end for [DXC](https://github.com/microsoft/DirectXShaderCompiler), it does not implement any shader language processing itself.

For the list of command line options, run `nvrhi-scomp --help`.

The shader compiler is driven by a configuration file that lists shaders, their targets and permutations. The file has one shader source per line, for example:
```
Shaders.hlsl -T vs_5_0 -E main_vs
Shaders.hlsl -T ps_5_0 -E main_ps -D ENABLE_SOMETHING={0,1}
```

The above configuration will compile 3 shaders total: one vertex shader `main_vs` and two permutations of pixel shader `main_ps` with different values of the `ENABLE_SOMETHING` define. The pixel shader permutations will be combined into a single permutation blob. Permutation blobs can be parsed using functions declared in `<nvrhi/common/shader-blob.h>`.

## NVAPI Support

NVRHI includes optional support for certain DX11 and DX12 extensions available through the NVAPI library. The library is not distributed with NVRHI but is available separately [here](https://developer.nvidia.com/nvapi).

To enable NVAPI support, extract the NVAPI SDK into the `nvapi` subfolder of your main project and set the `NVRHI_WITH_NVAPI` CMake variable to `ON`.

The following extensions are supported:

- Fast Geometry Shader with optional coordinate swizzling (Maxwell+)
- Single Pass Stereo (Pascal+)
- HLSL Extensions through a fake UAV slot (see [this blog post](https://developer.nvidia.com/unlocking-gpu-intrinsics-hlsl))

## RTXMU Integration

NVRHI includes an optional integration of the [RTXMU](https://github.com/NVIDIAGameWorks/RTXMU) library. The library is included as a git submodule, and can be enabled with the `NVRHI_WITH_RTXMU` CMake variable.

When RTXMU integration is enabled, all bottom-level ray tracing acceleration structures (BLAS'es) are managed by that library. All built BLAS'es that have the `AllowCompaction` flag set are automatically compacted when `ICommandList::compactBottomLevelAccelStructs` method is called. No other configuration is necessary.

## License

NVRHI is licensed under the [MIT License](LICENSE.txt).