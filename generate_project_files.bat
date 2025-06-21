pushd Sharpmake
Sharpmake.Application\Debug\net6.0\Sharpmake.Application.exe /sources('main.sharpmake.cs')
popd

if ERRORLEVEL 1 (
pause
)