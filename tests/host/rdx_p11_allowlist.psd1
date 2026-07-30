@{
    Version = 1
    Entries = @(
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_wifi_service.c'
            Function = 'wifi_tx_done_cb'
            Symbol = 'rdx_uxfile_recordFileData_sendBuf_free'
            Count = 1
            Purpose = 'P12 file-transfer TX completion exception; existing call only'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_storage_service.c'
            Function = 'rdx_cmd_handle_sd_mem_query'
            Symbol = 'rdx_uxfile_device_sd_mem_check'
            Count = 1
            Purpose = 'P11 storage control-plane capacity query'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_storage_service.c'
            Function = 'rdx_cmd_handle_file_delete'
            Symbol = 'rdx_uxfile_recordFile_delete_handle'
            Count = 1
            Purpose = 'P11 storage control-plane file deletion'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_storage_service.c'
            Function = 'rdx_storage_service_runtime_init'
            Symbol = 'rdx_uxfile_init'
            Count = 1
            Purpose = 'P11 storage runtime initialization owner'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_storage_service.c'
            Function = 'rdx_storage_service_format_for_app'
            Symbol = 'rdx_uxfile_sd_format'
            Count = 1
            Purpose = 'P11 APP clean format request with fixed completion'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/compat/rdx_storage_format_compat.c'
            Function = 'rdx_storage_format_compat_for_dut'
            Symbol = 'rdx_uxfile_device_sd_format'
            Count = 1
            Purpose = 'P11 DUT legacy callback format compatibility request'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/compat/rdx_storage_format_compat.c'
            Function = 'rdx_storage_format_compat_for_unbind'
            Symbol = 'rdx_uxfile_sd_format'
            Count = 1
            Purpose = 'P11 unbound legacy callback format compatibility request'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_storage_service.c'
            Function = 'rdx_storage_is_formatting'
            Symbol = 'rdx_uxfile_sd_format_status_check'
            Count = 1
            Purpose = 'P11 narrow formatting-state query'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/compat/rdx_file_transfer_cleanup_compat.c'
            Function = 'rdx_file_transfer_compat_cleanup_ble_delayed'
            Symbol = 'rdx_uxfile_recordFileData_sendBuf_free'
            Count = 1
            Purpose = 'Delayed/full BLE cleanup compatibility profile'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/compat/rdx_file_transfer_cleanup_compat.c'
            Function = 'rdx_file_transfer_compat_cleanup_ble_delayed'
            Symbol = 'rdx_uxfile_datFileInfo_sendBuf_free'
            Count = 1
            Purpose = 'Delayed/full BLE cleanup DAT-list release'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.c'
            Function = 'rdx_record_add_mark'
            Symbol = 'rdx_uxfile_get_operateFile_info'
            Count = 1
            Purpose = 'Record-owner active-file metadata data plane'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.c'
            Function = 'rdx_record_run_init'
            Symbol = 'rdx_uxfile_get_operateFile_info'
            Count = 2
            Purpose = 'Record-owner active-file resume data plane'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.c'
            Function = 'rdx_record_run_init'
            Symbol = 'rdx_uxfile_dat_1_gen'
            Count = 2
            Purpose = 'Record-owner DAT creation data plane'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.c'
            Function = 'rdx_record_run_data_handle'
            Symbol = 'rdx_uxfile_raw_write'
            Count = 2
            Purpose = 'Record-owner real-time raw-write data plane'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.c'
            Function = 'rdx_record_run_exit'
            Symbol = 'rdx_uxfile_raw_write'
            Count = 2
            Purpose = 'Record-owner buffered tail write data plane'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.c'
            Function = 'rdx_record_run_exit'
            Symbol = 'rdx_uxfile_get_operateFile_info'
            Count = 2
            Purpose = 'Record-owner pause/exit metadata data plane'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.c'
            Function = 'rdx_record_run_exit'
            Symbol = 'rdx_uxfile_dat_1_save_gen'
            Count = 2
            Purpose = 'Record-owner DAT finalization data plane'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.c'
            Function = 'rdx_record_run_exit'
            Symbol = 'rdx_uxfile_operate_file_init'
            Count = 2
            Purpose = 'Record-owner active-file reset data plane'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_get_activity'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 private owner adapter reads legacy state for semantic activity query'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_get_activity'
            Symbol = 'rdx_record_get_status'
            Count = 1
            Purpose = 'P11 private owner adapter obtains stable legacy owner object'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_get_scene'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 private owner adapter reads legacy scene for semantic query'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_get_scene'
            Symbol = 'rdx_record_get_status'
            Count = 1
            Purpose = 'P11 private owner adapter obtains stable object for scene query'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_get_path'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 private owner adapter reads legacy mode for semantic path query'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_get_path'
            Symbol = 'rdx_record_get_status'
            Count = 1
            Purpose = 'P11 private owner adapter obtains stable object for path query'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_is_offline_active'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 private owner adapter reads legacy mode and activity state'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_is_offline_active'
            Symbol = 'rdx_record_get_status'
            Count = 1
            Purpose = 'P11 private owner adapter obtains stable object for offline-active query'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_prepare_stop_internal'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 private owner adapter applies the legacy STOP transition'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_prepare_stop_internal'
            Symbol = 'rdx_record_get_status'
            Count = 1
            Purpose = 'P11 private owner adapter obtains the stable object for STOP transition'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_stop_now'
            Symbol = 'rdx_record_process'
            Count = 1
            Purpose = 'P11 private owner adapter executes the synchronous STOP process'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_get_running'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 private owner adapter reads exact legacy running state'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_get_running'
            Symbol = 'rdx_record_get_status'
            Count = 1
            Purpose = 'P11 private owner adapter obtains stable object for running query'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_set_path'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 private owner adapter applies legacy online/offline path fields'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_set_path'
            Symbol = 'rdx_record_get_status'
            Count = 1
            Purpose = 'P11 private owner adapter obtains stable object for path command'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_mark_key_triggered'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 private owner adapter marks an idle key-trigger transaction'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_mark_key_triggered'
            Symbol = 'rdx_record_get_status'
            Count = 1
            Purpose = 'P11 private owner adapter obtains stable object for key-trigger command'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_complete_switch'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 private owner adapter completes switch state and snapshots restart scene'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_complete_switch'
            Symbol = 'rdx_record_get_status'
            Count = 1
            Purpose = 'P11 private owner adapter obtains stable object for switch completion'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_handle_ble_disconnected'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 private owner adapter applies product-specific BLE disconnect state'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_handle_ble_disconnected'
            Symbol = 'rdx_record_get_status'
            Count = 1
            Purpose = 'P11 private owner adapter obtains stable object for BLE disconnect command'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_handle_ble_disconnected'
            Symbol = 'rdx_record_process'
            Count = 1
            Purpose = 'P11 private owner adapter synchronously processes online BLE disconnect stop'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_stop_running_now'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 private owner adapter applies active-only BLE compatibility stop'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_stop_running_now'
            Symbol = 'rdx_record_get_status'
            Count = 1
            Purpose = 'P11 private owner adapter obtains one stable object for active-only stop'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_stop_running_now'
            Symbol = 'rdx_record_process'
            Count = 1
            Purpose = 'P11 private owner adapter synchronously processes active-only stop'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_copy_state'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 private owner adapter copies the narrow legacy decision state'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_get_state'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 private owner adapter reads the narrow decision state'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_get_state'
            Symbol = 'rdx_record_get_status'
            Count = 1
            Purpose = 'P11 private owner adapter obtains the stable object for decision state'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_prepare_upload_fallback'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 private owner adapter prepares active upload fallback STOP payload state'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_prepare_upload_fallback'
            Symbol = 'rdx_record_get_status'
            Count = 1
            Purpose = 'P11 private owner adapter obtains stable state for upload fallback'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_prepare_connected_toggle'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 private owner adapter prepares connected device toggle state'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_prepare_connected_toggle'
            Symbol = 'rdx_record_get_status'
            Count = 1
            Purpose = 'P11 private owner adapter obtains stable state for connected device toggle'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_toggle_post'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 private owner adapter applies offline device toggle state'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_toggle_post'
            Symbol = 'rdx_record_get_status'
            Count = 1
            Purpose = 'P11 private owner adapter obtains stable state for offline device toggle'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_mode_active_check'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 private owner adapter applies idle mode activation fields'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_mode_active_check'
            Symbol = 'rdx_record_get_status'
            Count = 1
            Purpose = 'P11 private owner adapter obtains stable state for mode activation'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_prepare_switch_internal'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 private owner adapter prepares switch fields and payload state'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_prepare_switch_internal'
            Symbol = 'rdx_record_get_status'
            Count = 1
            Purpose = 'P11 private owner adapter obtains stable state for scene switch'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c'
            Function = 'rdx_record_domain_prepare_switch_internal'
            Symbol = 'rdx_record_process'
            Count = 1
            Purpose = 'P11 private owner adapter preserves disconnected synchronous switch stop'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/compat/rdx_record_protocol_adapter.c'
            Function = '<file-scope>'
            Symbol = 'RecordStatus'
            Count = 2
            Purpose = 'P11 protocol adapter owns the four-slot legacy payload pool and callback declaration'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/compat/rdx_record_protocol_adapter.c'
            Function = 'rdx_record_protocol_pool_alloc'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 protocol adapter allocates one legacy payload slot'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/compat/rdx_record_protocol_adapter.c'
            Function = 'rdx_record_protocol_pool_release'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 protocol adapter releases one legacy payload slot'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/compat/rdx_record_protocol_adapter.c'
            Function = 'rdx_record_protocol_pool_callback'
            Symbol = 'RecordStatus'
            Count = 2
            Purpose = 'P11 protocol adapter preserves callback payload type and lifetime'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/compat/rdx_record_protocol_adapter.c'
            Function = 'rdx_record_protocol_reserve'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 protocol adapter reserves and conditionally clears a legacy slot'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/compat/rdx_record_protocol_adapter.c'
            Function = 'rdx_record_protocol_fill_reserved'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 protocol adapter writes kind-specific legacy payload fields'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/compat/rdx_record_protocol_adapter.c'
            Function = 'rdx_record_protocol_post_reserved'
            Symbol = 'RecordStatus'
            Count = 1
            Purpose = 'P11 protocol adapter posts a reserved legacy payload slot'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/compat/rdx_file_transfer_cleanup_compat.c'
            Function = 'rdx_file_transfer_compat_cleanup_record_disconnect'
            Symbol = 'rdx_uxfile_recordFileData_sendBuf_free'
            Count = 1
            Purpose = 'P11 immediate BLE cleanup preserves transfer-buffer-only release'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_storage_domain.c'
            Function = 'rdx_storage_domain_adjust_active_record_time'
            Symbol = 'rdx_uxfile_get_operateFile_info'
            Count = 1
            Purpose = 'P11 storage domain adjusts active-file time after record running gate'
        }
    )
}
