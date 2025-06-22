using Sharpmake;
using System;
using System.IO;

namespace SlimEngine
{
    [Sharpmake.Generate]
    public class DirectXToolkitProject : SlimEngineBaseProject
    {
        public DirectXToolkitProject() : base("DirectXToolkit")
        {
            SourceRootPath = Path.Join(Globals.SubmodulesPath, @"DirectXTK12\Src");
            SourceFiles.Add(Globals.GetThisFilePath());
        }

        public override void ConfigureAll(Configuration conf, SlimEngineTarget target)
        {
            base.ConfigureAll(conf, target);
            conf.SolutionFolder = "SubModules";
            conf.IncludePaths.Add(Path.Join(SourceRootPath, @"..\Inc"));
            conf.IncludePaths.Add(Path.Join(SourceRootPath, @"..\Src"));
            conf.SourceFilesBuildExcludeRegex.Add(@"^(?!.*Geometry)");
        }
    }
}