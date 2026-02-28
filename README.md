# Slim Graphics
A lightweight graphics engine to serve as a playground for different graphical techniques and features.

The focus of this project is NOT to try and make the most perfect and optimal rendering abstraction, it's designed for myself to try and make it easy to add new features easily without having to get bogged down in DirectX 12 API specifics. For example, most of the pain surrounding barriers has been kept simple by returning resources to a generic read state most of the time.

## Features

* 3D Model amd texture loading via Assimp
* Mesh shader support
  * Amplification shader support
  * GPU meshlet culling (cone and frustum)
* CPU frustum culling
* Basic post-processing and visualisations
* Built-in magnification tool
* JSON driven shader compiling via C# tool
* Model visualisations
 * vertex & prim order
 * wave intrinsics
 * pixel order
 * meshlet & amplification
* Model cache optimisation (meshoptimizer and DirectXMesh)
* Model simplification (via meshoptimizer)
* Pipeline feedback (Via API and UAV Readback)

## Sub Modules
* Sharpmake
* meshoptimizer
* D3D12MA
* IMGUI & IMPLOT
* DirectXTK
* LodePNG
* DirectXMaths
* stb

## Building & Running
* Run `generate_project_files.bat` in root.
* Open `slimgraphics_vs2022.sln`
* Ensure configuration is set to `x64`
* Build and run `SlimGraphicsApplication` project
