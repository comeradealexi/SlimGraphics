using Sharpmake;
using System;
using System.IO;

namespace SlimEngine
{
    [Sharpmake.Generate]
    public class LodePNGProject : SlimEngineBaseProject
    {
        public LodePNGProject() : base("LodePNG")
        {
            SourceRootPath = Path.Join(Globals.SubmodulesPath, "lodepng");
            SourceFiles.Add(Globals.GetThisFilePath());
        }

        public override void ConfigureAll(Configuration conf, SlimEngineTarget target)
        {
            base.ConfigureAll(conf, target);

            conf.SolutionFolder = "SubModules";
            conf.IncludePaths.Add(SourceRootPath);
            conf.SourceFilesBuildExcludeRegex.Add(@"\.*\\(examples)\\");
            conf.SourceFilesBuildExcludeRegex.Add(@"lodepng_benchmark.cpp");
        }
    }
}