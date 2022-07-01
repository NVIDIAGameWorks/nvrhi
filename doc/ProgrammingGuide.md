# NVRHI Programming Guide

NVRHI programming model is mostly a blend between DX11 and DX12, with a flavor of Vulkan. Unlike the modern GAPIs, the library tracks the resources created by the application, where they are used, when the GPU work using the resources finishes, and when it's safe to release the resources. The library also implements resource state tracking and automatic barrier placement, although it needs some hints to do so and maintain support for multiple command lists that might use the same resource. Finally, the library implements automatic handling of GPU data uploads and ray tracing acceleration structure build scratch buffers.

Compared to DX11, the API is more coarse-grained, more like DX12 or Vulkan: there are pipeline state objects (PSOs), graphics state structures, and binding layouts and sets. Issuing some draws or dispatches takes fewer API calls than it does on any of the three supported GAPIs, and more structure filling. This allows the library to implement efficient state caching and reduce the number of calls to the underlying GAPIs.

## Resources

All resources (pipelines, textures, etc.) provided by NVRHI are descendants of the `IResource` class, which implements the `AddRef` and `Release` methods, following the COM model used in DX11/12. Resources implement reference counting and are destroyed when the reference count reaches zero. Note that there are often internal references from the Device or the CommandList to the resources: these references are used to defer destruction of the resources until they are no longer used by the GPU. Actual destruction of the resources released by the application that were in use at the time of final release is performed in `IDevice::runGarbageCollection()`, which is supposed to be called at least once per frame.

To automate the process of reference counting, NVRHI provides a template class `RefCountPtr<T>`, same as `ComPtr` provided by WRL ([Windows Runtime C++ Template Library](https://docs.microsoft.com/en-us/cpp/cppcx/wrl/windows-runtime-cpp-template-library-wrl)). All resource types have "handles" defined as reference counting pointers to those types, such as `typedef RefCountPtr<ITexture> TextureHandle`.

As a consequence of this model, any function that accepts a resource pointer is able to convert such pointer into a handle and keep a strong reference to the resource. This is in contrast with `std::shared_ptr` and `std::weak_ptr` or raw pointers, where a function needs to accept a `shared_ptr` to keep a strong reference.

Combined with automatic resource lifetime tracking, the typical usage for resource handles is to make them members of a class that represents a render pass or a similar entity. When the pass is no longer needed, it's destroyed by the application, automatically releasing all resources that it owns, and these resources are destroyed by the library at a later time. Similarly, NVRHI supports a "fire and forget" model: when some render pass only needs to happen once, it is valid to create resources and even pipelines in local scope, record the draw commands into a command list, maybe execute that command list, and just exit the scope.

Another important method of `IResource` is `getNativeObject`. This method returns any underlying GAPI pointer or handle for the specified resource and object type, if it's applicable and available. For example, `getNativeObject` can be used to get `ID3D12Device` from `IDevice`, or `VkImage` from `ITexture`.

## Device

The `IDevice` interface provides methods that operate on the underlying device or command queues. This includes things like creating textures, buffers, pipelines and binding sets, executing command lists, and performing timer and event queries. There are no draw or dispatch methods in `IDevice`, those are all in `ICommandList`. Also note that there are no methods to destroy resources: as explained above, resources are destroyed when their reference count (both internal and external) reaches zero.

NVRHI does not provide any functionality to create the underlying GAPI device(s); that is the responsibility of the application. To create an `IDevice` interface over the existing device, use the GAPI-specific functions provided by the backends:

* `nvrhi::d3d11::createDevice` defined in `<nvrhi/d3d11.h>`,
* `nvrhi::d3d12::createDevice` defined in `<nvrhi/d3d12.h>`,
* `nvrhi::vulkan::createDevice` defined in `<nvrhi/vulkan.h>`,
* `nvrhi::validation::createDevice` defined in `<nvrhi/validation.h>`.

As there is no separate abstraction for command queues, up to 3 queues must be provided at the time of `IDevice` creation on DX12 or Vulkan: the graphics, compute, and copy queues. The graphics queue is required, the rest are optional.

The validation device is a wrapper that can be created around another `IDevice` instance. It implements the same interface and essentially intercepts and validates all NVRHI API calls before executing them. The validation device will also wrap every command list that the application creates with a similar validation layer. All messages from the validation layer are passed to the `messageCallback` interface provided to the underlying device at the time of its creation. 

## Command List

The `ICommandList` interface provides methods that go directly into a command list, such as state manipulation and draw or dispatch commands. Command lists are created using `IDevice::createCommandList` and executed using `IDevice::executeCommandList`. The command list must be opened with `open()` before recording any commands, and closed with `close()` before being executed. It is valid (though not really tested) to close the command list and then open it again without executing. It is also valid to record multiple command lists concurrently and then execute them in any order.

Since this model is not well supported on DX11, there is a special kind of command list called "immediate" that maps to the immediate command list on DX11. The application may create multiple immediate command lists, but only one of them may be open at a time. The immediate command lists still need to be explicitly executed to function on DX12 and Vulkan.

On DX12 and Vulkan, NVRHI command lists do not map to GAPI command lists 1:1, they aggregate more resources in order to make the programming model easier to use. One command list will typically keep multiple GAPI command lists and use them in a round-robin fashion if the previously recorded instance of the command list is still being executed when the command list is re-opened. Therefore, it is valid to record and execute a command list, then immediately open it again and start recording new commands. Additionally, the command lists handle texture and buffer writes: the `writeTexture` and `writeBuffer` methods behave similarly to DX11's `UpdateSubresource` through an upload manager that keeps a set of upload buffers and tracks their usage. In a similar fashion, the command lists also manage scratch buffers for ray tracing acceleration structure builds. Note that these upload and scratch managers never shrink their working set, so if it's necessary to release the memory after uploading a large set of textures or building many BLAS'es, the only option is to release the command list that was used for that activity, and create a new one.

## State Tracking and Barriers

NVRHI command lists implement resource state tracking and barrier placement. Since a command list may be recorded in parallel with other command lists, and then executed out of order, tracking resource states across command list boundaries at record time is impossible. The command list must know which state each referenced resource is in when it enters the command list, and which state to leave each resource in when exiting the command list. There are 3 ways to achieve that:

1. Use the `beginTrackingTexture/BufferState` methods to provide the prior state information to the command list per-resource explicitly, and `setTexture/BufferState` at the end of the command list to transition the resources to the desired states.
2. Create textures and buffers with the `keepInitialState` descriptor field set to `true`. For such resources, a command list will assume that the resource enters the command list in its `initialState`, and will transition the resource back to its `initialState` at the end.
3. Make some resources' states permanent by calling `setPermanentTexture/BufferState`. This is useful for static resources like material textures and vertex buffers: after initialization, their contents never change, and they can be kept in a `ShaderResource` or similar state without ever being transitioned. Permanent resources do not require any state tracking and are therefore cheaper on the CPU side.

As part of state tracking, NVRHI will place UAV barriers between successive uses of the same resource in `UnorderedAccess` state. That might not always be desired: for example, some rendering methods address the same texture as a UAV from the pixel shader, and do not care about ordering of accesses for different meshes. For such use cases, the command list provides the `setEnableUavBarriersForTexture/Buffer(bool enable)` methods that can be used to temporarily remove such UAV barriers. On DX11, these methods map to `NVAPI_D3D11_Begin/EndUAVOverlap` calls. Conversely, it is sometimes necessary to place UAV barriers more often than NVRHI would do it, which is at every `setGraphicsState` or similar call. For example, there may be a sequence of compute passes operating on a buffer that use the same shader but different constants. As updating constants does not require a call to one of those state setting functions, an automatic barrier will not be placed. To place a UAV barrier manually, use the `nvrhi::utils::texture/bufferUavBarrier` functions.

Additionally, automatic barrier placement can be disabled completely using the `setEnableAutomaticBarriers` method. Command lists are initialized with automatic barriers enabled, and the state set using this method is kept when the command list is closed and re-opened. So, applications may disable automatic barriers in a command list upon creation and for its lifetime, or disable it temporarily for performance-critical sections. When running in this "manual" mode, all resource state manipulation must be performed by the application. The basic functions provided for that are `setTexture/BufferState`. NVRHI also provides some convenience functions: `setResourceStatesForBindingSet` and `setResourceStatesForFramebuffer`. Note that all these functions only place barriers into an internal accumulator; in order to push them onto the GAPI command list, call the `commitBarriers` method.

## Synchronization

Although NVRHI manages resource lifetime and reuse hazards, sometimes it is necessary to explicitly synchronize the CPU with the GPU, or synchronize GPU workloads on different queues. There are a few synchronization features available:

1. Event queries - these are objects created using `IDevice::createEventQuery`. Event queries can be set on a command queue using the `IDevice::setEventQuery` method, and then waited for on the CPU using the `IDevice::waitEventQuery` method.

2. Wait for Idle. Use the `IDevice::waitForIdle` method which waits for all queues to finish executing their commands.

3. Inter-queue synchronization, which is provided using the `IDevice::queueWaitForCommandList` method. That method accepts an "instance" parameter, which should receive the value previously returned by `IDevice::executeCommandList`. 

## Buffers

There are two kinds of buffers, both represented by the same `IBuffer` interface: regular buffers and volatile constant buffers. These are differentiated by the `isVolatile` flag in the `BufferDesc` structure. All buffers are created using the `IDevice::createBuffer` method. To use a buffer created outside NVRHI, call `IDevice::createHandleForNativeBuffer`.

Regular buffers directly map to GAPI buffer objects, and they can be CPU-accessible too. Such buffers can be used as shader resources, UAVs, index or vertex buffers, etc., can be copied to and from, and written into from the CPU using the `ICommandList::writeBuffer` method. Normally buffers are created as committed resources (using the DX12 terminology), but they can also be placed into a heap if created with the `isVirtual` flag. In the latter case, the application must call `IDevice::bindBufferMemory` before using the buffer in any way.

Volatile constant buffers (VCB) are a special feature of NVRHI. They provide an easy and lightweight interface to upload constant buffers that are used for one or a few draw calls per frame. Semantically, a VCB is considered non-existent until the first call to `writeBuffer` is made for this VCB; then it exists until the command list is closed, and the new instance of a command list needs to write into the VCB again. A command list may write into the same VCB multiple times, and the writes will update the VCB contents visible by shaders in API order. Volatile constant buffers can only be bound to binding layout items of the same kind, i.e. `ResourceType::VolatileConstantBuffer`.

The implementation of VCBs is different for each GAPI:

* On DX11, VCBs directly map to buffers with the `D3D11_USAGE_DYNAMIC` usage. This means the contents of a VCB will persist over command list instances, but clearing them would be doing extra work.
* On DX12, VCB instances are sub-allocated from the command list upload buffer on each call to `writeBuffer`, and bound to the shaders using root constant buffer views.
* On Vulkan, VCBs are regular buffers that keep multiple versions of the data, and therefore are multiple times larger than the buffer size declared in `BufferDesc`. On each call to `writeBuffer`, a new version is written; the use of each version by the GPU is tracked, and old versions are reused automatically. Since the buffer is statically sized, the number of versions to use must be predicted by the application as `numberOfUsesPerFrame * numberOfFramesInFlight` and provided to NVRHI through `BufferDesc::maxVersions` at buffer creation time. VCBs with insufficient version counts will generate a runtime error. VCBs are bound to the shaders using dynamic descriptors with offsets.

## Push Constants

Push constants are a more lightweight version of volatile constant buffers that is useful for very small amounts of data. They have a size limit of 128 bytes, and no resource object. To use push constants, the application must declare them both in the binding layout and the binding set, and then call `ICommandList::setPushConstants` after setting the graphics or compute state.

Push constants are implemented as a single small dynamic buffer on DX11, as root constants on DX12, and as push constants on Vulkan.

## Textures

NVRHI supports textures of any dimension, i.e. 1D, 1DArray, 2D, 2DArray, 2DMS, Cube, CubeArray, 3D - which is specified using the `TextureDesc::dimension` field. Textures are GPU-only, they cannot be read or written by the CPU; for that purpose, there is a special object called "Staging texture". All regular (non-staging) textures are created using the `IDevice::createTexture` method. To use a texture create outside NVRHI, call `IDevice::createHandleForNativeTexture`.

Like buffers, textures can be committed or virtual. Virtual textures are created without backing memory and must be bound to a region of a heap using the `IDevice::bindTextureMemory` method before they are used in any way. Tiled or sparse textures are not supported.

Textures can be written into without the use of a staging texture, using the `ICommandList::writeTexture` method, which behaves similarly to DX11's `UpdateSubresource`.

Staging texture is a special primitive that allows uploading and downloading texture data to and from the GPU, respectively. It can be created using the `IDevice::createStagingTexture` method. Staging textures cannot be used by shaders in any capacity, they can only be copied to and from. On DX11, staging textures are implemented as regular textures with the right usage mode. On DX12 and Vulkan, staging textures are implemented as mappable buffers.

## Binding Layouts and Sets

The NVRHI resource binding system is unlike any of the GAPIs. It has two symmetrical parts: layouts and sets. Binding layouts declare which binding slots will be used by the shaders, and for what resource type. Binding sets are created using an existing layout and provide actual resources to be used by shaders.

Each binding layout consists of a number of items, instances of the `BindingLayoutItem` structure. Each layout item specifies the resource type, such as `Texture_SRV`, `TypedBuffer_UAV`, or `VolatileConstantBuffer`; and the binding slot. The binding slot directly maps to the slot used in HLSL, such as `t#` slots for `SRV` type bindings, `u#` slots for `UAV` type bindings, and so on. For push constants, the layout item must also specify the size of the constant data. Besides the layout items, the binding layout also specifies the shader visibility mask, which is shared for all resources in the layout, and the register space (only valid on DX12).

Binding layouts are used to create pipelines - graphics, compute, meshlet, or ray tracing. When creating a pipeline, a few binding layouts can be specified as part of its descriptor. The layouts are used to build the root signature on DX12 or the pipeline layout on Vulkan. For ray tracing pipelines on DXR, a single local binding layout may also be specified for each generic shader or hit group, and it will translate to the local root signature.

Each binding set consists of the same number of items as the corresponding layout, in the same order. These items are instances of the `BindingSetItem` structure. They also specify the resource type and binding slot - which may seem redundant because this information is already present in the layout, but it improves code readability and bug detection. Besides the binding information, set items also provide pointers to the resources that need to be bound, and additional parameters, depending on the binding type: subresource sets and formats. For example, a `Texture_UAV` type binding may bind only a subset of array slices at a single mip level, and apply a format specification if the texture is typeless. The application does not need to create any view objects for such subresource bindings, that is handled automatically by NVRHI.

Binding sets are used to issue draw and dispatch commands. Before any draw calls can be made, the application must call `ICommandList::setGraphicsState`, `ICommandList::setComputeState` etc. with a state description structure. That description structure includes references to a few binding sets. These binding sets must be in the same order as the layouts that were used to create the pipeline. Binding the resources to the GAPI is very cheap with this model, because all descriptors are pre-filled at the time each binding set is created.

Both binding layouts and sets are immutable, i.e. cannot be modified once they are created.

Binding sets keep strong references to all resources that are used in them. When a binding set is used in a command list, the command list will optionally keep a strong reference to the binding set (which transitively means referencing all resources in the set, but cheaply) until that instance of the command list has finished executing on the GPU, which makes sure that the resources are not destroyed while the GPU is using them. If the application knows that a certain binding set will not be released for a long time, it may set `BindingSetDesc::trackLiveness` to `false`, which makes the command lists not keep any strong references to such binding sets, improving CPU performance. In this case, the application must take care to wait until all GPU commands referencing the binding set are finished before releasing the set.

## Bindless Resources

Modern rendering techniques, most notably ones using ray tracing, need to address resources using dynamic indices, without any static binding layout known beforehand. To support bindless rendering, NVRHI implements bindless layouts and descriptor tables.

Bindless layouts implement the `IBindingLayout` interface, just like regular binding layouts, and can be used in their place when creating pipelines. A bindless layout specifies the shader visibility mask, the register spaces that will be used to bind the same array of the resources (on DX12), and the maximum capacity of the bound descriptor table (on Vulkan). To create a bindless layout, use the `IDevice::createBindlessLayout` method.

Instead of "bindless sets", the runtime counterpart of a bindless layout is a descriptor table. A descriptor table is an untyped array of resource bindings, which has variable size (on DX12) and can be modified after creation. To write a binding into a descriptor table, use the `IDevice::writeDescriptorTable` method; to erase a binding, use the same method with the resource type set to `None`.

Descriptor tables also implement the `IBindingSet` interface and can be used in place of regular binding sets when setting state.

**Note**: descriptor tables do not keep strong references to their resources, and therefore provide no resource lifetime tracking or automatic barrier placement. Applications must take care to synchronize descriptor table writes with GPU work and to ensure the correct state of each referenced resource - most likely, by using only permanent resources in descriptor tables.

## Vulkan Binding Offsets

To enable using the same HLSL shaders on both DX and Vulkan, applications typically cross-compile the shaders into SPIR-V using DXC and apply some offsets to the binding slots, depending on the resource type. The reason to apply these offsets is that Vulkan does not have separate namespaces for different resource types, such as SRVs or UAVs.

NVRHI provides a way to automatically apply these binding offsets when creating the binding layout, using the `VulkanBindingOffsets` structure. The offsets are shared between all shader stages within the layout, but if different offsets are desired per-stage, multiple layouts with disjoint shader visibility masks can be used.

The default offsets used in this structure match the offsets used by the NVRHI shader compiler when targeting SPIR-V.

## Pipelines and States

Like DX12 and Vulkan, NVRHI requires that applications create pipeline state objects that include all shaders, binding layouts, and some other bits of rendering state, such as rasterizer and ROP settings. There are 4 kinds of pipelines supported by NVRHI, ordered by increasing complexity:

1. Compute (`IComputePipeline`). Created with `IDevice::createComputePipeline`, includes a compute shader and binding layouts. 
2. Meshlet (`IMeshletPipeline`). Created with `IDevice::createMeshletPipeline`, includes up to 3 shaders - amplification, mesh, and pixel, and binding layouts. Also includes the rasterizer state (minus the viewports and stencil), depth-stencil state, and blend state.
3. Graphics (`IGraphicsPipeline`), created with `IDevice::createGraphicsPipeline`, includes up to 5 shaders - vertex, hull, domain, geometry, pixel; binding layouts, and the same rendering state as a meshlet pipeline.
4. Ray tracing (`rt::IPipeline`), created with `IDevice::createRayTracingPipeline`, includes many shaders and shader groups, global and local binding layouts, and pipeline settings like maximum recursion depth.

When the pipeline is created, it is immutable. It can only be used to set the rendering state on a command list and issue rendering commands:

1. `ICommandList::setComputeState`, followed by `dispatch` or `dispatchIndirect`.
2. `ICommandList::setMeshletState`, followed by `dispatchMesh`.
3. `ICommandList::setGraphicsState`, followed by `draw`, `drawIndexed`, `drawIndirect`.
4. `ICommandList::setRayTracingState`, followed by `dispatchRays`.

Note that setting the state of one kind invalidated all other kinds of state, e.g. `setComputeState` invalidates the previously set graphics, meshlet, or ray tracing state. The only commands that are safe to use on the command list between state setting and draw or dispatch are `writeBuffer` on volatile constant buffers and `setPushConstants`. Also note that VCBs must be written before they are used in any of the `setState` calls, and writing them after setting the state has an extra cost; in contrast with that, push constants can only be set after the `setState` call.

## Framebuffers

Following the Vulkan API for creating graphics pipelines, NVRHI has a concept of a framebuffer. A framebuffer is a collection of render targets, up to 8, and a depth target, each with its subresource set. Framebuffers hold strong references to their textures and are immutable.

A valid framebuffer is necessary to create a graphics or meshlet pipeline. A pipeline created with a certain framebuffer can then be used with the same framebuffer, or with any other framebuffer which is compatible. Two framebuffers are considered compatible when they have the same number and formats of the render targets, and the width and height of the render targets. These parameters are grouped into the `FramebufferInfo` structure, which is accessible through the `IFramebuffer::getFramebufferInfo` method. If two `FramebufferInfo` structures are equal, their framebuffers are compatible.

## Ray Tracing Support

NVRHI implements support for DXR 1.0 pipelines and acceleration structure handling, and supports DXR 1.1 ray queries through binding acceleration structures to any shader stage. It also implements `KHR_acceleration_structure` and `KHR_ray_tracing_pipeline` support on Vulkan, and supports `KHR_ray_query` in the same way. The API coverage is not 100% at this time, most notably, acceleration structure compaction, growing shader objects, indirect dispatch, and Vulkan shader record buffer are not supported. It should also be noted that local binding layouts and sets are not supported on Vulkan because Vulkan does not have a concept of local bindings, besides the generic SRB.

Acceleration structures (AS) are represented by `IAccelStruct` and created using the `IDevice::createAccelStruct` method. The type of the AS - top or bottom - and its capacity are specified at creation time by providing a descriptor that lists the geometries that will be placed into the AS. Acceleration structure objects hold a data buffer that stores the AS itself, and use a scratch buffer managed by the command list to build. Acceleration structure buffers can be committed or virtual, just like regular buffers, and that is specified using the `isVirtual` flag in the descriptor. Virtual AS have no backing memory initially, and have to be bound to a heap region by calling `IDevice::bindAccelStructMemory` before they can be used.

Since the acceleration structures have an associated buffer whose size is fixed, they cannot be rebuilt to a size that exceeds the limits specified at AS creation; NVRHI will issue an error if such a build is attempted. They can, however, be rebuilt or updated in place, with updates requiring that the `AllowUpdate` build flag is specified at AS creation. The NVRHI validation layer checks this and several other usage constraints, some of which would be missed by the DX12 or Vulkan validation layers, and violating those constraints would lead to GPU page faults.

Acceleration structures are bound to shaders using the same binding layout and set concept as any other resource type, using `ResourceType::AccelStruct`.

Ray tracing pipelines correspond directly to DXR or Vulkan RT pipelines. They consist of a number of generic shaders, each of them named; and a number of hit groups, each of them also named and consisting of any or all of: closest hit shader, any hit shader, and intersection shader. Each generic shader and hit group can have its own (single) local binding layout on DX12; attempting to use local bindings on Vulkan will result in an error. The shaders used in RT pipelines should come from shader libraries, which are created using `IDevice::createShaderLibrary` and then specialized to a particular entry using `IShaderLibrary::getShader`. On Vulkan, shader specializations are also supported.

Once a pipeline is created, a shader table also needs to be created through `rt::IPipeline::createShaderTable` in order to use the pipeline. The shader table object is used to build the SBT at dispatch time, in every instance of the command list that uses the shader table. Unlike the GAPI interfaces that use shader handles or pointers, NVRHI makes the shader table reference shaders and hit groups by their name - which is admittedly slower but is sufficient for the typical use case. The shader table is mutable and versioned, so it is valid to use one version of the shader table in a dispatch command, then modify the shader table, and use it again in the same command list.

To trace some rays using the pipeline method, use `ICommandList::setRayTracingState`, which includes a reference to a shader table; and then use `ICommandList::dispatchRays`.

## Readbacks

Sometimes it is necessary to move data from the GPU to the CPU. It could be contents of a buffer with debug output, or a processed texture such as a lighting probe, or a GPU timer query result. These scenarios are supported by NVRHI.

### Buffers

To read contents of a GPU buffer, first create a staging buffer that will make the data available from the CPU. A staging buffer is created with `BufferDesc::cpuAccess` set to `CpuAccessMode::Read`. Then, in a command list, copy the contents of the GPU buffer or portion thereof to the staging buffer using `ICommandList::copyBuffer`.

Once the command list is submitted for execution, use `IDevice::mapBuffer` to gain access to the contents of the staging buffer. Note that `mapBuffer` is a blocking function, i.e. it will wait until the last command list that accessed the staging buffer completes. If that wait is not desirable, such as when reading profiling data from a real-time renderer without affecting the frame rate, a more elaborate solution with multiple staging buffers is necessary. Cycle through these staging buffers by using a new one on every frame, and only map the buffer when its command list has already finished. Use event queries (`IDevice::pollEventQuery`) to find out if a particular command list has finished executing. Once the data has been copied to CPU memory or otherwise processed, unmap the staging buffer using `IDevice::unmapStagingBuffer`.

### Textures

Similar to buffers, accessing texture data from the CPU requires a staging texture. Staging textures in NVRHI have a separate resource type, `IStagingTexture`, because the implementation of staging textures is significantly different from regular textures and differs between the underlying graphics APIs as well.

Performing a texture readback follows the same logic as buffer readbacks. First, copy the contents of a texture slice (or multiple slices) from the GPU texture to the staging texture using one or multiple `ICommandList::copyTexture` calls. Then map the staging texture using `IDevice::mapStagingTexture`. Note that this function is blocking, similar to `IDevice::mapBuffer`. Once the data has been copied to CPU memory, unmap the staging texture using `IDevice::unmapStagingTexture`.

The data in a staging texture is stored in a pitch-linear layout, meaning that pixels in the same row are densely packed. Rows of pixels are packed with a format-dependent pitch that is returned by `mapStagingTexture` through the `outRowPitch` parameter.

### Timer Queries

Timer queries are represented by `ITimerQuery` type objects and allow measuring time elapsed on the GPU between two points in the same command list with high precision. To use a timer query, first create it using `IDevice::createTimerQuery`, and then use `ICommandList::beginTimerQuery` to mark the beginning of the profiled region and `ICommandList::endTimerQuery` to mark the end. Once the command list completes on the GPU, the timer query data will be available through `IDevice::getTimerQueryTime` which returns the measured duration in seconds. After use, a timer query must be reset using `IDevice::resetTimerQuery` before it can be used in a command list again.

Note that the `IDevice::getTimerQueryTime` function is blocking, meaning it will wait for the command list to finish executing. That's often undesirable, so use the same strategy as with staging buffers, i.e. create multiple timer queries and poll them after a frame or two. Use `IDevice::pollTimerQuery` to find out if the data is already available.

Timer queries do not map directly to DX12 or Vulkan objects. There is an implicit query heap (DX12) or query pool (Vulkan) that is managed by the NVRHI backends. The capacity of this heap/pool is set with `DeviceDesc::maxTimerQueries` at device initialization.
