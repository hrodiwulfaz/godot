using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Testing;
using Microsoft.CodeAnalysis.Diagnostics;
using Microsoft.CodeAnalysis.Testing;
using Microsoft.CodeAnalysis.Text;

namespace Godot.SourceGenerators.Tests;

public static class CSharpAnalyzerVerifier<TAnalyzer>
    where TAnalyzer : DiagnosticAnalyzer, new()
{
    public const LanguageVersion LangVersion = LanguageVersion.CSharp11;

    public class Test : CSharpAnalyzerTest<TAnalyzer, DefaultVerifier>
    {
        public Test()
        {
            ReferenceAssemblies = Constants.Net80;

            SolutionTransforms.Add((Solution solution, ProjectId projectId) =>
            {
                Project project =
                    solution.GetProject(projectId)!.AddMetadataReference(Constants.GodotSharpAssembly
                        .CreateMetadataReference()).WithParseOptions(new CSharpParseOptions(LangVersion));

                return project.Solution;
            });
        }
    }

    public static Task Verify(string sources, params DiagnosticResult[] expected)
    {
        return MakeVerifier(new string[] { sources }, expected).RunAsync();
    }

    public static Test MakeVerifier(ICollection<string> sources, params DiagnosticResult[] expected)
    {
        return MakeVerifier(sources, new Dictionary<string, string>(), expected);
    }

    public static Test MakeVerifier(
        ICollection<string> sources,
        IReadOnlyDictionary<string, string> globalOptions,
        params DiagnosticResult[] expected)
    {
        var verifier = new Test();

        verifier.TestState.AnalyzerConfigFiles.Add(("/.globalconfig", CreateGlobalConfig(globalOptions)));

        verifier.TestState.Sources.AddRange(sources.Select(source =>
        {
            return (source, SourceText.From(File.ReadAllText(Path.Combine(Constants.SourceFolderPath, source))));
        }));

        verifier.ExpectedDiagnostics.AddRange(expected);
        return verifier;
    }

    public static Test MakeInlineVerifier(
        string source,
        IReadOnlyDictionary<string, string>? globalOptions = null,
        params DiagnosticResult[] expected)
    {
        var verifier = new Test();

        verifier.TestState.AnalyzerConfigFiles.Add((
            "/.globalconfig",
            CreateGlobalConfig(globalOptions ?? new Dictionary<string, string>())));
        verifier.TestState.Sources.Add(("/Test0.cs", SourceText.From(source)));
        verifier.ExpectedDiagnostics.AddRange(expected);
        return verifier;
    }

    private static string CreateGlobalConfig(IReadOnlyDictionary<string, string> globalOptions)
    {
        var config = new StringBuilder();
        config.AppendLine("is_global = true");
        config.AppendLine($"build_property.GodotProjectDir = {Constants.ExecutingAssemblyPath}");

        foreach ((string property, string value) in globalOptions)
            config.AppendLine($"build_property.{property} = {value}");

        return config.ToString();
    }
}
