# Opt-in: verifies the local game install is the build this project supports (spec §8).
Describe 'local game install' -Skip:(-not $env:SOTHR_GAME_DIR) {
    BeforeAll {
        $warfb = Join-Path $env:SOTHR_GAME_DIR 'WARFB'
        function Get-Hash([string] $Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
    }

    It 'has the supported WHSHR.EXE' {
        Get-Hash (Join-Path $warfb 'WHSHR.EXE') | Should -Be 'df462ba4dc29d0832c44f14207b47459f281e6dd4a9e3f3ad972fd43a5921326'
    }

    It 'has the supported original wing32.dll' {
        $orig = Join-Path $warfb 'wing32_orig.dll'
        $path = if (Test-Path -LiteralPath $orig) { $orig } else { Join-Path $warfb 'wing32.dll' }
        Get-Hash $path | Should -Be 'dfc3a949141dad973ad2eb4b66e3c8dc8dc7851fe8f360cd42697aab439d70c0'
    }
}
