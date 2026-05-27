
#ifndef _OPUS_STENCODE_API_H_
#define _OPUS_STENCODE_API_H_

#ifndef u16
#define  u32  unsigned int
#define  u16  unsigned short
#define  s16  short
#define  u8  unsigned char
#endif


typedef struct _OPUS_STEN_FILE_IO_
{
	void *priv;
	u16 (*input_data)(void *priv,s16 *buf,u8 channel,u16 len);   //short
	u32 (*output_data)(void *priv,u8 *buf,u16 len);  //bytes
}OPUS_STEN_FILE_IO; // 在run_wb 模式是不会调用的


typedef struct _OPUS_ENC_PARA_    
{
	int sr;   //samplerate: 8000/16000/24000/48000.
	int br;   //bitrate:
	u16 nch;  //channels  1:2. 
	u16 format_mode;   //封装格式:   0:百度无头.      1:size+rangeFinal. 源码可兼容版本.    2:ogg封装,pc软件可播放.
	u16 complexity;    //0 1 2 3 4   fix_4.
	u16 frame_ms;      //10ms:20ms. 
}OPUS_ENC_PARA;


typedef struct __OPUS_STENC_OPS {
	u32 (*need_buf)(OPUS_ENC_PARA* para);      //samplerate=16k   ignore
	u32 (*open)(u8 *ptr, OPUS_STEN_FILE_IO*audioIO, OPUS_ENC_PARA *para);
	u32 (*run)(u8 *ptr);
	u32 (*run_wb)(u8* ptr,short *pcmL,short*pcmR,u8 *obuf,int *encLen );     //run_with_buffer  仅限封装模式0 or 1.
}OPUS_STENC_OPS;


extern OPUS_STENC_OPS* get_opus_stenc_ops(void);


#define OPUS_CR (16)//opus压缩率
#define OPUS_SR (16000)//opus采样率Hz
#define OPUS_FS	(20)//opus每帧间隔时间ms
#define FRAME_POINT (OPUS_SR/(1000/OPUS_FS))
#define FRAME_SIZE  (FRAME_POINT * sizeof(s16))

#define MIC (1)
#define DAC (2)

typedef enum {
    MIC_TO_MONO_OPUS = (1 << 4) | MIC,//mic输出到单声道opus编码器（单声道）
    DAC_TO_MONO_OPUS = (1 << 4) | DAC,//dac输出到单声道opus编码器（单声道）
    MIC_TO_STERO_OPUS = (2 << 4) | MIC,//mic输出到立体声opus编码器（单声道）
    DAC_TO_STERO_OPUS = (2 << 4) | DAC,//dac输出到立体声opus编码器（单声道）
    MIC_DAC_TO_STERO_OPUS = (2 << 4) | MIC | DAC,//mic和dac输出到立体声opus编码器（立体声）
} stream_type;

//#pragma bss_seg(".opus_stenc.data.bss")
//#pragma data_seg(".opus_stenc.data")
//#pragma const_seg(".opus_stenc.text.cache.L2.const")
//#pragma code_seg(".opus_stenc.text.cache.L2.run")
//
//#define YAT(x)           __attribute__((section(#x)))
//#define AT_SPARSE_CODE    YAT(.opus_stenc.text)
//#define AT_SPARSE_CONST   YAT(.opus_stenc.text.const) 




#endif


