#Requires -Version 5.1
<#
.SYNOPSIS
    Validates the RDX local playback Phase 1 navigation contract.

.DESCRIPTION
    Exercises the ring-navigation specification with representative SN sets
    and freezes the source-level transaction invariants that can be checked
    without running the JL target binary.
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ProtocolDir = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol'
$PlaybackPath = Join-Path $ProtocolDir 'rdx_playback.c'
$PlaybackHeaderPath = Join-Path $ProtocolDir 'rdx_playback.h'
$AppPath = Join-Path $ProtocolDir 'rdx_app.c'

$PlaybackText = Get-Content -Raw -Path $PlaybackPath
$PlaybackHeaderText = Get-Content -Raw -Path $PlaybackHeaderPath
$AppText = Get-Content -Raw -Path $AppPath

$Checks = [System.Collections.Generic.List[object]]::new()
$Failed = 0

function Add-Check {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][bool]$Passed,
        [string]$Message = ''
    )

    $script:Checks.Add([PSCustomObject]@{
        Name = $Name
        Passed = $Passed
        Message = $Message
    })
    if (-not $Passed) {
        $script:Failed++
    }

    if ($Passed) {
        Write-Host "PASS: $Name"
    } else {
        Write-Host "FAIL: ${Name}: $Message"
    }
}

function Get-RingAdjacent {
    param(
        [uint32[]]$Items,
        [uint32]$Anchor,
        [ValidateSet('Prev', 'Next')][string]$Direction,
        [uint32[]]$Unplayable = @()
    )

    $valid = @($Items | Sort-Object -Unique | Where-Object { $_ -notin $Unplayable })
    if ($valid.Count -eq 0) {
        return [uint32]0
    }

    if ($Direction -eq 'Prev') {
        $candidate = @($valid | Where-Object { $_ -gt $Anchor } | Select-Object -First 1)
        return [uint32]$(if ($candidate.Count) { $candidate[0] } else { $valid[0] })
    }

    $candidate = @($valid | Where-Object { $_ -lt $Anchor } | Select-Object -Last 1)
    return [uint32]$(if ($candidate.Count) { $candidate[0] } else { $valid[-1] })
}

$Cases = @(
    @{ Name = 'SINGLE_NEXT_SELF'; Items = @(20); Anchor = 20; Direction = 'Next'; Expected = 20 },
    @{ Name = 'SINGLE_PREV_SELF'; Items = @(20); Anchor = 20; Direction = 'Prev'; Expected = 20 },
    @{ Name = 'BOOT_NEXT_FROM_EMPTY_HEAD'; Items = @(123, 456, 789); Anchor = 1; Direction = 'Next'; Expected = 789 },
    @{ Name = 'BOOT_PREV_FROM_EMPTY_HEAD'; Items = @(123, 456, 789); Anchor = 789; Direction = 'Prev'; Expected = 123 },
    @{ Name = 'NEXT_WRAP'; Items = @(3, 8, 11, 20); Anchor = 3; Direction = 'Next'; Expected = 20 },
    @{ Name = 'PREV_WRAP'; Items = @(3, 8, 11, 20); Anchor = 20; Direction = 'Prev'; Expected = 3 },
    @{ Name = 'NEXT_GAP'; Items = @(3, 8, 11, 20); Anchor = 8; Direction = 'Next'; Expected = 3 },
    @{ Name = 'PREV_GAP'; Items = @(3, 8, 11, 20); Anchor = 8; Direction = 'Prev'; Expected = 11 },
    @{ Name = 'NEXT_SKIP_BAD'; Items = @(3, 8, 11, 20); Anchor = 8; Direction = 'Next'; Bad = @(3); Expected = 20 },
    @{ Name = 'ALL_BAD'; Items = @(3, 8); Anchor = 3; Direction = 'Next'; Bad = @(3, 8); Expected = 0 }
)

foreach ($case in $Cases) {
    $bad = if ($case.ContainsKey('Bad')) { [uint32[]]$case.Bad } else { [uint32[]]@() }
    $actual = Get-RingAdjacent -Items ([uint32[]]$case.Items) -Anchor $case.Anchor `
        -Direction $case.Direction -Unplayable $bad
    Add-Check -Name $case.Name -Passed ($actual -eq $case.Expected) `
        -Message "expected $($case.Expected), got $actual"
}

Add-Check -Name 'STATE_FIELDS_EXPLICIT' -Passed (
    $PlaybackHeaderText -match '\bu32\s+selected_sn\s*;' -and
    $PlaybackHeaderText -match '\bu32\s+current_sn\s*;' -and
    $PlaybackHeaderText -match '\bu32\s+pending_sn\s*;'
) -Message 'selected/current/pending SN fields are required'

Add-Check -Name 'PHASE1_API_SURFACE_MINIMAL' -Passed (
    $PlaybackHeaderText -notmatch '\brdx_playback_(?:play|pause|resume)\s*\(' -and
    $PlaybackText -notmatch '\brdx_playback_(?:play|pause|resume)\s*\(' -and
    $PlaybackHeaderText -notmatch '\bPB_STATE_(?:PAUSED|SEEKING|ERROR)\b'
) -Message 'Phase 1 should expose only prev/next/ff/fr/stop and active navigation states'

Add-Check -Name 'ONE_RING_PROBE_BOUND' -Passed (
    $PlaybackText -match 'while\s*\(probed_slots\s*<\s*pb_max_sn\s*&&\s*candidate_count\s*<\s*pb\.total_count\)'
) -Message 'navigation must be bounded by one SN ring and the DAT count'

Add-Check -Name 'UNIFIED_DIRECTION_SWITCH' -Passed (
    $PlaybackText -match 'rdx_playback_prev\s*\([^)]*\)\s*\{\s*return\s+pb_switch_track\(PB_DIRECTION_NEWER\);' -and
    $PlaybackText -match 'rdx_playback_next\s*\([^)]*\)\s*\{\s*return\s+pb_switch_track\(PB_DIRECTION_OLDER\);'
) -Message 'prev and next must share the same switch transaction'

$CandidateStart = $PlaybackText.IndexOf('static pb_candidate_result_t pb_start_candidate')
$OpenStream = $PlaybackText.IndexOf('dev_flow_player_open(', $CandidateStart)
$SchedulePump = $PlaybackText.IndexOf('pb_schedule_pump(PB_PUMP_INTERVAL_MS)', $OpenStream)
$CommitSelected = $PlaybackText.IndexOf('pb.selected_sn = candidate_sn', $SchedulePump)
$CommitCurrent = $PlaybackText.IndexOf('pb.current_sn = candidate_sn', $SchedulePump)
Add-Check -Name 'COMMIT_AFTER_STARTUP' -Passed (
    $CandidateStart -ge 0 -and $OpenStream -gt $CandidateStart -and
    $SchedulePump -gt $OpenStream -and $CommitSelected -gt $SchedulePump -and
    $CommitCurrent -gt $SchedulePump
) -Message 'stable SNs must be committed after stream and pump startup'

$NextCase = [regex]::Match($AppText, '(?s)case\s+APP_MSG_REC_NEXT:.*?break;')
Add-Check -Name 'EMPTY_HEAD_ENTERS_RING_ENDPOINTS' -Passed (
    $NextCase.Success -and
    $NextCase.Value -match 'rdx_playback_next\(\);' -and
    $NextCase.Value -notmatch 'rdx_playback_play\(\);' -and
    $PlaybackText -match 'u32\s+entry_anchor_sn\s*=\s*\(direction\s*==\s*PB_DIRECTION_OLDER\)\s*\?\s*1\s*:\s*pb_max_sn;'
) -Message 'empty-head navigation must enter newest on next and oldest on prev'

$NaturalStart = $PlaybackText.IndexOf('static void pb_finish_natural')
$PumpTimerStart = $PlaybackText.IndexOf('static void pb_pump_timer_cb', $NaturalStart)
$NaturalBody = if ($NaturalStart -ge 0 -and $PumpTimerStart -gt $NaturalStart) {
    $PlaybackText.Substring($NaturalStart, $PumpTimerStart - $NaturalStart)
} else {
    ''
}
Add-Check -Name 'EOF_STOPS_WITHOUT_AUTO_NEXT' -Passed (
    $NaturalBody -match 'pb_finish_stop\(false\);' -and
    $NaturalBody -notmatch 'rdx_playback_next\(\);'
) -Message 'natural EOF must stop and wait for the next user command'

Add-Check -Name 'DELETE_INVALIDATES_SELECTION' -Passed (
    $PlaybackText -match 'void\s+rdx_playback_on_file_deleted\s*\(' -and
    $AppText -match 'rdx_playback_on_file_deleted\(\(u32\)p->file_sn\);'
) -Message 'successful deletion must invalidate playback navigation state'

Add-Check -Name 'NO_DYNAMIC_PLAYLIST_ALLOCATION' -Passed (
    $PlaybackText -notmatch '\b(?:malloc|zalloc|calloc)\s*\('
) -Message 'Phase 1 navigation should not allocate a duplicate playlist'

Write-Host ''
Write-Host '-------------------'
if ($Failed -eq 0) {
    Write-Host "All $($Checks.Count) RDX playback navigation checks passed."
    exit 0
}

Write-Host "$Failed of $($Checks.Count) RDX playback navigation checks failed."
exit 1
