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
            Function = 'rdx_storage_service_format_handle'
            Symbol = 'rdx_uxfile_sd_format'
            Count = 1
            Purpose = 'Legacy APP format compatibility request'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_storage_service.c'
            Function = 'rdx_storage_is_formatting'
            Symbol = 'rdx_uxfile_sd_format_status_check'
            Count = 1
            Purpose = 'P11 narrow formatting-state query'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_storage_service.c'
            Function = 'rdx_storage_service_cleanup_ble_buffers'
            Symbol = 'rdx_uxfile_recordFileData_sendBuf_free'
            Count = 1
            Purpose = 'Delayed/full BLE cleanup compatibility profile'
        }
        @{
            File = 'SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_storage_service.c'
            Function = 'rdx_storage_service_cleanup_ble_buffers'
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
    )
}
