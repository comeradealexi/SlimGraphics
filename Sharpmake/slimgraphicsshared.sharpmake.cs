using Sharpmake;
using System;
using System.IO;

namespace SlimEngine
{
    [Sharpmake.Generate]
    public class SlimGraphicsShared : SlimEngineBaseProject
    {
        public SlimGraphicsShared() : base("SlimGraphicsShared")
        {
            SourceRootPath = Path.Join(Globals.Root, "SlimGraphicsShared");
            SourceFiles.Add(Globals.GetThisFilePath());
        }

        public override void ConfigureAll(Configuration conf, SlimEngineTarget target)
        {
            base.ConfigureAll(conf, target);
            conf.SolutionFolder = "SlimEngine";

            conf.IncludePaths.Add(SourceRootPath);
            conf.IncludePaths.Add(Path.Join(Globals.Root, @"SlimEngine"));
            conf.IncludePaths.Add(Path.Join(Globals.Root, @"SlimGraphicsShared"));
           
            conf.AddPrivateDependency<SlimEngineProject>(target);
            conf.AddPrivateDependency<SlimGraphicsD3D12>(target);
        }
    }
}