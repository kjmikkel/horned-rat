BeforeAll {
    $script:Installer = Join-Path $PSScriptRoot '..\..\tools\install.ps1'

    function New-FakeGame([string] $Root) {
        $warfb = Join-Path $Root 'WARFB'
        New-Item -ItemType Directory -Path $warfb -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $warfb 'WHSHR.EXE') -Value 'fake exe' -NoNewline
        Set-Content -LiteralPath (Join-Path $warfb 'wing32.dll') -Value 'original wing' -NoNewline
        Set-Content -LiteralPath (Join-Path $Root 'proxy.dll') -Value 'proxy wing' -NoNewline
        $warfb
    }

    function Get-Hash([string] $Path) {
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

Describe 'install.ps1' {
    BeforeEach {
        $root = Join-Path $TestDrive ([guid]::NewGuid())
        $warfb = New-FakeGame $root
        $proxy = Join-Path $root 'proxy.dll'
        $hashes = @{
            'WHSHR.EXE'  = Get-Hash (Join-Path $warfb 'WHSHR.EXE')
            'wing32.dll' = Get-Hash (Join-Path $warfb 'wing32.dll')
        }
    }

    It 'keeps the original as wing32_orig.dll and copies the proxy' {
        & $Installer -GameDir $root -SourceDll $proxy -SupportedHashes $hashes | Out-Null
        Get-Content -Raw (Join-Path $warfb 'wing32_orig.dll') | Should -Be 'original wing'
        Get-Content -Raw (Join-Path $warfb 'wing32.dll') | Should -Be 'proxy wing'
    }

    It 'accepts the WARFB folder itself as GameDir' {
        & $Installer -GameDir $warfb -SourceDll $proxy -SupportedHashes $hashes | Out-Null
        Get-Content -Raw (Join-Path $warfb 'wing32.dll') | Should -Be 'proxy wing'
    }

    It 'refuses an unsupported WHSHR.EXE and changes nothing' {
        $hashes['WHSHR.EXE'] = 'deadbeef'
        { & $Installer -GameDir $root -SourceDll $proxy -SupportedHashes $hashes } | Should -Throw '*WHSHR.EXE*'
        Get-Content -Raw (Join-Path $warfb 'wing32.dll') | Should -Be 'original wing'
        Join-Path $warfb 'wing32_orig.dll' | Should -Not -Exist
    }

    It 'refuses an unsupported wing32.dll' {
        $hashes['wing32.dll'] = 'deadbeef'
        { & $Installer -GameDir $root -SourceDll $proxy -SupportedHashes $hashes } | Should -Throw '*wing32.dll*'
    }

    It 'installs anyway with -Force' {
        $hashes['WHSHR.EXE'] = 'deadbeef'
        & $Installer -GameDir $root -SourceDll $proxy -SupportedHashes $hashes -Force | Out-Null
        Get-Content -Raw (Join-Path $warfb 'wing32.dll') | Should -Be 'proxy wing'
    }

    It 'is safe to run twice: the original is never overwritten' {
        & $Installer -GameDir $root -SourceDll $proxy -SupportedHashes $hashes | Out-Null
        & $Installer -GameDir $root -SourceDll $proxy -SupportedHashes $hashes | Out-Null
        Get-Content -Raw (Join-Path $warfb 'wing32_orig.dll') | Should -Be 'original wing'
    }

    It 'uninstalls back to the original state' {
        & $Installer -GameDir $root -SourceDll $proxy -SupportedHashes $hashes | Out-Null
        & $Installer -GameDir $root -Uninstall -SupportedHashes $hashes | Out-Null
        Get-Content -Raw (Join-Path $warfb 'wing32.dll') | Should -Be 'original wing'
        Join-Path $warfb 'wing32_orig.dll' | Should -Not -Exist
    }

    It 'refuses to uninstall when not installed and points to GOG Verify / Repair' {
        { & $Installer -GameDir $root -Uninstall -SupportedHashes $hashes } | Should -Throw '*Verify / Repair*'
    }

    It 'fails clearly when WHSHR.EXE cannot be found' {
        { & $Installer -GameDir $TestDrive -SourceDll $proxy -SupportedHashes $hashes } | Should -Throw '*WHSHR.EXE not found*'
    }
}
