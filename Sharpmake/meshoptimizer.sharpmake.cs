using Sharpmake;
using System;
using System.IO;

namespace SlimEngine
{
    [Sharpmake.Generate]
    public class MeshOptimizerProject : SlimEngineBaseProject
    {
        public MeshOptimizerProject() : base("meshoptimizer")
        {
            SourceRootPath = Path.Join(Globals.SubmodulesPath, "meshoptimizer/src");
        }

        public override void ConfigureAll(Configuration conf, SlimEngineTarget target)
        {
            base.ConfigureAll(conf, target);
            conf.SolutionFolder = "SubModules";
            conf.IncludePaths.Add(SourceRootPath);
        }
    }
}