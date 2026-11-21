#Requires -Version 5.1
<#
.SYNOPSIS
    Builds Slim Graphics for each rendering backend, runs the automated feature tests with the graphics API validation layers
    enabled and checks the results.

.DESCRIPTION
    For each backend (D3D12, Vulkan) this:
      1. Regenerates the project files (Sharpmake, /vulkan for Vulkan) and builds the Debug application (shaders are
         compiled by the pre-build step). Build errors fail; warnings in SlimGraphicsVulkan fail; other first party
         warnings are reported.
      2. Runs the test plan (Tools/validation_tests.json by default) in ONE application session per validation pass: the
         application's TestRunner sets up each case (model viewer modes, GDR scenes and camera track positions, post
         processing, ...), renders a few frames, captures the final render target, the presented output and the back buffer
         and logs statistics. A pass fails if the session times out or exits with a non-zero code, its validation layer did
         not load, any validation error / warning / corruption message or assert is logged, the runner reports an error, it
         does not complete every case, or a capture is missing. Messages are attributed to the case that was running.
      3. Compares D3D12 and Vulkan: the final and output captures pixel by pixel (back buffers, which include ImGui with
         live timings, are reported only) and the logged statistics (scene totals, GPU culling feedback, pipeline statistics,
         shader counters). Cases marked "nondeterministic" in the plan depend on GPU scheduling and are not compared.
    -Extended adds passes with D3D12 GPU-based validation, Vulkan synchronization validation and Vulkan GPU-assisted
    validation (each pass is its own session: validation layer settings are fixed when the device is created).

    Run from any directory:
        powershell -ExecutionPolicy Bypass -File Tools\validate.ps1 [-Extended] [-Backends Vulkan] [-SkipBuild] [-Cases "gdr_*"]

    The project files are left generated for the last backend validated.

.PARAMETER Backends
    Backends to validate. Parity is only compared when both are validated.
.PARAMETER Extended
    Also run the slower GPU-based / synchronization / GPU-assisted validation passes.
.PARAMETER BuildRelease
    Also build the Release configuration of each backend (compile and warning check only: Release has no validation).
.PARAMETER SkipBuild
    Reuse the executables saved by a previous run (slimgraphicsapplication_<backend>.exe next to the Debug build).
.PARAMETER ApplySubmodulePatches
    Apply any SubModules/Patches/*.patch that is not applied yet, instead of failing.
.PARAMETER TestPlan
    The test plan to run (see SlimGraphicsApplication/TestRunner.h).
.PARAMETER Cases
    Only run the plan's cases whose names match one of these wildcard patterns.
.PARAMETER OutDir
    Where build logs, run logs, captures and diff images are written.
.PARAMETER MaxDiffPercent
    Largest percentage of pixels allowed to differ (by more than 24/255 in any channel) between the backends' captures.
.PARAMETER SessionTimeoutSeconds
    Longest a test session may run.
#>
[CmdletBinding()]
param(
    [ValidateSet("D3D12", "Vulkan")]
    [string[]]$Backends = @("D3D12", "Vulkan"),
    [switch]$Extended,
    [switch]$BuildRelease,
    [switch]$SkipBuild,
    [switch]$ApplySubmodulePatches,
    [string]$TestPlan = "", # Default: Tools\validation_tests.json
    [string[]]$Cases = @(),
    [string]$OutDir = (Join-Path $env:TEMP "SlimGraphicsValidation"),
    [double]$MaxDiffPercent = 0.05,
    [int]$SessionTimeoutSeconds = 1800
)

$ErrorActionPreference = "Continue" # Native tool failures are checked through exit codes
$Root = Split-Path -Parent $PSScriptRoot
if (-not $TestPlan) { $TestPlan = Join-Path $PSScriptRoot "validation_tests.json" }
$AppDir = Join-Path $Root "SlimGraphicsApplication"
$SharpmakeDir = Join-Path $Root "Sharpmake"
$AppProject = Join-Path $SharpmakeDir "projects\slimgraphicsapplication_vs2022_win64.vcxproj"
$Failures = New-Object System.Collections.Generic.List[string]
$Notes = New-Object System.Collections.Generic.List[string]
$Results = New-Object System.Collections.Generic.List[object]

# Lines that count as validation problems in run logs (see the D3D12 / Vulkan debug message callbacks and se::Dbg::Assert)
$IssuePattern = '^\[(Vulkan|D3D12) (ERROR|WARNING|CORRUPTION)\]|^Assert:'

# Statistics compared between the backends: exact, or within a relative tolerance (plus a small absolute slack for
# counts that rasterisation or floating point differences between the shader compilers can nudge)
$ExactStats = @("gdr_instances", "gdr_mesh_parts", "gdr_meshlets", "gdr_lights", "gdr_texture_slices", "gdr_terrain_tiles", "gdr_opaque_meshlets", "gdr_foliage_meshlets", "gdr_terrain_meshlets", "ia_vertices", "ia_primitives")
$TolerantStats = @("gdr_culled_total", "gdr_culled_frustum", "gdr_culled_cone", "cs_invocations", "as_invocations", "ms_invocations",
    "ps_invocations", "gs_invocations", "gs_primitives", "mv_pixels", "mv_ms", "mv_as", "mv_ms_prims", "mv_cull_cone", "mv_cull_sphere", "mv_gs_in", "mv_gs_out")
$StatTolerance = 0.02
$StatSlack = 64

function Write-Step([string]$Message) { Write-Host ""; Write-Host "=== $Message" -ForegroundColor Cyan }
function Add-Failure([string]$Message) { $Failures.Add($Message); Write-Host "FAIL: $Message" -ForegroundColor Red }
function Add-Note([string]$Message) { $Notes.Add($Message); Write-Host "NOTE: $Message" -ForegroundColor Yellow }

function Get-Passes([string]$Backend)
{
    if ($Backend -eq "D3D12")
    {
        $passes = @(@{ Name = "core"; Env = @{}; Status = @("D3D12 debug layer: enabled"); Allow = @() })
        if ($Extended)
        {
            $passes += @{ Name = "gpu_validation"; Env = @{ SG_D3D12_GPU_VALIDATION = "1" }; Status = @("D3D12 debug layer: enabled", "D3D12 GPU-based validation: enabled"); Allow = @() }
        }
    }
    else
    {
        $passes = @(@{ Name = "core"; Env = @{}; Status = @("Vulkan validation layer: enabled"); Allow = @() })
        if ($Extended)
        {
            $passes += @{ Name = "sync"; Env = @{ VK_KHRONOS_VALIDATION_VALIDATE_SYNC = "true" }; Status = @("Vulkan validation layer: enabled"); Allow = @() }
            # GPU-AV reports the device features it force enables for its instrumentation; that is informational
            $passes += @{ Name = "gpuav"; Env = @{ VK_KHRONOS_VALIDATION_GPUAV_ENABLE = "true" }; Status = @("Vulkan validation layer: enabled"); Allow = @("validation is adjusting settings") }
        }
    }
    return $passes
}

function Get-MSBuild
{
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $null }
    $found = @(& $vswhere -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe")
    if ($found.Count -eq 0) { return $null }
    return $found[0]
}

function Test-SubmodulePatches
{
    # Local fixes to third party submodules live in SubModules/Patches as <SubmoduleFolder>-<description>.patch
    $patchDir = Join-Path $Root "SubModules\Patches"
    if (-not (Test-Path $patchDir)) { return }
    foreach ($patch in Get-ChildItem -LiteralPath $patchDir -Filter "*.patch")
    {
        $submodule = $patch.Name.Split('-')[0]
        $submoduleDir = Join-Path $Root "SubModules\$submodule"
        & git -C $submoduleDir apply --reverse --check $patch.FullName 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) { continue } # Already applied
        if ($ApplySubmodulePatches)
        {
            & git -C $submoduleDir apply $patch.FullName 2>&1 | Out-Null
            if ($LASTEXITCODE -eq 0) { Add-Note "Applied $($patch.Name) to SubModules/$submodule" }
            else { Add-Failure "Could not apply $($patch.Name) to SubModules/$submodule (submodule changed?)" }
        }
        else
        {
            Add-Failure "$($patch.Name) is not applied. Run: git -C SubModules/$submodule apply ../Patches/$($patch.Name)   (or pass -ApplySubmodulePatches)"
        }
    }
}

function Invoke-Sharpmake([string]$Backend)
{
    $arguments = @("/sources('main.sharpmake.cs')", "/generateDebugSolution", "/debugSolutionPath('sharpmake_projects')")
    if ($Backend -eq "Vulkan") { $arguments += "/vulkan" }
    Push-Location $SharpmakeDir
    try
    {
        $output = & ".\Sharpmake.Application\Debug\net6.0\Sharpmake.Application.exe" @arguments 2>&1
        $code = $LASTEXITCODE
    }
    finally { Pop-Location }
    if ($code -ne 0)
    {
        $output | Select-Object -Last 20 | ForEach-Object { Write-Host $_ }
        Add-Failure "Sharpmake project generation failed for $Backend"
        return $false
    }
    return $true
}

function Invoke-Build([string]$Backend, [string]$Configuration)
{
    $log = Join-Path $OutDir "build_${Backend}_$Configuration.log"
    # Output goes to a file logger: piping MSBuild into a cmdlet that stops reading early (Select-Object -First) deadlocks it
    & $script:MSBuild $AppProject "-p:Configuration=$Configuration" "-p:Platform=x64" "-m" "-v:minimal" "-noconsolelogger" "-flp:logfile=$log;verbosity=minimal" | Out-Null
    $code = $LASTEXITCODE
    $lines = @(Get-Content -LiteralPath $log)
    $errors = @($lines | Where-Object { $_ -match ': (fatal )?error |Error: Shader' } | Sort-Object -Unique)
    $warnings = @($lines | Where-Object { $_ -match ': warning ' } | Sort-Object -Unique)
    $vulkanWarnings = @($warnings | Where-Object { $_ -match '\\SlimGraphicsVulkan\\' })
    $firstPartyWarnings = @($warnings | Where-Object { $_ -match '\\(SlimEngine|SlimGraphicsD3D12|SlimGraphicsShared|SlimGraphicsApplication)\\' })

    $errors | Select-Object -First 20 | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    if ($code -ne 0 -or $errors.Count -gt 0) { Add-Failure "$Backend $Configuration build failed ($($errors.Count) errors, log: $log)"; return $false }
    if ($vulkanWarnings.Count -gt 0)
    {
        $vulkanWarnings | Select-Object -First 20 | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
        Add-Failure "$Backend $Configuration build has $($vulkanWarnings.Count) warnings in SlimGraphicsVulkan (must be warning free)"
    }
    if ($firstPartyWarnings.Count -gt 0) { Add-Note "$Backend $Configuration build: $($firstPartyWarnings.Count) warnings in other first party projects (see $log)" }
    Write-Host "  $Backend $Configuration build OK"
    return $true
}

function Get-DialogText([int]$ProcessId)
{
    # Assert / crash / fatal error dialogs block the process; read their text so the failure is actionable
    try
    {
        Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
        $condition = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $ProcessId)
        $windows = [System.Windows.Automation.AutomationElement]::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $condition)
        $text = @()
        foreach ($window in $windows)
        {
            foreach ($element in $window.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition))
            {
                if ($element.Current.Name.Length -gt 20) { $text += $element.Current.Name }
            }
        }
        return ($text | Select-Object -Unique) -join " | "
    }
    catch { return "" }
}

function Get-PlanCases([string]$PlanPath)
{
    # Case names and their nondeterministic flags (cases inherit the plan defaults)
    $plan = Get-Content -LiteralPath $PlanPath -Raw | ConvertFrom-Json
    $defaultNondeterministic = $false
    if ($plan.defaults -and $plan.defaults.nondeterministic) { $defaultNondeterministic = [bool]$plan.defaults.nondeterministic }
    $defaultBackends = @("D3D12", "Vulkan")
    if ($plan.defaults -and $null -ne $plan.defaults.backends) { $defaultBackends = @($plan.defaults.backends) }
    $list = @()
    foreach ($case in $plan.cases)
    {
        $nondeterministic = $defaultNondeterministic
        if ($null -ne $case.nondeterministic) { $nondeterministic = [bool]$case.nondeterministic }
        # Statistics that depend on GPU timing for this case (for example pixel shader invocations when the shader can
        # discard, which stops early depth rejection) are not compared; its images and other statistics still are
        $timingDependent = @()
        if ($null -ne $case.timing_dependent_stats) { $timingDependent = @($case.timing_dependent_stats) }
        # Backends the case runs on (features one backend does not implement yet); the others log it as skipped
        $caseBackends = $defaultBackends
        if ($null -ne $case.backends) { $caseBackends = @($case.backends) }
        $list += [pscustomobject]@{ Name = $case.name; Nondeterministic = $nondeterministic; TimingDependentStats = $timingDependent; Backends = $caseBackends }
    }
    return $list
}

function New-FilteredPlan([string]$PlanPath, [string[]]$Patterns)
{
    $plan = Get-Content -LiteralPath $PlanPath -Raw | ConvertFrom-Json
    # powershell -File passes "a","b" as one comma separated string
    $Patterns = @($Patterns | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
    $plan.cases = @($plan.cases | Where-Object { $name = $_.name; @($Patterns | Where-Object { $name -like $_ }).Count -gt 0 })
    if ($plan.cases.Count -eq 0) { Add-Failure "No test cases match $($Patterns -join ', ')"; return $null }
    $path = Join-Path $OutDir "filtered_plan.json"
    $plan | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $path -Encoding UTF8
    return $path
}

function Invoke-TestSession([string]$Backend, [string]$Exe, $Pass, [string]$PlanPath, $PlanCases)
{
    $dir = Join-Path $OutDir "$Backend\$($Pass.Name)"
    if (Test-Path $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Force $dir | Out-Null
    $log = Join-Path $dir "log.txt"
    $label = "$Backend/$($Pass.Name)"

    $environment = @{ SE_LOG_FILE = $log }
    foreach ($key in $Pass.Env.Keys) { $environment[$key] = $Pass.Env[$key] }
    $saved = @{}
    foreach ($key in $environment.Keys)
    {
        $saved[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
        [Environment]::SetEnvironmentVariable($key, $environment[$key], "Process")
    }

    $sessionProblems = @()
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    try
    {
        $process = Start-Process -FilePath $Exe -ArgumentList @("`"-test_plan=$PlanPath`"", "`"-test_output=$dir`"") -WorkingDirectory $AppDir -PassThru
        $null = $process.Handle # Keeps the exit code available after exit
        if (-not $process.WaitForExit($SessionTimeoutSeconds * 1000))
        {
            $dialog = Get-DialogText $process.Id
            Stop-Process -Id $process.Id -Force
            $sessionProblems += "timed out after $SessionTimeoutSeconds s" + $(if ($dialog) { " (dialog: $dialog)" } else { "" })
        }
        elseif ($process.ExitCode -ne 0)
        {
            $sessionProblems += ("exit code 0x{0:X}" -f $process.ExitCode)
        }
    }
    finally
    {
        foreach ($key in $saved.Keys) { [Environment]::SetEnvironmentVariable($key, $saved[$key], "Process") }
    }
    $seconds = [Math]::Round($stopwatch.Elapsed.TotalSeconds, 1)

    $lines = @()
    if (Test-Path $log) { $lines = @(Get-Content -LiteralPath $log) } else { $sessionProblems += "no log written" }
    foreach ($status in $Pass.Status)
    {
        if (-not ($lines | Where-Object { $_ -eq $status })) { $sessionProblems += "validation not active: missing '$status'" }
    }
    if (-not ($lines | Where-Object { $_ -match '^\[TEST\] COMPLETE' })) { $sessionProblems += "the test plan did not complete" }

    # Walk the log: attribute validation messages and runner errors to the case running at the time, collect statistics
    $caseIssues = @{}
    $stats = @{}
    $completed = @{}
    $skipped = @{}
    $current = "(startup)"
    foreach ($line in $lines)
    {
        if ($line -match '^\[TEST\] SKIP (\S+)') { $skipped[$Matches[1]] = $true; continue }
        if ($line -match '^\[TEST\] BEGIN (\S+)') { $current = $Matches[1]; continue }
        if ($line -match '^\[TEST\] END (\S+)') { $completed[$Matches[1]] = $true; $current = "(between cases)"; continue }
        if ($line -match '^\[TEST\] STAT (\S+) (.*)$')
        {
            $name = $Matches[1]
            if (-not $stats.ContainsKey($name)) { $stats[$name] = @{} }
            foreach ($pair in $Matches[2].Split(' ')) { $kv = $pair.Split('='); if ($kv.Count -eq 2) { $stats[$name][$kv[0]] = [double]$kv[1] } }
            continue
        }
        $isIssue = ($line -match $IssuePattern -or $line -match '^\[TEST\] ERROR')
        if ($isIssue)
        {
            $allowed = @($Pass.Allow | Where-Object { $line -match [regex]::Escape($_) }).Count -gt 0
            if (-not $allowed)
            {
                if (-not $caseIssues.ContainsKey($current)) { $caseIssues[$current] = @() }
                $caseIssues[$current] += $line
            }
        }
    }

    $failedCases = 0
    $ranCases = 0
    foreach ($case in $PlanCases)
    {
        if ($case.Backends -notcontains $Backend)
        {
            if (-not $skipped.ContainsKey($case.Name)) { Add-Failure "$label/$($case.Name): should have been skipped (it does not run on $Backend)" }
            $Results.Add([pscustomobject]@{ Backend = $Backend; Pass = $Pass.Name; Case = $case.Name; Status = "SKIP"; Issues = 0 })
            continue
        }
        $ranCases++
        $problems = @()
        if (-not $completed.ContainsKey($case.Name)) { $problems += "did not run to completion" }
        foreach ($suffix in "_final.png", "_output.png", "_backbuffer.png")
        {
            if ($completed.ContainsKey($case.Name) -and -not (Test-Path (Join-Path $dir "$($case.Name)$suffix"))) { $problems += "missing $($case.Name)$suffix" }
        }
        $issues = @()
        if ($caseIssues.ContainsKey($case.Name)) { $issues = $caseIssues[$case.Name] }
        if ($issues.Count -gt 0) { $problems += "$($issues.Count) validation / assert / runner messages" }
        if ($problems.Count -gt 0)
        {
            $failedCases++
            Add-Failure "$label/$($case.Name): $($problems -join '; ')"
            $issues | Sort-Object -Unique | Select-Object -First 6 | ForEach-Object { Write-Host "       $($_.Substring(0, [Math]::Min(300, $_.Length)))" -ForegroundColor DarkRed }
        }
        $Results.Add([pscustomobject]@{ Backend = $Backend; Pass = $Pass.Name; Case = $case.Name; Status = $(if ($problems.Count -eq 0) { "PASS" } else { "FAIL" }); Issues = $issues.Count })
    }
    foreach ($key in $caseIssues.Keys | Where-Object { $_ -like "(*" })
    {
        Add-Failure "$label $key`: $($caseIssues[$key].Count) validation / assert messages outside the test cases"
        $caseIssues[$key] | Sort-Object -Unique | Select-Object -First 6 | ForEach-Object { Write-Host "       $($_.Substring(0, [Math]::Min(300, $_.Length)))" -ForegroundColor DarkRed }
    }
    foreach ($problem in $sessionProblems) { Add-Failure "$label session: $problem (log: $log)" }

    $color = if ($failedCases -eq 0 -and $sessionProblems.Count -eq 0) { "Green" } else { "Red" }
    Write-Host ("  {0}: {1} cases, {2} failed, {3}s" -f $label, $ranCases, $failedCases, $seconds) -ForegroundColor $color
    return $stats
}

function Initialize-ImageDiff
{
    if ("SlimGraphicsImageDiff" -as [type]) { return }
    Add-Type -AssemblyName System.Drawing
    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class SlimGraphicsImageDiff
{
    static byte[] Load(string path, out int width, out int height)
    {
        using (var bitmap = new Bitmap(path))
        {
            width = bitmap.Width; height = bitmap.Height;
            var data = bitmap.LockBits(new Rectangle(0, 0, width, height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            var bytes = new byte[data.Stride * height];
            Marshal.Copy(data.Scan0, bytes, 0, bytes.Length);
            bitmap.UnlockBits(data);
            return bytes;
        }
    }
    // Returns the percentage of pixels differing by more than 'threshold' in any channel (-1 if the sizes differ) and, when
    // any do, writes an amplified difference image
    public static double Compare(string a, string b, string diffPath, int threshold)
    {
        int wa, ha, wb, hb;
        var pa = Load(a, out wa, out ha);
        var pb = Load(b, out wb, out hb);
        if (wa != wb || ha != hb) return -1.0;
        int over = 0;
        var diff = new byte[pa.Length];
        for (int i = 0; i < pa.Length; i += 4)
        {
            int d = Math.Max(Math.Abs(pa[i] - pb[i]), Math.Max(Math.Abs(pa[i + 1] - pb[i + 1]), Math.Abs(pa[i + 2] - pb[i + 2])));
            if (d > threshold) over++;
            byte v = (byte)Math.Min(255, d * 4);
            diff[i] = v; diff[i + 1] = v; diff[i + 2] = v; diff[i + 3] = 255;
        }
        if (over == 0) return 0.0;
        using (var bitmap = new Bitmap(wa, ha, PixelFormat.Format32bppArgb))
        {
            var data = bitmap.LockBits(new Rectangle(0, 0, wa, ha), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            Marshal.Copy(diff, 0, data.Scan0, diff.Length);
            bitmap.UnlockBits(data);
            bitmap.Save(diffPath, ImageFormat.Png);
        }
        return 100.0 * over / (wa * ha);
    }
}
"@
}

function Compare-Backends($PlanCases, $StatsD3D12, $StatsVulkan)
{
    Initialize-ImageDiff
    $parityDir = Join-Path $OutDir "parity"
    if (Test-Path $parityDir) { Remove-Item -LiteralPath $parityDir -Recurse -Force }
    New-Item -ItemType Directory -Force $parityDir | Out-Null
    $compared = 0
    $worst = 0.0
    foreach ($case in $PlanCases)
    {
        if ($case.Nondeterministic) { continue }
        # Images: final render target and presented output are gated; the back buffer (ImGui, live timings) is reported
        foreach ($capture in @(@{ Suffix = "_final.png"; Gate = $true; Limit = $MaxDiffPercent }, @{ Suffix = "_output.png"; Gate = $true; Limit = $MaxDiffPercent },
                               @{ Suffix = "_magnifier.png"; Gate = $true; Limit = $MaxDiffPercent }, @{ Suffix = "_backbuffer.png"; Gate = $false; Limit = 1.0 }))
        {
            $a = Join-Path $OutDir "D3D12\core\$($case.Name)$($capture.Suffix)"
            $b = Join-Path $OutDir "Vulkan\core\$($case.Name)$($capture.Suffix)"
            if (-not ((Test-Path $a) -and (Test-Path $b))) { continue }
            $diffPath = Join-Path $parityDir "$($case.Name)$($capture.Suffix.Replace('.png', '_diff.png'))"
            $percent = [SlimGraphicsImageDiff]::Compare($a, $b, $diffPath, 24)
            if ($capture.Gate) { $compared++; $worst = [Math]::Max($worst, $percent) }
            if ($percent -lt 0 -or $percent -gt $capture.Limit)
            {
                $message = "D3D12 vs Vulkan $($case.Name)$($capture.Suffix): $([Math]::Round($percent, 3))% of pixels differ (limit $($capture.Limit)%, diff: $diffPath)"
                if ($capture.Gate) { Add-Failure $message } else { Add-Note $message }
            }
        }
        # Statistics
        if (-not ($StatsD3D12.ContainsKey($case.Name) -and $StatsVulkan.ContainsKey($case.Name))) { continue }
        $d = $StatsD3D12[$case.Name]
        $v = $StatsVulkan[$case.Name]
        foreach ($key in $ExactStats + $TolerantStats)
        {
            if (-not ($d.ContainsKey($key) -and $v.ContainsKey($key))) { continue }
            if ($case.TimingDependentStats -contains $key) { continue }
            $difference = [Math]::Abs($d[$key] - $v[$key])
            $allowed = if ($ExactStats -contains $key) { 0 } else { [Math]::Max($d[$key], $v[$key]) * $StatTolerance + $StatSlack }
            if ($difference -gt $allowed)
            {
                Add-Failure "D3D12 vs Vulkan $($case.Name) $key`: $($d[$key]) vs $($v[$key])"
            }
        }
    }
    Write-Host ("  {0} captures compared, largest difference {1:N3}% of pixels" -f $compared, $worst)
    if ($compared -eq 0) { Add-Failure "D3D12 vs Vulkan: no captures to compare" }
}

# ---------------------------------------------------------------------------------------------------------------------

New-Item -ItemType Directory -Force $OutDir | Out-Null
Write-Host "Slim Graphics validation - output in $OutDir"

if (-not $SkipBuild)
{
    $script:MSBuild = Get-MSBuild
    if (-not $script:MSBuild) { Add-Failure "MSBuild not found (install Visual Studio 2022 with the C++ workload)" }
}
if ($Backends -contains "Vulkan" -and -not $env:VULKAN_SDK) { Add-Failure "VULKAN_SDK is not set: install the Vulkan SDK (needed for vulkan-1.lib and the validation layer)" }
Test-SubmodulePatches

$compiler = Join-Path $Root "Binaries\SlimShaderCompiler.exe"
$compilerSource = Join-Path $Root "SlimShaderCompiler\Program.cs"
if ((Get-Item $compilerSource).LastWriteTime -gt (Get-Item $compiler).LastWriteTime.AddMinutes(1))
{
    Add-Note "SlimShaderCompiler\Program.cs is newer than Binaries\SlimShaderCompiler.exe - rebuild the compiler (see CLAUDE.md) if it changed"
}

$planPath = (Resolve-Path -LiteralPath $TestPlan).Path
if ($Cases.Count -gt 0) { $planPath = New-FilteredPlan $planPath $Cases }
$planCases = @()
if ($planPath) { $planCases = @(Get-PlanCases $planPath) }

$sessionStats = @{}
if ($Failures.Count -eq 0)
{
    foreach ($backend in $Backends)
    {
        $debugDir = Join-Path $SharpmakeDir "projects\output\win64\debug"
        $exe = Join-Path $debugDir ("slimgraphicsapplication_{0}.exe" -f $backend.ToLower())
        if (-not $SkipBuild)
        {
            Write-Step "$backend build"
            if (-not (Invoke-Sharpmake $backend)) { continue }
            if (-not (Invoke-Build $backend "Debug")) { continue }
            Copy-Item -LiteralPath (Join-Path $debugDir "slimgraphicsapplication.exe") -Destination $exe -Force
            if ($BuildRelease) { Invoke-Build $backend "Release" | Out-Null }
        }
        if (-not (Test-Path $exe)) { Add-Failure "$exe not found (run without -SkipBuild)"; continue }

        foreach ($pass in (Get-Passes $backend))
        {
            Write-Step "$backend tests: $($pass.Name) validation ($($planCases.Count) cases in one session)"
            $sessionStats["$backend/$($pass.Name)"] = Invoke-TestSession $backend $exe $pass $planPath $planCases
        }
    }

    if ($Backends.Count -eq 2 -and $sessionStats.ContainsKey("D3D12/core") -and $sessionStats.ContainsKey("Vulkan/core"))
    {
        Write-Step "D3D12 / Vulkan parity"
        Compare-Backends $planCases $sessionStats["D3D12/core"] $sessionStats["Vulkan/core"]
    }
    else
    {
        Add-Note "Parity not compared (both backends' core passes are needed)"
    }
}

Write-Step "Summary"
if ($Results.Count -gt 0)
{
    $Results | Group-Object Backend, Pass | ForEach-Object {
        $failed = @($_.Group | Where-Object { $_.Status -eq "FAIL" }).Count
        $skippedCount = @($_.Group | Where-Object { $_.Status -eq "SKIP" }).Count
        $ran = $_.Count - $skippedCount
        Write-Host ("  {0,-24} {1,3} cases, {2,3} failed{3}" -f $_.Name, $ran, $failed, $(if ($skippedCount -gt 0) { ", $skippedCount skipped (other backend only)" } else { "" })) -ForegroundColor $(if ($failed -eq 0) { "Green" } else { "Red" })
    }
}
if (-not $SkipBuild -and $Backends.Count -gt 0)
{
    $last = $Backends[$Backends.Count - 1]
    $Notes.Add(("Project files are now generated for {0} (generate_project_files.bat{1} to switch)" -f $last, $(if ($last -eq "Vulkan") { "" } else { " /vulkan" })))
}
foreach ($note in $Notes) { Write-Host "NOTE: $note" -ForegroundColor Yellow }
if ($Failures.Count -gt 0)
{
    Write-Host ""
    Write-Host "VALIDATION FAILED ($($Failures.Count) problems):" -ForegroundColor Red
    foreach ($failure in $Failures) { Write-Host "  - $failure" -ForegroundColor Red }
    exit 1
}
Write-Host ""
Write-Host "VALIDATION PASSED" -ForegroundColor Green
exit 0
