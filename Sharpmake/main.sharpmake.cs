using Sharpmake;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.CompilerServices;

[module: Sharpmake.Include("*.sharpmake.cs")]

namespace SlimEngine
{
    public static class Globals
    {
        // branch root path relative to current sharpmake file location
        public const string Root = @"[project.SharpmakeCsPath]\..\";
        public static string SubmodulesPath = @"[project.SharpmakeCsPath]\..\SubModules";
        public static string ExternalsPath = @"[project.SharpmakeCsPath]\..\External";

        public static bool ImGuiDocking = true;

        [CommandLine.Option("alexblobonly", @"Only generate blob and work blob files: ex: /blobonly")]
        public static void CommandLineBlobOnly()
        {
            System.Console.WriteLine("ALEX !!! BlobOnly!");
            Debug.WriteLine("ALEX !!! BlobOnly!");
        }

        public static string GetThisFilePath([CallerFilePath] string path = null)
        {
            return path;
        }
    }

    [Sharpmake.Generate]
    public class SlimGraphicsSolution : Sharpmake.Solution
    {
        public SlimGraphicsSolution() : base(typeof(SlimEngineTarget))
        {
            Name = "SlimGraphics";

            AddTargets(
                new SlimEngineTarget(
                    Platform.win64,
                    DevEnv.vs2022,
                    Optimization.Debug | Optimization.Profile | Optimization.Release),
                new SlimEngineTarget(
                    Platform.anycpu,
                    DevEnv.vs2022,
                    Optimization.Debug | Optimization.Release, OutputType.Dll)
                
                );
        }

        [Configure()]
        public void ConfigureAll(Configuration conf, SlimEngineTarget target)
        {
            conf.SolutionFileName = "[solution.Name]_[target.DevEnv]";
            conf.SolutionPath = @"[solution.SharpmakeCsPath]\projects";

            if (target.Platform == Platform.anycpu) // C# Projects
            {
                conf.AddProject<SharpmakeReference>(target);
                conf.AddProject<SlimShaderCompilerProject>(target);
            }
            else
            { 
                conf.AddProject<SlimEngineProject>(target);
                conf.AddProject<ImGuiProject>(target);
                conf.AddProject<ImPlotProject>(target);
                conf.AddProject<MeshOptimizerProject>(target);
                conf.AddProject<D3D12MAProject>(target);
                conf.AddProject<DirectXMeshProject>(target);
                conf.AddProject<DirectXToolkitProject>(target);
                conf.AddProject<SlimGraphicsD3D12>(target);
                conf.AddProject<SlimGraphicsApplicationProject>(target);
                conf.SetStartupProject<SlimGraphicsApplicationProject>();
            }
        }
    }

    public static class Main
    {
        [Sharpmake.Main]
        public static void SharpmakeMain(Sharpmake.Arguments arguments)
        {
            CommandLine.ExecuteOnType(typeof(Globals));

            KitsRootPaths.SetUseKitsRootForDevEnv(DevEnv.vs2022, KitsRootEnum.KitsRoot10, Options.Vc.General.WindowsTargetPlatformVersion.Latest);
            arguments.Generate<SlimGraphicsSolution>();
        }
    }
}
