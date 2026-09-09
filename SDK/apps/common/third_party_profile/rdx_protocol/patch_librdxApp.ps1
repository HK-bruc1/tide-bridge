#Requires -Version 5.1

[CmdletBinding()]
param(
    [string]$InputArchive = '',
    [string]$OutputArchive = '',
    [string]$ToolchainBin = 'C:\JL\pi32\bin'
)

$ErrorActionPreference = 'Stop'
$ExpectedInputSha256 = '4289EC0F6D923EC9337A5DBE57F8D720BCC4946601B7D8393F9E2878B16F5C7D'

if ([string]::IsNullOrWhiteSpace($InputArchive)) {
    $InputArchive = Join-Path $PSScriptRoot 'librdxApp.a'
}
if ([string]::IsNullOrWhiteSpace($OutputArchive)) {
    $OutputArchive = Join-Path $PSScriptRoot 'librdxApp_patched.a'
}

$InputArchive = (Resolve-Path -LiteralPath $InputArchive).Path
$OutputArchive = [IO.Path]::GetFullPath($OutputArchive)
if ($InputArchive -eq $OutputArchive) {
    throw 'The patched archive must not overwrite the original librdxApp.a.'
}

$Clang = Join-Path $ToolchainBin 'clang.exe'
$Ar = Join-Path $ToolchainBin 'llvm-ar.exe'
$Nm = Join-Path $ToolchainBin 'llvm-nm.exe'
foreach ($Tool in @($Clang, $Ar, $Nm)) {
    if (!(Test-Path -LiteralPath $Tool -PathType Leaf)) {
        throw "Required JL toolchain executable not found: $Tool"
    }
}

$ActualInputSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $InputArchive).Hash
if ($ActualInputSha256 -ne $ExpectedInputSha256) {
    throw "Unsupported librdxApp.a revision. Expected SHA-256 $ExpectedInputSha256, got $ActualInputSha256."
}

function Invoke-CheckedTool {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Tool failed with exit code ${LASTEXITCODE}: $FilePath $($Arguments -join ' ')"
    }
}

function Replace-ExactlyOnce {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$OldText,
        [Parameter(Mandatory = $true)][string]$NewText,
        [Parameter(Mandatory = $true)][string]$PatchName
    )

    $Count = ([regex]::Matches($Text, [regex]::Escape($OldText))).Count
    if ($Count -ne 1) {
        throw "IR patch '$PatchName' expected exactly one match, found $Count."
    }
    return $Text.Replace($OldText, $NewText)
}

$TempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
$TempDir = [IO.Path]::GetFullPath(
    (Join-Path $TempRoot ("rdx-library-patch-" + [guid]::NewGuid().ToString('N'))))
if ([IO.Path]::GetDirectoryName($TempDir).TrimEnd('\', '/') -ne $TempRoot) {
    throw "Refusing to use an unexpected temporary path: $TempDir"
}
New-Item -ItemType Directory -Path $TempDir | Out-Null

try {
    $ObjectPath = Join-Path $TempDir 'rdx_uxfile.c.o'
    $OriginalIrPath = Join-Path $TempDir 'rdx_uxfile.original.ll'
    $PatchedIrPath = Join-Path $TempDir 'rdx_uxfile.patched.ll'
    $CandidateArchive = Join-Path $TempDir 'librdxApp_patched.a'

    Push-Location $TempDir
    try {
        Invoke-CheckedTool $Ar @('x', $InputArchive, 'rdx_uxfile.c.o')
    } finally {
        Pop-Location
    }
    if (!(Test-Path -LiteralPath $ObjectPath -PathType Leaf)) {
        throw 'rdx_uxfile.c.o was not extracted from librdxApp.a.'
    }

    Invoke-CheckedTool $Clang @('-S', '-emit-llvm', '-target', 'pi32v2', '-mcpu=r3', '-x', 'ir', $ObjectPath, '-o', $OriginalIrPath)
    $Ir = [IO.File]::ReadAllText($OriginalIrPath).Replace("`r`n", "`n")

    $Ir = Replace-ExactlyOnce $Ir `
        '  call void @llvm.dbg.value(metadata i8 0, i64 0, metadata !4857, metadata !650), !dbg !4908' `
        '  call void @llvm.dbg.value(metadata i8 -86, i64 0, metadata !4857, metadata !650), !dbg !4908' `
        'format completion marker debug value'
    $Ir = Replace-ExactlyOnce $Ir `
        '  store i8 0, i8* %3, align 1, !dbg !4908, !tbaa !710' `
        '  store i8 -86, i8* %3, align 1, !dbg !4908, !tbaa !710' `
        'format completion marker value'
    $Ir = Replace-ExactlyOnce $Ir `
        '  store i8 0, i8* @g_dat_vm_flag_written, align 1, !dbg !4911, !tbaa !972' `
        "  store i8 1, i8* @g_dat_vm_flag_written, align 1, !dbg !4911, !tbaa !972`n  store i1 false, i1* @g_dat_need_upgrade_rebuild, align 4" `
        'format runtime state'

    # rdx_dat_cache_save() aliases its 248-byte marks buffer through a pointer,
    # then uses sizeof(pointer) for every capacity/boundary expression.  On
    # BR28 that becomes 4: even after fixing snprintf's capacity, the loop still
    # exits when the first offset makes pos > 3 and closes the JSON as [].  Fix
    # all four sizeof(pointer)-derived sites, including the full-buffer fallback.
    $Ir = Replace-ExactlyOnce $Ir `
        '  %113 = sub i32 4, %105, !dbg !1146' `
        '  %113 = sub i32 248, %105, !dbg !1146' `
        'record marks persistence buffer capacity'
    $Ir = Replace-ExactlyOnce $Ir `
        '  %122 = icmp ugt i32 %121, 3, !dbg !1157' `
        '  %122 = icmp ugt i32 %121, 247, !dbg !1157' `
        'record marks persistence append boundary'
    $Ir = Replace-ExactlyOnce $Ir `
        '  %126 = icmp ult i32 %125, 4, !dbg !1165' `
        '  %126 = icmp ult i32 %125, 248, !dbg !1165' `
        'record marks persistence closing boundary'
    $Ir = Replace-ExactlyOnce $Ir `
        '  store i8 93, i8* getelementptr inbounds ([248 x i8], [248 x i8]* @s_marks_str, i32 0, i32 2), align 1, !dbg !1173, !tbaa !710' `
        '  store i8 93, i8* getelementptr inbounds ([248 x i8], [248 x i8]* @s_marks_str, i32 0, i32 246), align 1, !dbg !1173, !tbaa !710' `
        'record marks persistence fallback closing bracket'
    $Ir = Replace-ExactlyOnce $Ir `
        '  %133 = phi i8* [ getelementptr inbounds ([248 x i8], [248 x i8]* @s_marks_str, i32 0, i32 3), %131 ], [ %130, %127 ]' `
        '  %133 = phi i8* [ getelementptr inbounds ([248 x i8], [248 x i8]* @s_marks_str, i32 0, i32 247), %131 ], [ %130, %127 ]' `
        'record marks persistence fallback terminator'

    $Ir = Replace-ExactlyOnce $Ir `
        '  %218 = phi i32 [ 0, %59 ], [ -1, %49 ], [ -1, %44 ], [ -2, %96 ], [ %88, %215 ], [ %88, %216 ], [ %88, %210 ]' `
        '  %218 = phi i32 [ 0, %59 ], [ -1, %49 ], [ -1, %44 ], [ -2, %96 ], [ %88, %215 ], [ -3, %216 ], [ %88, %210 ]' `
        'scan save failure result'

    # A missing DAT plus an empty cache is the normal factory-new state.  The
    # original fallback recursively scans the whole FAT volume and is known to
    # stay inside fscan() indefinitely on some otherwise mountable new NANDs.
    # Commit an empty index instead; orphan-file recovery must be an explicit
    # maintenance operation, not a boot prerequisite.
    $Ir = Replace-ExactlyOnce $Ir `
        '  br i1 %25, label %26, label %219, !dbg !5153' `
        '  br i1 %25, label %221, label %219, !dbg !5153' `
        'factory-new empty index fast path'
    $Ir = Replace-ExactlyOnce $Ir `
        '  %222 = phi i32 [ %218, %217 ], [ %220, %219 ]' `
        '  %222 = phi i32 [ %218, %217 ], [ %220, %219 ], [ 0, %15 ]' `
        'factory-new empty index result'

    $OldCompletion = @'
; <label>:227:                                    ; preds = %226, %224
  store i1 false, i1* @g_dat_need_upgrade_rebuild, align 4
'@
    $NewCompletion = @'
; <label>:227:                                    ; preds = %226, %224
  %rdx_patch_scan_succeeded = icmp sge i32 %222, 0
  %rdx_patch_scan_flag = select i1 %rdx_patch_scan_succeeded, i8 -86, i8 0
  store i8 %rdx_patch_scan_flag, i8* %5, align 1, !tbaa !710
  %rdx_patch_syscfg_result = call i32 @syscfg_write(i16 zeroext 159, i8* nonnull %5, i16 zeroext 1) #11, !dbg !5458
  %rdx_patch_vm_written = zext i1 %rdx_patch_scan_succeeded to i8
  store i8 %rdx_patch_vm_written, i8* @g_dat_vm_flag_written, align 1, !tbaa !972
  store i1 false, i1* @g_dat_need_upgrade_rebuild, align 4
'@
    $Ir = Replace-ExactlyOnce $Ir $OldCompletion $NewCompletion 'persist completed scan marker'

    $OldScanActive = @'
; Function Attrs: minsize norecurse nounwind optsize readnone
define zeroext i8 @rdx_uxfile_is_scan_active() local_unnamed_addr #9 !dbg !6111 {
  ret i8 0, !dbg !6112
}
'@
    $NewScanActive = @'
; Function Attrs: minsize norecurse nounwind optsize
define zeroext i8 @rdx_uxfile_is_scan_active() local_unnamed_addr #4 !dbg !6111 {
  %rdx_patch_sync_state = load volatile i32, i32* @g_sync_state, align 4, !tbaa !710
  %rdx_patch_sync_started = icmp uge i32 %rdx_patch_sync_state, 1
  %rdx_patch_sync_not_completed = icmp ule i32 %rdx_patch_sync_state, 3
  %rdx_patch_sync_active = and i1 %rdx_patch_sync_started, %rdx_patch_sync_not_completed
  %rdx_patch_rebuild_pending = load i1, i1* @g_dat_need_upgrade_rebuild, align 4
  %rdx_patch_scan_active = or i1 %rdx_patch_sync_active, %rdx_patch_rebuild_pending
  %rdx_patch_scan_active_u8 = zext i1 %rdx_patch_scan_active to i8
  ret i8 %rdx_patch_scan_active_u8, !dbg !6112
}
'@
    $Ir = Replace-ExactlyOnce $Ir $OldScanActive $NewScanActive 'scan activity state query'

    . (Join-Path $PSScriptRoot 'patch_librdxApp_storage.ps1')
    $Ir = Update-RdxStorageIr $Ir

    $Utf8NoBom = New-Object System.Text.UTF8Encoding -ArgumentList $false
    [IO.File]::WriteAllText($PatchedIrPath, $Ir, $Utf8NoBom)

    Invoke-CheckedTool $Clang @('-c', '-emit-llvm', '-target', 'pi32v2', '-mcpu=r3', '-x', 'ir', $PatchedIrPath, '-o', $ObjectPath)
    $ObjectHeader = [IO.File]::ReadAllBytes($ObjectPath)[0..3]
    if (($ObjectHeader | ForEach-Object { $_.ToString('X2') }) -join ' ' -ne '42 43 C0 DE') {
        throw 'Patched rdx_uxfile.c.o is not LLVM bitcode.'
    }

    Copy-Item -LiteralPath $InputArchive -Destination $CandidateArchive -Force
    Push-Location $TempDir
    try {
        Invoke-CheckedTool $Ar @('r', $CandidateArchive, 'rdx_uxfile.c.o')
    } finally {
        Pop-Location
    }

    $Members = @(& $Ar 't' $CandidateArchive)
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to list patched archive members.'
    }
    $ExpectedMembers = @(
        'rdx_encryption.c.o',
        'rdx_protocol.c.o',
        'rdx_queue.c.o',
        'rdx_uxfile.c.o',
        'xxpUart.c.o'
    )
    if (($Members -join "`n") -ne ($ExpectedMembers -join "`n")) {
        throw "Patched archive member list is unexpected: $($Members -join ', ')"
    }

    $Symbols = @(& $Nm '--defined-only' $CandidateArchive)
    if ($LASTEXITCODE -ne 0) {
        throw 'Patched archive symbol verification failed.'
    }
    foreach ($Symbol in @('rdx_uxfile_is_scan_active', 'rdx_uxfile_finish_record',
                          'rdx_uxfile_fence_request', 'rdx_uxfile_fence_poll',
                          'rdx_uxfile_pc_returned', 'rdx_uxfile_pc_refresh_status',
                          'rdx_uxfile_storage_status')) {
        if (($Symbols -join "`n") -notmatch ('\b' + $Symbol + '\b')) {
            throw "Patched archive missing ABI symbol: $Symbol"
        }
    }

    # Publish only after the candidate compiles and its ABI/members verify.
    Copy-Item -LiteralPath $CandidateArchive -Destination $OutputArchive -Force

    $OutputHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $OutputArchive).Hash
    Write-Host "Created patched RDX archive: $OutputArchive"
    Write-Host "Input SHA-256 : $ActualInputSha256"
    Write-Host "Output SHA-256: $OutputHash"
} finally {
    if (Test-Path -LiteralPath $TempDir) {
        Remove-Item -LiteralPath $TempDir -Recurse -Force
    }
}
