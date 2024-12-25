using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Text.Json;
using System.Reflection;
using System.Collections;
using System.Diagnostics;

namespace SlimShaderCompiler
{
    public class Attribute_DoNotCopy: Attribute { public string DefaultValue { get; set; } }
    public class Attribute_Append : Attribute { public string DefaultValue { get; set; } }
    public class ShaderJSON
    {
        public string name { get; set; }
        
        public string input_file { get; set; }
                
        public string entrypoint { get; set; }
        
        [Attribute_Append]
        public List<string> defines { get; set; }
        
        public string profile { get; set; }
        
        [Attribute_DoNotCopy]
        public ShaderJSON[] shaders { get; set; }

        public string working_directory { get; set; }

        // And null values in `this` will be set by shader argument
        public void SetNullFieldsFromParent(ShaderJSON shader)
        {
            System.Reflection.PropertyInfo[] properties = typeof(ShaderJSON).GetProperties();
            foreach (var property in properties)
            {
                if (property.GetValue(shader) == null) // If parent property is null, then nothing to do here
                {
                    continue;
                }

                if (property.GetCustomAttribute<Attribute_Append>() != null) // Process appending attributes
                {
                    if (property.GetValue(this) == null)
                    {
                        property.SetValue(this, property.GetValue(shader));
                    }
                    else
                    {
                        List<string> this_defines = (List<string>)property.GetValue(this);
                        List<string> parent_defines = (List<string>)property.GetValue(shader);
                        int index = 0;
                        foreach (var parent in parent_defines)
                        {
                            this_defines.Insert(index, parent);
                            index++;
                        }
                    }
                }
                else
                {
                    // If our property is null, set from parent
                    if (property.GetValue(this) == null && property.GetCustomAttribute<Attribute_DoNotCopy>() == null)
                    {
                        property.SetValue(this, property.GetValue(shader));
                    }
                }
    
            }
        }
    }
    public enum Platform
    {
        PC_DXC, PC_DXC_SPIRV,
    }
    public struct CompileArguments
    {
        public List<ShaderJSON> shaders;
        public Platform platform;
        public List<string> defines;
        public string output_directory;
    }
    internal class ShaderCompiler
    {
        static readonly string shader_list_file_extension = ".slimshaderjson";
        static readonly string dxc_exe_path = "..\\Binaries\\dxc.exe";
        static void PrintHelp()
        {
            Console.WriteLine("Arg 0: Input directory to search");
            Console.WriteLine("Arg 1: Output directory");
            Console.WriteLine("Arg 2: Platform");
            Console.WriteLine("Arg 3 (optional): Additional defines (separated with ; - e.g: define1;define2;)");
            Console.WriteLine("");
            Console.WriteLine("Valid Platforms: {0}", string.Join(",", Enum.GetNames(typeof(Platform))));            
        }
        static int Main(string[] args)
        {
            Console.WriteLine("Slim Shader Compiler");
            if (args.Length < 3)
            {
                PrintHelp();
                return -1;
            }
            
            string input_directory = args[0];
            string output_directory = args[1];
            Console.WriteLine("Input Directory:  {0}", input_directory);
            Console.WriteLine("Output Directory: {0}", output_directory);
            Directory.CreateDirectory(output_directory);
            Platform platform;
            List<string> defines = new List<string>();
            defines.Add("SLIM_SHADER_COMPILER");
            List<ShaderJSON> shaders = new List<ShaderJSON>();

            if (Enum.TryParse<Platform>(args[2], out platform) == false)
            {
                Console.WriteLine("Failed to parse {0} as a valid platform.", args[2]);
                PrintHelp();
                return -1;
            }
            Console.WriteLine("Platform: {0}", args[2]);

            if (args.Length > 3)
            {
                defines.AddRange(args[3].Split(';'));
            }
            
            if (Directory.Exists(input_directory) == false)
            {
                Console.WriteLine("Error: Input directory {0} does not exist.", input_directory);
                return -1;
            }
            Directory.SetCurrentDirectory(input_directory);

            foreach (string file in Directory.EnumerateFiles(input_directory, "*" + shader_list_file_extension, SearchOption.AllDirectories))
            {
                Console.WriteLine("Processing {0}", file);
                string file_data = File.ReadAllText(file);
                ShaderJSON[] shader_read;
                try
                {
                    shader_read = JsonSerializer.Deserialize<ShaderJSON[]>(file_data);
                }
                catch (Exception ex)
                {
                    Console.WriteLine("Error: JSON failed to parse file {0} - Exception: {1}", file, ex.Message);
                    return -1;
                }                

                foreach (ShaderJSON shader in shader_read)
                {
                    shader.working_directory = Path.GetDirectoryName(file);
                    ProcessShader(shader, ref shaders);
                }
            }

            if (SanityCheckShaders(ref shaders) == false)
            {
                return -1;
            }

            CompileArguments compile_args;
            compile_args.shaders = shaders;
            compile_args.platform = platform;
            compile_args.defines = defines;
            compile_args.output_directory = output_directory;

            bool compile_success = Compile(compile_args);
            if (compile_success == false)
            {
                Console.WriteLine("Shader compilation has failed.");
                return -1;
            }
            return 0;
        }

        // Called recursively
        static void ProcessShader(ShaderJSON shader, ref List<ShaderJSON> shader_compile_list)
        {
            // We're a root level shader, add this shader to be compiled.
            if (shader.shaders == null || shader.shaders.Length == 0)
            {
                shader_compile_list.Add(shader);
            }
            else
            {
                foreach (var sub_shader in shader.shaders)
                {
                    sub_shader.SetNullFieldsFromParent(shader);
                    ProcessShader(sub_shader, ref shader_compile_list);
                }
            }
        }

        static bool SanityCheckShaders(ref List<ShaderJSON> shaders)
        {
            HashSet<string> shadernames = new HashSet<string>();
            foreach(var v in shaders)
            {
                if (v.input_file == null || v.input_file.Length == 0)
                {
                    Console.WriteLine("Error: Shader has no input file specified. {0}", v.name);
                    return false;
                }

                if (v.profile == null || v.profile.Length == 0)
                {
                    Console.WriteLine("Error: Shader has no profile specified. {0}", v.name);
                    return false;
                }

                if (v.entrypoint == null || v.entrypoint.Length == 0)
                {
                    Console.WriteLine("Error: Shader has no entrypoint specified. {0}", v.name);
                    return false;
                }

                if (shadernames.Add(v.name) == false)
                {
                    Console.WriteLine("Error: Duplicate shader output names will conflict {0}", v.name);
                    return false;
                }
            }
            return true;
        }

        static bool Compile(CompileArguments args)
        {
            int counter = 0;
            foreach (var shader in args.shaders)
            {
                Console.WriteLine("Building {0}/{1}: {2}", ++counter, args.shaders.Count, shader.name);
                StringBuilder sb = new StringBuilder();
                if (args.platform == Platform.PC_DXC_SPIRV)
                {
                    sb.Append("-spirv ");
                }
                sb.AppendFormat("-T {0} ", shader.profile);
                sb.AppendFormat("-E {0} ", shader.entrypoint);
                sb.AppendFormat("-Fo \"{0}\\{1}.{2}\" ", args.output_directory, shader.name, args.platform.ToString());
                if (args.defines != null)
                {
                    foreach (string define in args.defines)
                    {
                        sb.AppendFormat("-D {0} ", define);
                    }
                }
                if (shader.defines != null)
                {
                    foreach (string define in shader.defines)
                    {
                        sb.AppendFormat("-D {0} ", define);
                    }
                }

                sb.Append(shader.input_file);

                Process process = new Process();
                process.StartInfo.CreateNoWindow = true;
                process.StartInfo.FileName = Path.Combine(Directory.GetCurrentDirectory(), dxc_exe_path);
                process.StartInfo.Arguments = sb.ToString();
                process.StartInfo.WorkingDirectory = shader.working_directory;
                process.StartInfo.UseShellExecute = false;
                process.StartInfo.RedirectStandardOutput = true;
                process.StartInfo.RedirectStandardError = true;
                if (process.Start() == false)
                {
                    Console.WriteLine("Failed to start process");
                    return false;
                }
                process.WaitForExit();
                if (process.ExitCode != 0)
                {
                    Console.WriteLine("Shader compile failed with command line: {0} {1}", process.StartInfo.FileName, process.StartInfo.Arguments);
                    Console.WriteLine("Working directory: {0}", process.StartInfo.WorkingDirectory);
                    Console.WriteLine("stdout: {0}", process.StandardOutput.ReadToEnd());
                    Console.WriteLine("stderr: {0}", process.StandardError.ReadToEnd());
                    return false;
                }
            }
            return true;
        }
    }
}
