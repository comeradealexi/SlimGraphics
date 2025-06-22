using Sharpmake;
using System;
using System.IO;

namespace SlimEngine
{
    [Sharpmake.Generate]
    public class SlimGraphicsApplicationProject : SlimEngineBaseProject
    {
        public SlimGraphicsApplicationProject() : base("SlimGraphicsApplication")
        {
            SourceRootPath = Path.Join(Globals.Root, "SlimGraphicsApplication");
            SourceFiles.Add(Globals.GetThisFilePath());
        }

        public override void ConfigureAll(Configuration conf, SlimEngineTarget target)
        {
            base.ConfigureAll(conf, target);
            conf.SolutionFolder = "Applications";

            conf.IncludePaths.Add(SourceRootPath);
            conf.IncludePaths.Add(Path.Join(Globals.ExternalsPath, @"assimp-5.3.1\include"));

            conf.AddPrivateDependency<SlimEngineProject>(target);
            conf.AddPrivateDependency<SlimGraphicsD3D12>(target); ;
            conf.AddPrivateDependency<MeshOptimizerProject>(target);
            conf.AddPrivateDependency<D3D12MAProject>(target);
            conf.AddPrivateDependency<DirectXMeshProject>(target);
            conf.AddPrivateDependency<DirectXToolkitProject>(target);
            conf.AddPrivateDependency<ImGuiProject>(target);
            conf.AddPrivateDependency<ImPlotProject>(target);


            conf.LibraryFiles.Add("GameInput.lib");
            conf.LibraryFiles.Add("assimp-vc143-mt.lib");
            conf.LibraryFiles.Add("d3d12.lib");
            conf.LibraryFiles.Add("dxgi.lib");
            conf.LibraryFiles.Add("WinPixEventRuntime.lib");

            conf.LibraryPaths.Add(Path.Join(Globals.Root, @"SlimGraphicsD3D12\winpixeventruntime.1.0.231030001\bin\x64"));
            conf.LibraryPaths.Add(Path.Join(Globals.ExternalsPath, @"assimp-5.3.1\lib\RelWithDebInfo"));
            conf.LibraryPaths.Add(Path.Join(Globals.ExternalsPath, @"GameInput\lib\x64"));

            conf.EventPostBuild.Add("xcopy " + Path.Join(Globals.ExternalsPath, @"assimp-5.3.1\bin\RelWithDebInfo\assimp-vc143-mt.dll") + " " + SourceRootPath + " /y");
            conf.EventPreBuild.Add(Path.Join(Globals.Root, @"Binaries\SlimShaderCompiler.exe") + " " + SourceRootPath + " " + Path.Join(SourceRootPath, "ShaderBin_" + target.Optimization) + " PC_DXC");

            conf.VcxprojUserFile = new Configuration.VcxprojUserFileSettings();
            conf.VcxprojUserFile.LocalDebuggerWorkingDirectory = SourceRootPath;

            conf.Options.Add(Options.Vc.Linker.SubSystem.Windows);
            conf.Output = Configuration.OutputType.Exe;
        }
    }
}