using Sharpmake;
using System;
using System.IO;

namespace SlimEngine
{
    [Sharpmake.Generate]
    public class SlimGraphicsD3D12 : SlimEngineBaseProject
    {
        public SlimGraphicsD3D12() : base("SlimGraphicsD3D12")
        {
            SourceRootPath = Path.Join(Globals.Root, "SlimGraphicsD3D12");
        }

        public override void ConfigureAll(Configuration conf, SlimEngineTarget target)
        {
            base.ConfigureAll(conf, target);
            conf.SolutionFolder = "SlimEngine";

            conf.IncludePaths.Add(SourceRootPath);
            conf.IncludePaths.Add(Path.Join(Globals.SubmodulesPath, @"D3D12MemoryAllocator\include"));
            conf.IncludePaths.Add(Path.Join(Globals.SubmodulesPath, @"DirectX-Headers\include\directx"));
            conf.IncludePaths.Add(Path.Join(SourceRootPath, @"winpixeventruntime.1.0.231030001\Include\WinPixEventRuntime"));
            conf.IncludePaths.Add(Path.Join(Globals.Root, @"SlimEngine"));
            conf.IncludePaths.Add(Path.Join(Globals.Root, @"SlimGraphicsShared"));

            conf.AddPrivateDependency<SlimEngineProject>(target);
            conf.AddPrivateDependency<ImGuiProject>(target);

        }
    }
}