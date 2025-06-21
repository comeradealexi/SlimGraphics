using Sharpmake;
using System;
using System.IO;

namespace SlimEngine
{
    [Sharpmake.Generate]
    public class ImGuiProject : SlimEngineBaseProject
    {
        public ImGuiProject() : base("ImGui")
        {
            SourceRootPath = Path.Join(Globals.SubmodulesPath, "imgui");
        }

        public override void ConfigureAll(Configuration conf, SlimEngineTarget target)
        {
            base.ConfigureAll(conf, target);

            conf.SolutionFolder = "SubModules";
            conf.IncludePaths.Add(SourceRootPath);

            conf.SourceFilesBuildExcludeRegex.Add(@"\.*\\(examples)\\");
            conf.SourceFilesBuildExcludeRegex.Add(@"\.*\\(misc)\\");

            if (target.Platform == Platform.win64)
            {
                conf.SourceFilesBuildExcludeRegex.Add(@"\.*\\backends\\imgui_impl_(?!dx12|win32)");
            }
        }
    }
}