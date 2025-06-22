using Sharpmake;
using System;
using System.IO;

namespace SlimEngine
{
    [Sharpmake.Generate]
    public class SlimEngineProject : SlimEngineBaseProject
    {
        public SlimEngineProject() : base("SlimEngine")
        {
            SourceRootPath = Path.Join(Globals.Root, "SlimEngine");
            SourceFiles.Add(Globals.GetThisFilePath());
        }

        public override void ConfigureAll(Configuration conf, SlimEngineTarget target)
        {
            base.ConfigureAll(conf, target);
            conf.SolutionFolder = "SlimEngine";

            conf.IncludePaths.Add(SourceRootPath);
            conf.IncludePaths.Add(Path.Join(Globals.ExternalsPath, @"GameInput\include"));

            conf.PrecompHeader = "se_engine_pch.h";
            conf.PrecompSource = "se_engine_pch.cpp";

            if (target.Platform != Platform.win64)
            {
                conf.SourceFilesBuildExcludeRegex.Add(@"\win32\*");
            }
            conf.AddPrivateDependency<ImGuiProject>(target);
        }
    }
}