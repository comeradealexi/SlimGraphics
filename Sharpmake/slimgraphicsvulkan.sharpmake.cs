using Sharpmake;
using System;
using System.IO;

namespace SlimEngine
{
    [Sharpmake.Generate]
    public class SlimGraphicsVulkan : SlimEngineBaseProject
    {
        public SlimGraphicsVulkan() : base("SlimGraphicsVulkan")
        {
            SourceRootPath = Path.Join(Globals.Root, "SlimGraphicsVulkan");
            SourceFiles.Add(Globals.GetThisFilePath());
        }

        public override void ConfigureAll(Configuration conf, SlimEngineTarget target)
        {
            base.ConfigureAll(conf, target);
            conf.SolutionFolder = "SlimEngine";

            conf.IncludePaths.Add(SourceRootPath);
            conf.IncludePaths.Add(Path.Join(Globals.SubmodulesPath, @"D3D12MemoryAllocator\include"));
            conf.IncludePaths.Add(Path.Join(Globals.SubmodulesPath, @"Vulkan-Hpp"));
            conf.IncludePaths.Add(Path.Join(Globals.SubmodulesPath, @"Vulkan-Hpp\Vulkan-Headers\include"));
            conf.IncludePaths.Add(Path.Join(Globals.Root, @"SlimEngine"));
            conf.IncludePaths.Add(Path.Join(Globals.Root, @"SlimGraphicsShared"));

            conf.AddPrivateDependency<SlimEngineProject>(target);
            conf.AddPrivateDependency<ImGuiProject>(target);
            conf.AddPrivateDependency<D3D12Headers>(target);
            conf.AddPrivateDependency<DirectXToolkitProject>(target);
        }
    }
}