using Sharpmake;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;

namespace SlimEngine
{

    public partial class SlimEngineBaseProject : Project
    {
        public SlimEngineBaseProject(string name) : base(typeof(SlimEngineTarget))
        {
            Name = name;

            AddTargets(new SlimEngineTarget(
                    Platform.win64,
                    DevEnv.vs2022,
                    Optimization.Debug | Optimization.Profile | Optimization.Release)
                );
        }

        [Configure(Platform.win64)]
        public void ConfigureWin64(Configuration conf, SlimEngineTarget target)
        {
            conf.IntermediatePath += Path.DirectorySeparatorChar + Name;
            conf.Options.Add(Options.Vc.General.CharacterSet.Unicode);
            conf.Options.Add(Options.Vc.Compiler.CppLanguageStandard.CPP17);
        }

        [Configure]
        public virtual void ConfigureAll(Configuration conf, SlimEngineTarget target)
        {
            conf.ProjectFileName = "[project.Name]_[target.DevEnv]_[target.Platform]";
            conf.ProjectPath = @"[project.SharpmakeCsPath]\projects";
            conf.Output = Configuration.OutputType.Lib;

            switch (target.Optimization)
            {
                case Optimization.Debug:
                    conf.Defines.Add("SE_DEBUG");
                    break;
                case Optimization.Profile:
                    conf.Defines.Add("SE_PROFILE");
                    break;
                case Optimization.Release:
                    conf.Defines.Add("SE_RELEASE");
                    break;
                default:
                    throw new NotSupportedException("Optimization value " + target.Optimization.ToString());
            }
        }
    }

    [Fragment, Flags]
    public enum Optimization
    {
        Debug = 1 << 0,
        Profile = 1 << 1,
        Release = 1 << 2
    }

    public partial class SlimEngineTarget : Sharpmake.ITarget
    {
        public Platform Platform;
        public DevEnv DevEnv;
        public OutputType OutputType;
        public Optimization Optimization;
        public Blob Blob;
        public BuildSystem BuildSystem;
        public DotNetFramework framework = DotNetFramework.v4_7_2;

        public SlimEngineTarget() { }

        public SlimEngineTarget(
            Platform platform,
            DevEnv devEnv,
            Optimization optimization,
            OutputType outputType = OutputType.Lib,
            Blob blob = Blob.NoBlob,
            BuildSystem buildSystem = BuildSystem.MSBuild
        )
        {
            Platform = platform;
            DevEnv = devEnv;
            OutputType = outputType;
            Optimization = optimization;
            Blob = blob;
            BuildSystem = buildSystem;
        }

        public override string Name
        {
            get
            {
                return Optimization.ToString();
            }
        }

        public override Sharpmake.Optimization GetOptimization()
        {
            switch (Optimization)
            {
                case Optimization.Debug:
                    return Sharpmake.Optimization.Debug;
                case Optimization.Release:
                case Optimization.Profile:
                    return Sharpmake.Optimization.Release;
                default:
                    throw new NotSupportedException("Optimization value " + Optimization.ToString());
            }
        }

        public override Platform GetPlatform()
        {
            return Platform;
        }
    }
}