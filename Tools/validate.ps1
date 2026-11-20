#Requires -Version 5.1
<#
.SYNOPSIS
    Builds Slim Graphics for each rendering backend, runs the automated scenarios with the graphics API validation layers
    enabled and checks the results.

.DESCRIPTION
    For each backend (D3D12, Vulkan) this:
      1. Regenerates the project files (Sharpmake, /vulkan for Vulkan) and builds the Debug application (shaders are
         compiled by the pre-build step). Build errors fail; warnings in SlimGraphicsVulkan fail; other first party
         warnings are reported.
      2. Runs every scenario (model viewer vertex / mesh / amplification paths, GPU driven rendering scenes), capturing
         frame N and exiting. A run fails if it times out (an assert dialog or hang), exits with a non-zero code, logs any
         validation error / warning / corruption message or assert, does not report that its validation layer loaded, or
         does not write its captures.
      3. Compares the D3D12 and Vulkan final renders pixel by pixel (back buffers with ImGui are reported only: mouse
         hover highlights differ between runs).
    -Extended adds passes with D3D12 GPU-based validation, Vulkan synchronization validation and Vulkan GPU-assisted
    validation.

    Run from any directory:
        powershell -ExecutionPolicy Bypass -File Tools\validate.ps1 [-Extended] [-Backends Vulkan] [-SkipBuild]

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
.PARAMETER OutDir
    Where build logs, run logs, captures and diff images are written.
.PARAMETER MaxDiffPercent
    Largest percentage of pixels allowed to differ (by more than 24/255 in any channel) between the backends' final renders.
#>
[CmdletBinding()]
param(
    [ValidateSet("D3D12", "Vulkan")]
    [string[]]$Backends = @("D3D12", "Vulkan"),
    [switch]$Extended,
    [switch]$BuildRelease,
    [switch]$SkipBuild,
    [switch]$ApplySubmodulePatches,
    [string]$OutDir = (Join-Path $env:TEMP "SlimGraphicsValidation"),
    [int]$CaptureFrame = 30,
    [int]$TimeoutSeconds = 120,
    [double]$MaxDiffPercent = 0.05
)

$ErrorActionPreference = "Continue" # Native tool failures are checked through exit codes
$Root = Split-Path -Parent $PSScriptRoot
$AppDir = Join-Path $Root "SlimGraphicsApplication"
$SharpmakeDir = Join-Path $Root "Sharpmake"
$AppProject = Join-Path $SharpmakeDir "projects\slimgraphicsapplication_vs2022_win64.vcxproj"
$Failures = New-Object System.Collections.Generic.List[string]
$Notes = New-Object System.Collections.Generic.List[string]
$Results = New-Object System.Collections.Generic.List[object]

$Scenarios = @(
    @{ Name = "default";     Args = @() },
    @{ Name = "mesh_vs";     Args = @("-mesh_mode=0") },
    @{ Name = "mesh_ms";     Args = @("-mesh_mode=1") },
    @{ Name = "mesh_as_ms";  Args = @("-mesh_mode=2") },
    @{ Name = "gdr_heads";   Args = @("-gdr_scene=../SlimGraphicsAssets/GDRAssets/HeadsTest.json") },
    @{ Name = "gdr_spheres"; Args = @("-gdr_scene=../SlimGraphicsAssets/GDRAssets/SpheresTest.json") },
    # 265 instances of 27 meshes: deep hierarchies, multi part meshes, occluders, off screen and sub pixel instances
    @{ Name = "gdr_mixed";   Args = @("-gdr_scene=../SlimGraphicsAssets/GDRAssets/MixedAssetsTest.json") }
)

# Lines that count as validation problems in run logs (see the D3D12 / Vulkan debug message callbacks and se::Dbg::Assert)
$IssuePattern = '^\[(Vulkan|D3D12) (ERROR|WARNING|CORRUPTION)\]|^Assert:'

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
    # Assert / crash dialogs block the process; read their text so the failure is actionable
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

function Invoke-Scenario([string]$Backend, [string]$Exe, $Pass, $Scenario)
{
    $dir = Join-Path $OutDir "$Backend\$($Pass.Name)\$($Scenario.Name)"
    if (Test-Path $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Force $dir | Out-Null
    $log = Join-Path $dir "log.txt"
    $label = "$Backend/$($Pass.Name)/$($Scenario.Name)"

    $environment = @{ SE_LOG_FILE = $log }
    foreach ($key in $Pass.Env.Keys) { $environment[$key] = $Pass.Env[$key] }
    $saved = @{}
    foreach ($key in $environment.Keys)
    {
        $saved[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
        [Environment]::SetEnvironmentVariable($key, $environment[$key], "Process")
    }

    $problems = @()
    $exitCode = $null
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    try
    {
        $argumentList = @($Scenario.Args) + @("-capture_frame=$CaptureFrame", "-exit_after_capture", "`"-capture_dir=$dir`"")
        $process = Start-Process -FilePath $Exe -ArgumentList $argumentList -WorkingDirectory $AppDir -PassThru
        $null = $process.Handle # Keeps the exit code available after exit
        if (-not $process.WaitForExit($TimeoutSeconds * 1000))
        {
            $dialog = Get-DialogText $process.Id
            Stop-Process -Id $process.Id -Force
            $problems += "timed out after $TimeoutSeconds s" + $(if ($dialog) { " (dialog: $dialog)" } else { "" })
        }
        else
        {
            $exitCode = $process.ExitCode
            if ($exitCode -ne 0) { $problems += ("exit code 0x{0:X}" -f $exitCode) }
        }
    }
    finally
    {
        foreach ($key in $saved.Keys) { [Environment]::SetEnvironmentVariable($key, $saved[$key], "Process") }
    }
    $seconds = $stopwatch.Elapsed.TotalSeconds

    $text = @()
    if (Test-Path $log) { $text = @(Get-Content -LiteralPath $log) } else { $problems += "no log written" }
    foreach ($status in $Pass.Status)
    {
        if (-not ($text | Where-Object { $_ -eq $status })) { $problems += "validation not active: missing '$status'" }
    }
    $issues = @($text | Where-Object { $_ -match $IssuePattern } | Where-Object {
        $line = $_
        -not ($Pass.Allow | Where-Object { $line -match [regex]::Escape($_) })
    })
    if ($issues.Count -gt 0) { $problems += "$($issues.Count) validation / assert messages" }
    foreach ($capture in "capture_final.png", "capture_backbuffer.png")
    {
        if (-not (Test-Path (Join-Path $dir $capture))) { $problems += "missing $capture" }
    }

    $status = if ($problems.Count -eq 0) { "PASS" } else { "FAIL" }
    $color = if ($problems.Count -eq 0) { "Green" } else { "Red" }
    Write-Host ("  {0,-4} {1,-36} {2,5:N1}s" -f $status, $label, $seconds) -ForegroundColor $color
    if ($problems.Count -gt 0)
    {
        Add-Failure "$label`: $($problems -join '; ') (log: $log)"
        $issues | Sort-Object -Unique | Select-Object -First 10 | ForEach-Object { Write-Host "       $($_.Substring(0, [Math]::Min(300, $_.Length)))" -ForegroundColor DarkRed }
    }
    $Results.Add([pscustomobject]@{ Backend = $Backend; Pass = $Pass.Name; Scenario = $Scenario.Name; Status = $status; Seconds = [Math]::Round($seconds, 1); Issues = $issues.Count })
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
    // Returns the percentage of pixels differing by more than 'threshold' in any channel (-1 if the sizes differ) and writes
    // an amplified difference image
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

function Compare-Backends
{
    Initialize-ImageDiff
    $parityDir = Join-Path $OutDir "parity"
    New-Item -ItemType Directory -Force $parityDir | Out-Null
    foreach ($scenario in $Scenarios)
    {
        foreach ($capture in @(@{ File = "capture_final.png"; Gate = $true; Limit = $MaxDiffPercent }, @{ File = "capture_backbuffer.png"; Gate = $false; Limit = 1.0 }))
        {
            $a = Join-Path $OutDir "D3D12\core\$($scenario.Name)\$($capture.File)"
            $b = Join-Path $OutDir "Vulkan\core\$($scenario.Name)\$($capture.File)"
            if (-not ((Test-Path $a) -and (Test-Path $b))) { continue }
            $diffPath = Join-Path $parityDir "$($scenario.Name)_$($capture.File.Replace('.png', '_diff.png'))"
            $percent = [SlimGraphicsImageDiff]::Compare($a, $b, $diffPath, 24)
            $ok = $percent -ge 0 -and $percent -le $capture.Limit
            Write-Host ("  {0,-4} {1,-14} {2,-24} {3,8:N3}% of pixels differ" -f $(if ($ok) { "OK" } else { "DIFF" }), $scenario.Name, $capture.File, $percent) -ForegroundColor $(if ($ok) { "Green" } elseif ($capture.Gate) { "Red" } else { "Yellow" })
            if (-not $ok)
            {
                $message = "D3D12 vs Vulkan $($scenario.Name) $($capture.File): $([Math]::Round($percent, 3))% of pixels differ (limit $($capture.Limit)%, diff: $diffPath)"
                if ($capture.Gate) { Add-Failure $message } else { Add-Note "$message - check the ImGui overlay by eye (hover highlights vary)" }
            }
        }
    }
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
            Write-Step "$backend runs: $($pass.Name) validation"
            foreach ($scenario in $Scenarios) { Invoke-Scenario $backend $exe $pass $scenario }
        }
    }

    if ($Backends.Count -eq 2)
    {
        Write-Step "D3D12 / Vulkan parity"
        Compare-Backends
    }
    else
    {
        Add-Note "Parity not compared (only $Backends validated)"
    }
}

Write-Step "Summary"
if ($Results.Count -gt 0) { $Results | Format-Table -AutoSize | Out-String | Write-Host }
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
