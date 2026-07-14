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

Add-Check -Name 'PLAY_PAUSE_API_SURFACE' -Passed (
    $PlaybackHeaderText -match '\bPB_STATE_PAUSED\b' -and
    $PlaybackHeaderText -match '\bu32\s+resume_sn\s*;' -and
    $PlaybackHeaderText -match '\bu32\s+resume_frame\s*;' -and
    $PlaybackHeaderText -match '\bint\s+rdx_playback_play\s*\(' -and
    $PlaybackHeaderText -match '\bint\s+rdx_playback_pause\s*\(' -and
    $PlaybackHeaderText -notmatch '\brdx_playback_resume\s*\('
) -Message 'Phase 5 requires PAUSED state, an SN-bound resume cursor, and separate play/pause commands'

Add-Check -Name 'PAUSE_RELEASES_ACTIVE_TRACK' -Passed (
    $PlaybackText -match '(?s)int\s+rdx_playback_pause\s*\([^)]*\).*?pb\.resume_sn\s*=\s*pb\.selected_sn;.*?pb\.resume_frame\s*=\s*resume_frame;.*?pb_close_track\(\);.*?pb\.state\s*=\s*PB_STATE_PAUSED;' -and
    $PlaybackText -match '(?s)void\s+rdx_playback_get_info\s*\([^)]*\).*?PB_STATE_PAUSED.*?pb\.resume_frame'
) -Message 'pause must retain its SN/frame while releasing active resources and exposing the paused position'

Add-Check -Name 'PAUSE_REWIND_USES_FRAME_DURATION' -Passed (
    $PlaybackText -match '#define\s+PB_RESUME_REWIND_MS\s+\(100u\)' -and
    $PlaybackText -match '#define\s+PB_RESUME_REWIND_FRAMES\s+\(PB_RESUME_REWIND_MS\s*/\s*PB_OPUS_FRAME_MS\)' -and
    $PlaybackText -notmatch '\bPB_PAUSE_REWIND_(?:MS|FRAMES)\b'
) -Message 'pause resume rewind must derive frames from the Opus frame duration'

Add-Check -Name 'SWITCH_CLEARS_RESUME_AFTER_COMMIT' -Passed (
    $PlaybackText -match '(?s)pb\.selected_sn\s*=\s*candidate_sn;.*?pb\.current_sn\s*=\s*candidate_sn;.*?pb_clear_resume_cursor\(\);'
) -Message 'successful navigation must clear the old pause cursor only after committing the new track'

Add-Check -Name 'FAILED_SWITCH_RESTORES_PAUSE' -Passed (
    $PlaybackText -match '(?s)static\s+void\s+pb_restore_stable_state\s*\([^)]*\).*?previous_state\s*==\s*PB_STATE_PAUSED.*?pb\.resume_sn\s*==\s*pb\.selected_sn.*?pb\.state\s*=\s*PB_STATE_PAUSED;' -and
    $PlaybackText -match '(?s)if\s*\(result\s*==\s*PB_CANDIDATE_FATAL\).*?pb_restore_stable_state\(previous_state\);'
) -Message 'failed navigation from PAUSED must preserve and restore the original pause session'

Add-Check -Name 'SYNC_DOES_NOT_DESTROY_PAUSE' -Passed (
    $PlaybackText -match '!pb_has_active_track\(\)\s*&&\s*pb\.state\s*!=\s*PB_STATE_PAUSED'
) -Message 'temporary DAT sync must reject playback commands without destroying a paused cursor'

Add-Check -Name 'PLAY_INTENT_IS_NOT_SWITCH' -Passed (
    $PlaybackHeaderText -match '\bPB_INTENT_PLAY\b' -and
    $PlaybackText -match '(?s)rdx_playback_play\s*\([^)]*\).*?pb_start_candidate_at_frame\s*\(.*?PB_INTENT_PLAY\s*\);'
) -Message 'play and pause-resume must not be classified as navigation switch transactions'

Add-Check -Name 'ONE_RING_PROBE_BOUND' -Passed (
    $PlaybackText -match 'while\s*\(probed_slots\s*<\s*pb_max_sn\s*&&\s*candidate_count\s*<\s*pb\.total_count\)'
) -Message 'navigation must be bounded by one SN ring and the DAT count'

Add-Check -Name 'UNIFIED_DIRECTION_SWITCH' -Passed (
    $PlaybackText -match 'rdx_playback_prev\s*\([^)]*\)\s*\{\s*return\s+pb_switch_track\(PB_DIRECTION_NEWER\);' -and
    $PlaybackText -match 'rdx_playback_next\s*\([^)]*\)\s*\{\s*return\s+pb_switch_track\(PB_DIRECTION_OLDER\);'
) -Message 'prev and next must share the same switch transaction'

$CandidateStart = $PlaybackText.IndexOf('static pb_candidate_result_t pb_start_candidate_at_frame')
$StreamHelperStart = $PlaybackText.IndexOf('static int pb_open_stream_at_frame')
$OpenStream = if ($StreamHelperStart -ge 0) { $PlaybackText.IndexOf('dev_flow_player_open(', $StreamHelperStart) } else { -1 }
$SchedulePump = if ($OpenStream -ge 0) { $PlaybackText.IndexOf('pb_schedule_pump(PB_PUMP_INTERVAL_MS)', $OpenStream) } else { -1 }
$StartCandidateStream = if ($CandidateStart -ge 0) { $PlaybackText.IndexOf('pb_open_stream_at_frame(base_frame, transition_state)', $CandidateStart) } else { -1 }
$CommitSelected = if ($StartCandidateStream -ge 0) { $PlaybackText.IndexOf('pb.selected_sn = candidate_sn', $StartCandidateStream) } else { -1 }
$CommitCurrent = if ($StartCandidateStream -ge 0) { $PlaybackText.IndexOf('pb.current_sn = candidate_sn', $StartCandidateStream) } else { -1 }
Add-Check -Name 'COMMIT_AFTER_STARTUP' -Passed (
    $CandidateStart -ge 0 -and $StreamHelperStart -ge 0 -and
    $OpenStream -gt $StreamHelperStart -and $SchedulePump -gt $OpenStream -and
    $StartCandidateStream -gt $CandidateStart -and
    $CommitSelected -gt $StartCandidateStream -and
    $CommitCurrent -gt $StartCandidateStream
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

$SeekStart = $PlaybackText.IndexOf('static int pb_seek_relative')
$FfStart = $PlaybackText.IndexOf('void rdx_playback_ff', $SeekStart)
$SeekBody = if ($SeekStart -ge 0 -and $FfStart -gt $SeekStart) {
    $PlaybackText.Substring($SeekStart, $FfStart - $SeekStart)
} else {
    ''
}

Add-Check -Name 'SEEK_SOURCE_CONSUMED_POSITION' -Passed (
    $PlaybackText -match 'source_dev0_get_consumed_bytes\(\)\s*/\s*PB_OPUS_FRAME_BYTES' -and
    $PlaybackHeaderText -match '\bu32\s+seek_base_frame\s*;' -and
    $PlaybackHeaderText -match '\bu32\s+duration_frames\s*;'
) -Message 'seek position must use Source_Dev0 consumed bytes plus seek_base_frame'

Add-Check -Name 'SEEK_SINGLE_FILE_ONLY' -Passed (
    $SeekBody -match 'fseek\s*\(\s*pb_file\s*,\s*target_offset\s*,\s*SEEK_SET\s*\)' -and
    $SeekBody -notmatch 'rdx_playback_(?:next|prev)\s*\(' -and
    $SeekBody -notmatch 'pb_switch_track\s*\(' -and
    $SeekBody -match 'pb_finish_stop\(false\);'
) -Message 'ff/fr must seek within the current file and stop at end without navigating to another file'

Add-Check -Name 'FF_FR_SEEK_STEP_IMPLEMENTED' -Passed (
    $PlaybackText -match '#define\s+PB_SEEK_STEP_MS\s+\(5000u\)' -and
    $PlaybackText -match 'void\s+rdx_playback_ff\s*\([^)]*\)\s*\{\s*int\s+ret\s*=\s*pb_seek_relative\(\(s32\)PB_SEEK_STEP_FRAMES\);' -and
    $PlaybackText -match 'void\s+rdx_playback_fr\s*\([^)]*\)\s*\{\s*int\s+ret\s*=\s*pb_seek_relative\(-\(\(s32\)PB_SEEK_STEP_FRAMES\)\);' -and
    $PlaybackText -notmatch 'ff:\s+not implemented' -and
    $PlaybackText -notmatch 'fr:\s+not implemented'
) -Message 'ff/fr should implement +/-5s relative seek instead of logging a stub'

Write-Host ''
Write-Host '-------------------'
if ($Failed -eq 0) {
    Write-Host "All $($Checks.Count) RDX playback navigation checks passed."
    exit 0
}

Write-Host "$Failed of $($Checks.Count) RDX playback navigation checks failed."
exit 1
