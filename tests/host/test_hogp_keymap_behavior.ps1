#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$Failed = 0

function Test-Contract {
    param([string]$Name, [bool]$Passed, [string]$Message)
    if ($Passed) {
        Write-Host "PASS: $Name"
        return
    }
    Write-Host "FAIL: ${Name}: $Message"
    $script:Failed++
}

function Convert-HexToBytes([string]$Hex) {
    if (($Hex.Length % 2) -ne 0) { throw 'odd-length HEX' }
    $bytes = [byte[]]::new($Hex.Length / 2)
    for ($i = 0; $i -lt $bytes.Length; $i++) {
        $bytes[$i] = [Convert]::ToByte($Hex.Substring($i * 2, 2), 16)
    }
    return $bytes
}

function Convert-BytesToHex([byte[]]$Data) {
    return (($Data | ForEach-Object { $_.ToString('X2') }) -join '')
}

function Get-Le16([byte[]]$Data, [int]$Offset) {
    return [BitConverter]::ToUInt16($Data, $Offset)
}

function Get-Le32([byte[]]$Data, [int]$Offset) {
    return [BitConverter]::ToUInt32($Data, $Offset)
}

function Get-Crc32([byte[]]$Data, [int]$Length = -1) {
    if ($Length -lt 0) { $Length = $Data.Length }
    [uint64]$crc = 4294967295
    for ($i = 0; $i -lt $Length; $i++) {
        $crc = $crc -bxor $Data[$i]
        for ($bit = 0; $bit -lt 8; $bit++) {
            if (($crc -band 1) -ne 0) {
                $crc = (($crc -shr 1) -bxor 3988292384) -band 4294967295
            } else {
                $crc = ($crc -shr 1) -band 4294967295
            }
        }
    }
    return [uint32](($crc -bxor 4294967295) -band 4294967295)
}

function Test-Frame([string]$Name, [string]$Hex, [byte]$Opcode,
                    [uint16]$RequestId, [uint32]$Revision, [uint16]$PayloadLen) {
    $frame = Convert-HexToBytes $Hex
    $declaredLen = Get-Le16 $frame 8
    $crc = Get-Le32 $frame ($frame.Length - 4)
    Test-Contract "${Name}_VERSION" ($frame[0] -eq 1) 'version must be 1'
    Test-Contract "${Name}_OPCODE" ($frame[1] -eq $Opcode) 'opcode mismatch'
    Test-Contract "${Name}_REQUEST_ID" ((Get-Le16 $frame 2) -eq $RequestId) 'request ID mismatch'
    Test-Contract "${Name}_REVISION" ((Get-Le32 $frame 4) -eq $Revision) 'revision mismatch'
    Test-Contract "${Name}_PAYLOAD_LEN" ($declaredLen -eq $PayloadLen) 'payload length mismatch'
    Test-Contract "${Name}_FRAME_LEN" ($frame.Length -eq (10 + $declaredLen + 4)) 'frame length mismatch'
    Test-Contract "${Name}_CRC32" ($crc -eq (Get-Crc32 $frame ($frame.Length - 4))) 'CRC32 mismatch'
    return ,$frame
}

function Test-Keymap([byte[]]$Payload) {
    if ($Payload.Length -ne 35) { return $false }
    for ($key = 0; $key -lt 5; $key++) {
        $seenZero = $false
        $seen = @{}
        for ($usageIndex = 1; $usageIndex -le 6; $usageIndex++) {
            $usage = $Payload[$key * 7 + $usageIndex]
            if ($usage -eq 0) { $seenZero = $true; continue }
            if ($seenZero -or $seen.ContainsKey($usage)) { return $false }
            if (-not (($usage -ge 0x04 -and $usage -le 0xA4) -or
                      ($usage -ge 0xB0 -and $usage -le 0xDD))) { return $false }
            $seen[$usage] = $true
        }
    }
    return $true
}

$set = Test-Frame 'SET' `
    '01012A000700000023000106000000000001190000000000011B0000000000002A000000000000280000000000402956B8' `
    0x01 42 7 35
$payload = [byte[]]$set[10..44]
Test-Contract 'KEYMAP_VALID' (Test-Keymap $payload) 'A2 keymap vector must be valid'
Test-Contract 'KEYMAP_CRC32' ((Get-Crc32 $payload) -eq [uint32]3239322755) 'canonical keymap CRC mismatch'

$getRsp = Test-Frame 'GET_RSP' `
    '01822B00080000002400000106000000000001190000000000011B0000000000002A0000000000002800000000003311346E' `
    0x82 43 8 36
Test-Contract 'GET_RSP_STATUS' ($getRsp[10] -eq 0) 'GET status must be OK'
Test-Contract 'GET_RSP_KEYMAP' ((Convert-BytesToHex ([byte[]]$getRsp[11..45])) -eq (Convert-BytesToHex $payload)) `
    'GET response must return the committed keymap'

$capsRsp = Test-Frame 'CAPS_RSP' '01832C0008000000050000050603FF141E7965' 0x83 44 8 5
Test-Contract 'CAPS_PAYLOAD' ((Convert-BytesToHex ([byte[]]$capsRsp[10..14])) -eq '00050603FF') `
    'capabilities payload mismatch'

$invalidGap = [byte[]]$payload.Clone()
$invalidGap[1] = 0
$invalidGap[2] = 0x06
Test-Contract 'KEYMAP_REJECTS_GAP' (-not (Test-Keymap $invalidGap)) 'usage after zero must fail'
$invalidDuplicate = [byte[]]$payload.Clone()
$invalidDuplicate[2] = $invalidDuplicate[1]
Test-Contract 'KEYMAP_REJECTS_DUPLICATE' (-not (Test-Keymap $invalidDuplicate)) 'duplicate usage must fail'
$invalidModifierUsage = [byte[]]$payload.Clone()
$invalidModifierUsage[1] = 0xE0
Test-Contract 'KEYMAP_REJECTS_MODIFIER_USAGE' (-not (Test-Keymap $invalidModifierUsage)) `
    'modifier usage must be encoded in the modifier bitmap'

function Invoke-TransactionModel([string]$FailurePoint) {
    $state = [ordered]@{ OldVm = $true; Prepared = $false; Ram = 'old'; Committed = $false; Revision = 7 }
    if ($FailurePoint -eq 'prepare') { return $state }
    $state.Prepared = $true
    if ($FailurePoint -eq 'apply') { return $state }
    $state.Ram = 'new'
    if ($FailurePoint -eq 'commit') { $state.Ram = 'old'; return $state }
    $state.Committed = $true
    $state.Revision = 8
    return $state
}

$prepareFail = Invoke-TransactionModel 'prepare'
Test-Contract 'TX_PREPARE_FAIL_PRESERVES_RAM' ($prepareFail.Ram -eq 'old' -and -not $prepareFail.Prepared) `
    'prepare failure must not change RAM'
$applyFail = Invoke-TransactionModel 'apply'
Test-Contract 'TX_APPLY_FAIL_NOT_COMMITTED' ($applyFail.Ram -eq 'old' -and $applyFail.Prepared -and -not $applyFail.Committed) `
    'apply failure must leave only an uncommitted prepared slot'
$commitFail = Invoke-TransactionModel 'commit'
Test-Contract 'TX_COMMIT_FAIL_ROLLS_BACK' ($commitFail.Ram -eq 'old' -and -not $commitFail.Committed -and $commitFail.Revision -eq 7) `
    'commit failure must roll RAM back and preserve revision'
$success = Invoke-TransactionModel 'none'
Test-Contract 'TX_SUCCESS_PUBLISHES_REVISION' ($success.Ram -eq 'new' -and $success.Committed -and $success.Revision -eq 8) `
    'successful transaction must publish RAM and revision together'

# This is a semantic model, not a substitute for target-side syscfg power-cut injection.
function Invoke-DisconnectModel([string]$Point) {
    $state = [ordered]@{ Prepared = $true; Ram = 'old'; Committed = $false; Ack = $false }
    if ($Point -eq 'before_apply') { return $state }
    $state.Ram = 'new'
    if ($Point -eq 'before_commit') { $state.Ram = 'old'; return $state }
    $state.Committed = $true
    if ($Point -eq 'after_commit') { return $state }
    $state.Ack = $true
    return $state
}

$disconnectBeforeApply = Invoke-DisconnectModel 'before_apply'
Test-Contract 'DISCONNECT_BEFORE_APPLY_PRESERVES_OLD_STATE' `
    ($disconnectBeforeApply.Ram -eq 'old' -and -not $disconnectBeforeApply.Committed -and -not $disconnectBeforeApply.Ack) `
    'disconnect before apply must leave old state and suppress ACK'
$disconnectBeforeCommit = Invoke-DisconnectModel 'before_commit'
Test-Contract 'DISCONNECT_BEFORE_COMMIT_ROLLS_BACK' `
    ($disconnectBeforeCommit.Ram -eq 'old' -and -not $disconnectBeforeCommit.Committed -and -not $disconnectBeforeCommit.Ack) `
    'disconnect before commit must roll RAM back and suppress ACK'
$disconnectAfterCommit = Invoke-DisconnectModel 'after_commit'
Test-Contract 'DISCONNECT_AFTER_COMMIT_REQUIRES_GET_RECONCILE' `
    ($disconnectAfterCommit.Ram -eq 'new' -and $disconnectAfterCommit.Committed -and -not $disconnectAfterCommit.Ack) `
    'a committed transaction survives disconnect but must not ACK the new connection'

Write-Host '---------------------------'
if ($Failed -eq 0) {
    Write-Host 'All HOGP keymap behavior checks passed.'
    exit 0
}
Write-Host "$Failed HOGP keymap behavior checks failed."
exit 1
