using Sharpmake;
using System;
using System.IO;

namespace SlimEngine
{
    [Sharpmake.Generate]
    public class D3D12MAProject : SlimEngineBaseProject
    {
        public D3D12MAProject() : base("D3D12MA")
        {
            SourceRootPath = Path.Join(Globals.SubmodulesPath, "D3D12MemoryAllocator");
        }

        public override void ConfigureAll(Configuration conf, SlimEngineTarget target)
        {
            base.ConfigureAll(conf, target);
            conf.SolutionFolder = "SubModules";

            conf.IncludePaths.Add(Path.Join(SourceRootPath, "include"));

            conf.SourceFilesBuildExclude.Add("src/Tests.cpp");
        }
    }
}