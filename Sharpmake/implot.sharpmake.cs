using Sharpmake;
using System;
using System.IO;


namespace SlimEngine
{
    [Sharpmake.Generate]
    public class ImPlotProject : SlimEngineBaseProject
    {
        public ImPlotProject() : base("ImPlot")
        {
            SourceRootPath = Path.Join(Globals.SubmodulesPath, "implot");
            SourceFiles.Add(Globals.GetThisFilePath());
        }

        public override void ConfigureAll(Configuration conf, SlimEngineTarget target)
        {
            base.ConfigureAll(conf, target);

            conf.SolutionFolder = "SubModules";
            conf.IncludePaths.Add(SourceRootPath);
            conf.SourceFilesBuildExcludeRegex.Add(@"\.*\\(github)\\");
            conf.AddPrivateDependency<ImGuiProject>(target);
        }
    }
}