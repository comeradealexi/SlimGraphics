using Sharpmake;
using System;
using System.IO;

namespace SlimEngine
{
    [Sharpmake.Generate]
    public class DirectXMeshProject : SlimEngineBaseProject
    {
        public DirectXMeshProject() : base("DirectXMesh")
        {
            SourceRootPath = Path.Join(Globals.SubmodulesPath, "DirectXMesh");
            SourceFiles.Add(Globals.GetThisFilePath());
        }

        public override void ConfigureAll(Configuration conf, SlimEngineTarget target)
        {
            base.ConfigureAll(conf, target);
            conf.IncludePaths.Add(SourceRootPath);
            conf.IncludePaths.Add(Path.Join(SourceRootPath, "DirectXMesh"));
            conf.SolutionFolder = "SubModules";
            conf.SourceFilesBuildExcludeRegex.Add(@"\.*\\(Meshconvert)\\");
        }
    }
}