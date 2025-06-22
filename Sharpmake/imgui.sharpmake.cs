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
            SourceRootPath = Path.Join(Globals.SubmodulesPath, Globals.ImGuiDocking ? "ImGuiDocking" : "imgui");
            SourceFilesExcludeRegex.Add(@"\.*\\(examples)\\"); // Don't want loads of main.cpp files visible in project
            SourceFiles.Add(Globals.GetThisFilePath());
        }

        public override void ConfigureAll(Configuration conf, SlimEngineTarget target)
        {
            base.ConfigureAll(conf, target);

            conf.ExportDefines.Add("SE_IMGUI");
            if (Globals.ImGuiDocking)
            {
                conf.ExportDefines.Add("SE_IMGUI_DOCKING");
            }

            conf.SolutionFolder = "SubModules";
            conf.IncludePaths.Add(SourceRootPath);
            conf.IncludePaths.Add(Path.Join(SourceRootPath, "backends"));

            conf.SourceFilesBuildExcludeRegex.Add(@"\.*\\(misc)\\");

            if (target.Platform == Platform.win64)
            {
                conf.SourceFilesBuildExcludeRegex.Add(@"\.*\\backends\\imgui_impl_(?!dx12|win32)");
            }
        }
    }
}