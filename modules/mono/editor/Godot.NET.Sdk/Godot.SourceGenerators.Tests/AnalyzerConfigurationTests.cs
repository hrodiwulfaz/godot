using System.Collections.Generic;
using System.Threading.Tasks;
using Microsoft.CodeAnalysis.Diagnostics;
using Xunit;

namespace Godot.SourceGenerators.Tests;

public class AnalyzerConfigurationTests
{
    private const string ClassPartialSource = """
        using Godot;

        public class MissingPartial : GodotObject
        {
        }
        """;

    private const string ClassPartialSourceWithDiagnostic = """
        using Godot;

        public class {|GD0001:MissingPartial|} : GodotObject
        {
        }
        """;

    private const string GlobalClassSource = """
        using Godot;

        [GlobalClass]
        public partial class InvalidGlobal
        {
        }
        """;

    private const string GlobalClassSourceWithDiagnostic = """
        using Godot;

        [GlobalClass]
        public partial class {|GD0401:InvalidGlobal|}
        {
        }
        """;

    private const string MustBeVariantSource = """
        using Godot;

        public class VariantConsumer
        {
            private void Method<[MustBeVariant] T>()
            {
            }

            public void Test()
            {
                Method<object>();
            }
        }
        """;

    private const string MustBeVariantSourceWithDiagnostic = """
        using Godot;

        public class VariantConsumer
        {
            private void Method<[MustBeVariant] T>()
            {
            }

            public void Test()
            {
                Method<{|GD0301:object|}>();
            }
        }
        """;

    [Fact]
    public async Task AnalyzersRunByDefault()
    {
        await Verify<ClassPartialModifierAnalyzer>(ClassPartialSourceWithDiagnostic);
        await Verify<GlobalClassAnalyzer>(GlobalClassSourceWithDiagnostic);
        await Verify<MustBeVariantAnalyzer>(MustBeVariantSourceWithDiagnostic);
    }

    [Fact]
    public async Task GodotAnalyzersDisabledDisablesAllConfigurableAnalyzers()
    {
        var options = Options(("GodotAnalyzers", "disabled"));

        await Verify<ClassPartialModifierAnalyzer>(ClassPartialSource, options);
        await Verify<GlobalClassAnalyzer>(GlobalClassSource, options);
        await Verify<MustBeVariantAnalyzer>(MustBeVariantSource, options);
    }

    [Fact]
    public async Task EachAnalyzerCanBeDisabledByClassName()
    {
        await Verify<ClassPartialModifierAnalyzer>(
            ClassPartialSource,
            Options(("GodotDisabledAnalyzers", "ClassPartialModifierAnalyzer")));
        await Verify<GlobalClassAnalyzer>(
            GlobalClassSource,
            Options(("GodotDisabledAnalyzers", "GlobalClassAnalyzer")));
        await Verify<MustBeVariantAnalyzer>(
            MustBeVariantSource,
            Options(("GodotDisabledAnalyzers", "MustBeVariantAnalyzer")));
    }

    [Fact]
    public async Task DisabledAnalyzerListIsTrimmedAndCaseInsensitive()
    {
        await Verify<MustBeVariantAnalyzer>(
            MustBeVariantSource,
            Options(("GodotDisabledAnalyzers", "mustbevariantanalyzer; UnknownAnalyzer; GlobalClassAnalyzer")));
    }

    [Fact]
    public async Task CompilerVisibleAnalyzerListTransportIsTrimmedAndCaseInsensitive()
    {
        var options = Options((
            "GodotDisabledAnalyzersCompilerVisible",
            "classpartialmodifieranalyzer, UnknownAnalyzer, MUSTBEVARIANTANALYZER"
        ));

        await Verify<ClassPartialModifierAnalyzer>(ClassPartialSource, options);
        await Verify<MustBeVariantAnalyzer>(MustBeVariantSource, options);
        await Verify<GlobalClassAnalyzer>(GlobalClassSourceWithDiagnostic, options);
    }

    [Fact]
    public async Task UnknownAnalyzerNameDoesNotDisableAnalyzer()
    {
        await Verify<ClassPartialModifierAnalyzer>(
            ClassPartialSourceWithDiagnostic,
            Options(("GodotDisabledAnalyzers", "UnknownAnalyzer")));
    }

    [Fact]
    public async Task AnalyzerAndSourceGeneratorSettingsRemainIndependent()
    {
        await Verify<ClassPartialModifierAnalyzer>(
            ClassPartialSourceWithDiagnostic,
            Options(("GodotDisabledSourceGenerators", "ScriptMethods")));

        var enabledGenerator = CSharpSourceGeneratorVerifier<ScriptMethodsGenerator>.MakeVerifier(
            new[] { "Methods.cs" },
            new[] { "Methods_ScriptMethods.generated.cs" },
            Options(("GodotDisabledAnalyzers", "ClassPartialModifierAnalyzer")));
        await enabledGenerator.RunAsync();

        var disabledGenerator = CSharpSourceGeneratorVerifier<ScriptMethodsGenerator>.MakeVerifier(
            new[] { "Methods.cs" },
            new string[] { },
            Options(
                ("GodotDisabledAnalyzers", "ClassPartialModifierAnalyzer"),
                ("GodotDisabledSourceGenerators", "scriptmethods; Unknown; Other")));
        await disabledGenerator.RunAsync();
    }

    [Fact]
    public async Task ExistingSourceGeneratorToggleRemainsFunctionalWithAnalyzerSettings()
    {
        var verifier = CSharpSourceGeneratorVerifier<ScriptMethodsGenerator>.MakeVerifier(
            new[] { "Methods.cs" },
            new string[] { },
            Options(
                ("GodotAnalyzers", "disabled"),
                ("GodotSourceGenerators", "disabled")));

        await verifier.RunAsync();
    }

    private static Task Verify<TAnalyzer>(
        string source,
        IReadOnlyDictionary<string, string>? globalOptions = null)
        where TAnalyzer : DiagnosticAnalyzer, new()
    {
        return CSharpAnalyzerVerifier<TAnalyzer>
            .MakeInlineVerifier(source, globalOptions)
            .RunAsync();
    }

    private static IReadOnlyDictionary<string, string> Options(
        params (string Property, string Value)[] options)
    {
        var result = new Dictionary<string, string>();
        foreach ((string property, string value) in options)
            result.Add(property, value);

        return result;
    }
}
