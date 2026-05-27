
#include "opus_stenc_api.h"


FILE* file = NULL;    //opus_points
int fr_cnt = 0, aac_tt = 0;
#define   FRIN_BUF_SIZE   960
int file_end_flag = 0;
int TST_CHNNELS = 0;
int TST_FREAD_BLKL = 1000; // 500;     //165;
int TST_FREAD_BLKR = 1000; //500;     // 35;
short tst_buf[FRIN_BUF_SIZE * 2];
short tst_buf_LR[FRIN_BUF_SIZE * 2];
int in_buf_L = 0;
int in_buf_R = 0;
short* L_ptr, * R_ptr;
u16 mp_input(void* priv, s16* buf, u8 channel, u16 len)
{
	u32 i, rd_len;
	u32 need_len = len;// 30;
	u8* ptr = (u8*)buf;
	static int addr = 0;

	if (need_len > len)
		need_len = len;

	//printf("input    ch:%d\t  len:%d\n",channel,len);
	if (TST_CHNNELS == 1)
	{
		rd_len = fread(file, buf, 2 * need_len);
		rd_len /= 2;
		if (rd_len < need_len)
			file_end_flag = 1;
		addr += rd_len * 2;
		return rd_len;
	}
	else if (TST_CHNNELS == 2)
	{
		if ((in_buf_L == 0) && (in_buf_R == 0))
		{
			rd_len = fread(file, tst_buf, 2 * FRIN_BUF_SIZE * TST_CHNNELS);
			//printf("rd_len :%d\n",rd_len);
			rd_len /= 2;
			//printf("rd_len :%d\n",rd_len);
			//printf_buf(tst_buf,2* FRIN_BUF_SIZE* TST_CHNNELS);
			if (rd_len < 200)
				file_end_flag = 1;

			for (i = 0; i < rd_len / 2; i++)
			{
				tst_buf_LR[i] = tst_buf[2 * i + 0];       //L_pcm.
				tst_buf_LR[i + FRIN_BUF_SIZE] = tst_buf[2 * i + 1];    //R_pcm.
				//printf("%04x\t%04x\n",tst_buf_LR[i]&0xffff,tst_buf_LR[i + FRIN_BUF_SIZE]&0xffff);
			}
			in_buf_L = rd_len / 2;
			in_buf_R = rd_len / 2;
			L_ptr = tst_buf_LR;
			R_ptr = &tst_buf_LR[FRIN_BUF_SIZE];
			//printf_buf(tst_buf_LR,2* FRIN_BUF_SIZE* TST_CHNNELS);
		}
		if (channel == 0)
		{
			if (len > TST_FREAD_BLKL)
				len = TST_FREAD_BLKL;
			if (len > in_buf_L)
				len = in_buf_L;
			memcpy(buf, L_ptr, 2 * len);
			L_ptr += len;
			in_buf_L -= len;
			//printf_buf(buf,len*2);
			return len;
		}
		else if (channel == 1)
		{
			if (len > TST_FREAD_BLKR)
				len = TST_FREAD_BLKR;
			if (len > in_buf_R)
				len = in_buf_R;
			memcpy(buf, R_ptr, 2 * len);
			R_ptr += len;
			in_buf_R -= len;

			//printf_buf(buf,len*2);
			return len;
		}
	}
}

u32 mp_output(void* priv, u8* data, u16 len)
{
	int i, crc = 0;
	u8* ptr = (u8*)data;
	int wlen = len;// 50; // len; // 30;
	if (wlen > len)
		wlen = len;
	static int cnt = 0;
	for (i = 0; i < len; i++)
	{
		crc += *ptr++;
	}
	cnt++;
	//if(cnt%100==0)
	printf("crc%08x\n", crc);

	//printf_buf(data,len);
	//printf("\\\\\\\\\\\\\\\\\++++++mp_output:%08x", len);
	//fwrite(data, 1, wlen, fout);
	return wlen;
}

OPUS_STEN_FILE_IO my_enc_io =
{
	NULL,
	mp_input,
	mp_output,
};

int run_buf[24 * 1024 / 4];
void app_main_opus_stenc_func()
{
	int i, ret;
	u8  check_head[44];
	u32 tst_br = 64000;
	u16 frlen, blklen, rlen;

	sys_clk_set(SYS_96M);
	wdt_close();
#if 1
	{
		int frame_size, needsz;
		void* work_buf;
		OPUS_ENC_PARA  opuset;
		OPUS_STENC_OPS* opus_stenc = get_opus_stenc_ops();
		static int ret = 0;
		int i, crc = 0;
		opuset.sr = 16000;
		opuset.br = 128000; // 32000;
		opuset.nch = 2;     //channels
		opuset.format_mode = 0;     //0:百度   1:兼容源码解码器.    2:ogg.
		opuset.complexity = 4;      //complexity_max_4       //VVV_TST
		opuset.frame_ms = 20;


		TST_CHNNELS = opuset.nch;
		needsz = opus_stenc->need_buf(&opuset);
		printf("needsz  : %d\n", needsz);

		work_buf = (void*)run_buf;
		ret = opus_stenc->open(work_buf, &my_enc_io, &opuset);
		if (ret != 0)
		{
			printf("init failed \n");
		}

		aac_tt = timer_get_ms();

#if 0
		while (1)
		{
			if (fr_cnt % 40 == 0)
			{
				printf("ttt%d\n", timer_get_ms() - aac_tt);
			}
			fr_cnt++;

			ret = opus_stenc->run(work_buf);
			if (ret != 0)
				break;

			if (file_end_flag == 1)
				break;

			clr_wdt();
		}

		printf("----- tst end ----\n");
	}
#else
		if (opuset.format_mode != 2)
		{
			while (1)
			{
				int i, rd_len, encLen;
				unsigned char obuf[640 + 8];
				int frame_size = opuset.sr * opuset.frame_ms / 1000;

				if (fr_cnt % 40 == 0)
				{
					printf("ttt%d\n", timer_get_ms() - aac_tt);
				}
				fr_cnt++;

				memset(tst_buf, 0, sizeof(tst_buf));
				rd_len = fread(file, tst_buf, 2 * frame_size * TST_CHNNELS);
				if (rd_len < 2 * frame_size * TST_CHNNELS)
					break;
				rd_len /= 2;


				if (TST_CHNNELS == 2)
				{
					for (i = 0; i < rd_len / 2; i++)
					{
						tst_buf_LR[i] = tst_buf[2 * i + 0];       //L_pcm.
						tst_buf_LR[i + FRIN_BUF_SIZE] = tst_buf[2 * i + 1];    //R_pcm.
					}
					ret = opus_stenc->run_wb(work_buf, tst_buf_LR, &tst_buf_LR[FRIN_BUF_SIZE], obuf, &encLen);
				}
				else
				{
					ret = opus_stenc->run_wb(work_buf, tst_buf, &tst_buf_LR[FRIN_BUF_SIZE], obuf, &encLen);
				}
				if (ret != 0)
					break;
				{
					int crc = 0;
					for (i = 0; i < encLen; i++)
					{
						crc += obuf[i];
					}
					printf("crc%08x\n", crc);
				}
			}
		}
	}
#endif
#endif
	wdt_clear();
	while (1);
}



