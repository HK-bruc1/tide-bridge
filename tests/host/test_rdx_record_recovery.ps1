$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$recordPath = Join-Path $repo 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.c'
$recordHeaderPath = Join-Path $repo 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.h'
$adcPath = Join-Path $repo 'SDK/audio/framework/plugs/source/adc_file.c'
$sinkPath = Join-Path $repo 'SDK/audio/framework/nodes/sink_dev1_node.c'
$storagePath = Join-Path $repo 'SDK/apps/common/third_party_profile/rdx_protocol/port/jl/jl7018/rdx_jl_storage.c'
$shadowStoragePath = Join-Path $repo 'SDK/apps/common/third_party_profile/rdx_protocol/port/jl/jl7018_shadow/rdx_jl_storage.c'
$storageHeaderPath = Join-Path $repo 'SDK/apps/common/third_party_profile/rdx_protocol/port/jl/include/rdx_jl_storage.h'

$record = Get-Content -Raw -LiteralPath $recordPath
$recordHeader = Get-Content -Raw -LiteralPath $recordHeaderPath
$adc = Get-Content -Raw -LiteralPath $adcPath
$sink = Get-Content -Raw -LiteralPath $sinkPath
$storage = Get-Content -Raw -LiteralPath $storagePath
$shadowStorage = Get-Content -Raw -LiteralPath $shadowStoragePath
$storageHeader = Get-Content -Raw -LiteralPath $storageHeaderPath

function Assert-Match([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) {
        throw "FAIL: $Message"
    }
    Write-Host "PASS: $Message"
}

function Assert-NotMatch([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -match $Pattern) {
        throw "FAIL: $Message"
    }
    Write-Host "PASS: $Message"
}

# MIC gain persistence is successful only after exact-length write + readback.
Assert-Match $record 'rdx_storage_write\(RDX_STORAGE_KEY_MIC_GAIN' 'MIC gain persistence stays behind the JL storage port'
Assert-Match $record 'rdx_storage_write\(RDX_STORAGE_KEY_REC_ERR_REBOOT' 'Record recovery flag stays behind the JL storage port'
Assert-NotMatch $record '\bsyscfg_(?:read|write)\s*\(' 'Record business code has no direct syscfg access'
Assert-Match $storage 'return\s*\(ret\s*==\s*len\)\s*\?\s*RDX_OK\s*:\s*RDX_ERR_IO;' 'JL storage port requires exact-length transfer'
Assert-Match $shadowStorage 'return\s*\(ret\s*==\s*len\)\s*\?\s*RDX_OK\s*:\s*RDX_ERR_IO;' 'Shadow storage port matches exact-length semantics'
Assert-Match $storageHeader 'RDX_OK is returned only when[\s\S]*?exactly len bytes' 'Storage public contract documents exact-length semantics'
Assert-Match $record 'read_ret\s*!=\s*RDX_OK\s*\|\|\s*memcmp\(&verify,\s*gain,\s*sizeof\(verify\)\)\s*!=\s*0' 'MIC gain VM write is read back and byte-verified'
Assert-Match $record 'rdx_record_mic_gain_read_from_vm fail[\s\S]*?return NULL;' 'MIC gain VM read exposes failure instead of returning stale data'

# VM failure must not disable recording: safe defaults remain applicable at runtime.
Assert-Match $record 'use runtime defaults; VM repair failed[\s\S]*?p\s*=\s*&defaults;' 'Recording falls back to safe MIC gains when VM repair fails'
Assert-Match $record '!gain->chat_mic_flag[\s\S]*?chat_mic0_gain\s*==\s*0\s*\|\|\s*gain->chat_mic0_gain\s*==\s*0xff' 'Legacy chat zero/0xFF sentinel is gated by the unset flag'
Assert-Match $record '!gain->call_mic_flag[\s\S]*?call_mic0_gain\s*==\s*0\s*\|\|\s*gain->call_mic0_gain\s*==\s*0xff' 'Legacy call zero/0xFF sentinel is gated by the unset flag'
Assert-Match $record 'value\s*<=\s*RECORD_MIC_DB_VALUE_MAX\s*\|\|[\s\S]*?!configured\s*&&\s*value\s*==\s*0xff' 'Configured zero remains a legal gain value'
Assert-Match $record 'use_adc_value\s*&&[\s\S]*?rdx_audio_adc_file_get_gain_checked' 'Active-mode legacy migration preserves the current ADC default gain'
Assert-Match $record 'if\(chat_changed\)\s*\{\s*gain->chat_mic_flag\s*=\s*true;' 'Legacy chat migration initializes its flag in the same VM image'
Assert-Match $record 'if\(call_changed\)\s*\{\s*gain->call_mic_flag\s*=\s*true;' 'Legacy call migration initializes its flag in the same VM image'
Assert-Match $recordHeader 'void\s+rdx_record_mic_gain_check\(void\);' 'Historical MIC gain entry keeps its void ABI'

# Hardware application happens at the ADC/sink lifecycle boundary and is verified.
Assert-Match $sink 'rdx_record_mic_gain_check\(\);' 'Recording sink applies MIC gain at the ADC lifecycle boundary'
Assert-Match $adc 'void\s+rdx_audio_adc_file_set_gain\(u8 mic_index,\s*u8 mic_gain\)' 'Historical ADC MIC setter keeps its void ABI'
Assert-Match $adc 'u8\s+rdx_audio_adc_file_get_gain\(u8 mic_index\)' 'Historical ADC MIC getter keeps its u8 ABI'
Assert-Match $adc 'int\s+rdx_audio_adc_file_set_gain_checked\(u8 mic_index,\s*u8 mic_gain\)' 'Checked ADC MIC setter is additive'
Assert-Match $adc 'mic_index\s*>=\s*AUDIO_ADC_MAX_NUM' 'ADC MIC access rejects the upper out-of-range index'
Assert-Match $adc '!hdl_p\s*\|\|\s*!hdl_p->adc_f[\s\S]*?mic_en_map\s*&\s*BIT\(mic_index\)' 'ADC MIC setter rejects unavailable or disabled channels'

# BUSY commands are retained and replayed on app_core; latest command wins.
Assert-Match $record 'g_pending_busy_record_valid\s*=\s*true' 'BUSY recording command is retained'
Assert-Match $record 'memcpy\(&g_pending_busy_record_info,\s*r_info,\s*sizeof\(Record_info\)\)' 'Latest BUSY command replaces the previous pending command'
Assert-Match $record 'rdx_os_task_post_callback0\("app_core",\s*rdx_record_pending_busy_replay_cb\)' 'Pending recording command is replayed on app_core'
Assert-Match $record 'g_pending_busy_replay_timer\s*=\s*rdx_os_timer_add\([\s\S]*?rdx_record_pending_busy_replay_retry_cb' 'Replay queue failure uses an independent retry timer'
Assert-Match $record 'replay deferred; pending cmd retained' 'Double scheduling failure retains the pending command'
Assert-Match $record 'rdx_record_process_state_set_timer_cb[\s\S]*?rdx_record_set_process_state_ready\(\);' 'BUSY timeout transitions to READY and triggers replay scheduling'
Assert-Match $record 'g_pending_busy_replay_posted[\s\S]*?else if\(record_status.process_state\s*==\s*REC_PROCESS_STATE_BUSY\)' 'Pending replacement and BUSY admission share one decision chain'
Assert-Match $record 'if\(g_pending_busy_replay_timer\)\s*\{\s*rdx_os_timer_del\(g_pending_busy_replay_timer\);\s*g_pending_busy_replay_timer\s*=\s*0;' 'Record reset cancels the replay retry timer'
Assert-Match $record 'if\(record_set_process_state_timer\)\s*\{\s*rdx_os_timer_del\(record_set_process_state_timer\);\s*record_set_process_state_timer\s*=\s*0;' 'Record reset cancels the BUSY recovery timer'
Assert-Match $record 'if\(g_record_cmd_delay_timer\)\s*\{\s*rdx_os_timer_del\(g_record_cmd_delay_timer\);\s*g_record_cmd_delay_timer\s*=\s*0;' 'Record reset cancels the delayed START timer'

$admission = [regex]::Match($record, 'y_printf\("------ %s, r_info->cmd[\s\S]*?if\(r_info->cmd\s*==\s*\(RECORD_STATE_START').Value
if(([regex]::Matches($admission, 'CPU_CRITICAL_ENTER\(\);')).Count -ne 1){
    throw 'FAIL: command admission must use exactly one critical section'
}
Write-Host 'PASS: command admission uses exactly one critical section'

# Guard against restoring the old recursive READY/timer-stop pair.
$timerStop = [regex]::Match($record, 'void\s+rdx_record_process_state_timer_stop\(void\)[\s\S]*?\n\}').Value
Assert-NotMatch $timerStop 'rdx_record_set_process_state_ready\(' 'Timer stop no longer recursively calls READY'
$replaySchedule = [regex]::Match($record, 'static\s+void\s+rdx_record_pending_busy_replay_schedule\(void\)\s*\{[\s\S]*?\n\}').Value
Assert-NotMatch $replaySchedule 'rdx_record_pending_busy_replay_cb\(\);' 'Scheduling failure never runs record replay synchronously'

Write-Host 'All RDX record recovery contract checks passed.'
