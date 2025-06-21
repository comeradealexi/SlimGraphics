using Sharpmake;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;

namespace SlimEngine
{
    [Export]
    public class SharpmakeReference : CSharpProject
    {
        public SharpmakeReference() : base(typeof(SlimEngineTarget))
        {
            Name = "SharpmakeReference";
            AddTargets(new SlimEngineTarget(
                    Platform.anycpu,
                    DevEnv.vs2022,
                    Optimization.Debug | Optimization.Release, 
                    OutputType.Dll
                    ));
        }

        [Configure]
        public void ConfigureAll(CSharpProject.Configuration conf, SlimEngineTarget target)
        {
            conf.ProjectPath = @"[project.SharpmakeCsPath]\sharpmake_projects\";
            conf.ProjectFileName = "sharpmake_debug.vs2022";
            conf.SolutionFolder = "Sharpmake";

        }
    }
}