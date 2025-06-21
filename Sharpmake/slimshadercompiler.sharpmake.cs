using Sharpmake;
using System;
using System.IO;
using static Sharpmake.Project.Configuration;

namespace SlimEngine
{
    [Sharpmake.Generate]
    public class SlimShaderCompilerProject : CSharpProject
    {
        public SlimShaderCompilerProject() : base(typeof(SlimEngineTarget))
        {
            Name = "SlimShaderCompiler";
            RootPath = Path.Join(Globals.Root, "SlimShaderCompiler");

            SourceRootPath = Path.Join(Globals.Root, "SlimShaderCompiler");
            AddTargets(new SlimEngineTarget(
        Platform.anycpu,
        DevEnv.vs2022,
        Optimization.Debug | Optimization.Release, 
        Sharpmake.OutputType.Dll));
        }

        [Configure]
        public void ConfigureAll(CSharpProject.Configuration conf, SlimEngineTarget target)
        {
            conf.ProjectFileName = "[project.Name]_[target.DevEnv]_[target.Platform]";
            conf.ProjectPath = @"[project.SharpmakeCsPath]\csharp_projects";
            conf.SolutionFolder = "SlimEngine";
            conf.ReferencesByName.Add(
                "System.Web.DynamicData",
                "Microsoft.CSharp",
                "Microsoft.CSharp",
                "System.Web.Entity",
                "System.Web.ApplicationServices",
                "System",
                "System.Configuration",
                "System.Core",
                "System.Data",
                "System.Text.Json",
                "System.Drawing",
                "System.EnterpriseServices",
                "System.Runtime.Serialization",
                "System.ServiceModel",
                "System.ServiceModel.Web",
                "System.Web",
                "System.Web.Extensions",
                "System.Web.Services",
                "System.Xml",
                "System.Xml.Linq",
                "System.Memory");
        }
    }
}