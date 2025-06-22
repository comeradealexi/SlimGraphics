pushd Sharpmake
Sharpmake.Application\Debug\net6.0\Sharpmake.Application.exe /sources('main.sharpmake.cs') /generateDebugSolution /debugSolutionPath('sharpmake_projects')
popd

if ERRORLEVEL 1 (
pause
)

mklink .\slimgraphics_vs2022.sln .\Sharpmake\projects\slimgraphics_vs2022.sln