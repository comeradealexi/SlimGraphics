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
using System.Web.UI;

namespace SlimShaderCompiler
{
    public class Attribute_DoNotCopy : Attribute { public string DefaultValue { get; set; } }
    public class Attribute_Append : Attribute { public string DefaultValue { get; set; } }

    public struct VariantDefine
    {
        // define to expose
        public string[] defines { get; set; }

        // unique identified appended to start of the string
        public string id { get; set; }
    }

    public struct VariationCombinations
    {
        public List<string> defines;
        public string ids;
    };

    public class ShaderJSON : ICloneable
    {
        public string name { get; set; }
        
        public string input_file { get; set; }
                
        public string entrypoint { get; set; }

        [Attribute_Append]
        public List<string> defines { get; set; } = new List<string>();
        
        public string profile { get; set; }
        
        [Attribute_DoNotCopy]
        public ShaderJSON[] shaders { get; set; }

        public string working_directory { get; set; }

        // We compile each without any extra variant defines, then one version with each variant define
        [Attribute_Append]
        public List<VariantDefine> variant_defines { get; set; } = new List<VariantDefine>();

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
                        // TODO: handle below in a generic way
                        if (property.GetValue(this).GetType().Equals(typeof(List<string>)))
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
                        else if (property.GetValue(this).GetType().Equals(typeof(List<VariantDefine>)))
                        {
                            List<VariantDefine> this_defines = (List<VariantDefine>)property.GetValue(this);
                            List<VariantDefine> parent_defines = (List<VariantDefine>)property.GetValue(shader);
                            int index = 0;
                            foreach (var parent in parent_defines)
                            {
                                this_defines.Insert(index, parent);
                                index++;
                            }
                        }
                        else
                        {
                            Console.WriteLine(typeof(List<string>));
                            Console.WriteLine(property.GetValue(this).GetType());
                            throw new Exception();
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

        public object Clone()
        {
            return this.MemberwiseClone();
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

        // Will gather every variation based on bitset
        // e.g. 0b0000 will return no defines 0b1111 will return all defines etc
        static VariationCombinations GatherVariants(List<VariantDefine> variants, int bitset)
        {
            VariationCombinations return_data;
            return_data.defines = new List<string>();
            return_data.ids = "";
            for (int i = 0; i < variants.Count; i++)
            {
                if (((1 << i) & bitset) != 0)
                {
                    return_data.defines.InsertRange(0, variants[i].defines);
                    return_data.ids += "_" + variants[i].id;
                }
            }
            return return_data;
        }

        // Called recursively
        static void ProcessShader(ShaderJSON shader, ref List<ShaderJSON> shader_compile_list)
        {
            // We're a root level shader, add this shader to be compiled.
            if (shader.shaders == null || shader.shaders.Length == 0)
            {
                int count = 1 << shader.variant_defines.Count;
                for (int i = 0; i < count; i++)
                {
                    VariationCombinations combined_defines = GatherVariants(shader.variant_defines, i);
                    ShaderJSON shader_tmp = shader.Clone() as ShaderJSON;
                    if (combined_defines.defines.Count != 0)
                    {
                        shader_tmp.defines.InsertRange(0, combined_defines.defines);
                        shader_tmp.name = string.Format("{0}{1}", shader_tmp.name, combined_defines.ids);
                    }
                    shader_compile_list.Add(shader_tmp);
                }
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
            bool all_success = true;

            Parallel.For(0, args.shaders.Count, index =>
            {
                var shader = args.shaders[index];

                Stopwatch stop_watch = new Stopwatch();
                stop_watch.Start();
                StringBuilder sb = new StringBuilder();
                if (args.platform == Platform.PC_DXC_SPIRV)
                {
                    sb.Append("-spirv ");
                }
                sb.Append("/Zi ");
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
                    Console.WriteLine("Shader Index {0} - Failed to start process", index);
                    all_success = false;
                    return;
                }
                process.WaitForExit();
                if (process.ExitCode != 0)
                {
                    string error_string = string.Format("Error: Shader Index {0}", index);
                    error_string += string.Format("\n\tShader compile failed with command line: {0} {1}", process.StartInfo.FileName, process.StartInfo.Arguments);
                    error_string += string.Format("\n\tWorking directory: {0}", process.StartInfo.WorkingDirectory);
                    error_string += string.Format("\n\tstdout: {0}", process.StandardOutput.ReadToEnd());
                    error_string += string.Format("\n\tstderr: {0}", process.StandardError.ReadToEnd());

                    Console.WriteLine(error_string);
                    all_success = false;
                    return;
                }
                stop_watch.Stop();
                Console.WriteLine("Built {0}/{1}: {2} (Time: {3}ms)", index, args.shaders.Count, shader.name, stop_watch.ElapsedMilliseconds);

            });

            return all_success;
        }
    }
}
