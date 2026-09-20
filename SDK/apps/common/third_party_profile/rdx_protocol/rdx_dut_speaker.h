#ifndef RDX_DUT_SPEAKER_H
#define RDX_DUT_SPEAKER_H

enum { DUT_AUDIO_IDLE, DUT_AUDIO_STARTING, DUT_AUDIO_PLAYING, DUT_AUDIO_DONE };
int rdx_dut_speaker_open(void);
void rdx_dut_speaker_cancel(void);
int rdx_dut_speaker_state(void);
void rdx_dut_speaker_reap(void);

#endif
