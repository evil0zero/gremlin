param(
    [Parameter(Mandatory)][string]$Stage,
    [Parameter(Mandatory)][string]$CMake,
    [Parameter(Mandatory)][string]$CTest,
    [Parameter(Mandatory)][string]$Ninja,
    [Parameter(Mandatory)][string]$Compiler,
    [string]$SQLiteRoot,
    [string]$JsonRoot
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$logs = Join-Path $root "out/logs/stages/$Stage"
$reports = Join-Path $root 'out/reports'
$tempDir = Join-Path $root 'out/tmp'
New-Item -ItemType Directory -Force $logs,$reports,$tempDir | Out-Null
$env:TEMP = $tempDir
$env:TMP = $tempDir
$env:Path = "$(Split-Path $Compiler);$env:Path"
$sqliteOptions = @()
if ($SQLiteRoot) {
    $sqliteOptions = @(
        "-DSQLite3_INCLUDE_DIR=$(Join-Path $SQLiteRoot 'include')",
        "-DSQLite3_LIBRARY=$(Join-Path $SQLiteRoot 'lib/sqlite3.lib')",
        "-DGREMLIN_SQLITE_RUNTIME=$(Join-Path $SQLiteRoot 'bin/sqlite3.dll')")
}
$jsonOptions = @()
if ($JsonRoot) {
    $jsonOptions += "-Dnlohmann_json_DIR=$(Join-Path $JsonRoot 'share/cmake/nlohmann_json')"
}

function Invoke-Logged([string]$Label, [string]$Exe, [string[]]$Arguments) {
    $file = Join-Path $logs "$Label.log"
    ConvertTo-Json -Compress -InputObject (@($Exe) + $Arguments) | Set-Content $file
    & $Exe @Arguments *>&1 | Tee-Object -FilePath $file -Append
    $code = $LASTEXITCODE
    "exit=$code" | Tee-Object -FilePath $file -Append
    if ($code -ne 0) { throw "$Label failed with exit $code. See $file" }
}

foreach ($profile in @('Debug','Release')) {
    $name = $profile.ToLowerInvariant()
    $buildDir = Join-Path $root "out/build/windows-$name"
    $configureArgs = @('-S',$root,'-B',$buildDir,'-G','Ninja',
        "-DCMAKE_MAKE_PROGRAM=$Ninja","-DCMAKE_CXX_COMPILER=$Compiler","-DCMAKE_BUILD_TYPE=$profile",
        '-DBUILD_TESTING=ON','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON') + $sqliteOptions + $jsonOptions
    Invoke-Logged "$name-configure" $CMake $configureArgs
    Invoke-Logged "$name-build" $CMake @('--build',$buildDir,'--parallel')
    Invoke-Logged "$name-test" $CTest @('--test-dir',$buildDir,'--no-tests=error',
        '--output-on-failure','--output-junit',(Join-Path $reports "$Stage-$name.xml"))
}
$revision = Get-ChildItem $root -File -Recurse |
    Where-Object { $_.FullName -notlike "$root\out\*" -and $_.FullName -notlike "$root\.git\*" } |
    Sort-Object FullName | ForEach-Object {
        [ordered]@{ path = [IO.Path]::GetRelativePath($root,$_.FullName); sha256 = (Get-FileHash $_.FullName).Hash }
    }
ConvertTo-Json -InputObject @($revision) | Set-Content (Join-Path $reports "$Stage-source.json")
