#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".dev_manager.data.bss")
#pragma data_seg(".dev_manager.data")
#pragma const_seg(".dev_manager.text.const")
#pragma code_seg(".dev_manager.text")
#endif
#include "system/includes.h"
#include "system/generic/jiffies.h"
#include "dev_manager.h"
#include "app_config.h"
#include "app_main.h"
#ifndef TCFG_SD0_DIAG_ENABLE
#define TCFG_SD0_DIAG_ENABLE 0
#endif
#if TCFG_USB_DM_MULTIPLEX_WITH_SD_DAT0
#include "dev_multiplex_api.h"
#endif
#include "fat_nor/nor_fs.h"
#include "dev_update.h"
#include "syscfg_id.h"

#ifndef TCFG_SD0_FORMAT_DONE_MAGIC
#define TCFG_SD0_FORMAT_DONE_MAGIC   0xA5
#endif

#ifndef TCFG_SD0_AUTO_FORMAT_ON_MOUNT_FAIL_ENABLE
#define TCFG_SD0_AUTO_FORMAT_ON_MOUNT_FAIL_ENABLE TCFG_SD0_FORMAT_ON_BOOT
#endif

#if TCFG_SD0_DIAG_ENABLE
#include "device/device.h"
#endif

// *INDENT-OFF*

#if (TCFG_DEV_MANAGER_ENABLE)

#define DEV_MANAGER_TASK_NAME							"dev_mg"

///设备管理总控制句柄
struct __dev_manager {
	struct list_head 	list;
	OS_MUTEX			mutex;
	OS_SEM			    sem;
	volatile u32 				counter;
};
static struct __dev_manager dev_mg;
#define __this	(&dev_mg)

/* SD0 bring-up 诊断代码只在宏打开时编译，宏关闭时不改变正常 mount 路径。
 *
 * 设计目标：在无法修改闭源 SD 主机驱动库的前提下，把“从检测到挂载”的链路
 * 拆成若干阶段，通过阶段到达情况给出失败层级提示，方便 bring-up 时快速定位
 * 是电气/检测、SD 协议初始化、数据通路、分区表、PBR 还是文件系统层的问题。
 */
#if (TCFG_SD0_ENABLE && TCFG_SD0_DIAG_ENABLE)

#define SD0_DIAG_MBR_OFF_PART0      0x1be

enum sd0_diag_stage {
	SD0_STAGE_UNKNOWN = 0,
	SD0_STAGE_ONLINE,
	SD0_STAGE_OPEN,
	SD0_STAGE_IDENTIFY,
	SD0_STAGE_GEOMETRY,
	SD0_STAGE_LBA0,
	SD0_STAGE_PARTITION,
	SD0_STAGE_PBR,
	SD0_STAGE_MOUNT,
	SD0_STAGE_FS_OK,
};

static const char *sd0_diag_stage_name(enum sd0_diag_stage s)
{
	switch (s) {
	case SD0_STAGE_ONLINE:     return "ONLINE";
	case SD0_STAGE_OPEN:       return "OPEN";
	case SD0_STAGE_IDENTIFY:   return "IDENTIFY";
	case SD0_STAGE_GEOMETRY:   return "GEOMETRY";
	case SD0_STAGE_LBA0:       return "LBA0";
	case SD0_STAGE_PARTITION:  return "PARTITION";
	case SD0_STAGE_PBR:        return "PBR";
	case SD0_STAGE_MOUNT:      return "MOUNT";
	case SD0_STAGE_FS_OK:      return "FS_OK";
	default:                   return "UNKNOWN";
	}
}

static u8 sd0_diag_before_open_ok;
static u8 sd0_diag_ever_open_ok;
static u8 sd0_diag_last_stage;
static u32 sd0_diag_attempt;

static u16 sd0_diag_get_le16(const u8 *buf)
{
	return (u16)buf[0] | ((u16)buf[1] << 8);
}

static u32 sd0_diag_get_le32(const u8 *buf)
{
	return (u32)buf[0] | ((u32)buf[1] << 8) | ((u32)buf[2] << 16) | ((u32)buf[3] << 24);
}

static void sd0_diag_dump_hex(const u8 *buf, u32 len)
{
	u32 i;

	for (i = 0; i < len; i++) {
		if ((i & 0x0f) == 0) {
			printf("[SD-DIAG] %04x:", i);
		}
		printf(" %02x", buf[i]);
		if ((i & 0x0f) == 0x0f) {
			printf("\n");
		}
	}
	if (len & 0x0f) {
		printf("\n");
	}
}

static void sd0_diag_dump_bpb(const char *tag, const u8 *buf)
{
	printf("[SD-DIAG] %s sig=%02x%02x jmp=%02x %02x %02x oem=%c%c%c%c%c%c%c%c\n",
	       tag, buf[511], buf[510], buf[0], buf[1], buf[2],
	       buf[3], buf[4], buf[5], buf[6], buf[7], buf[8], buf[9], buf[10]);
	printf("[SD-DIAG] %s bps=%u spc=%u rsvd=%u fats=%u root_ent=%u tot16=%u tot32=%u fatsz16=%u\n",
	       tag,
	       sd0_diag_get_le16(&buf[11]),
	       buf[13],
	       sd0_diag_get_le16(&buf[14]),
	       buf[16],
	       sd0_diag_get_le16(&buf[17]),
	       sd0_diag_get_le16(&buf[19]),
	       sd0_diag_get_le32(&buf[32]),
	       sd0_diag_get_le16(&buf[22]));
}

static int sd0_diag_read_lba(void *fd, u8 *buf, u32 lba, const char *tag)
{
	int ret;

	memset(buf, 0, 512);
	ret = dev_bulk_read(fd, buf, lba, 1);
	printf("[SD-DIAG] read %s lba=%u ret=%d tail=%02x %02x\n",
	       tag, lba, ret, buf[510], buf[511]);
	if (ret == 1) {
		sd0_diag_dump_hex(buf, 64);
		if ((buf[510] == 0x55) && (buf[511] == 0xaa)) {
			sd0_diag_dump_bpb(tag, buf);
		}
	}
	return ret;
}

static void sd0_diag_parse_mbr(const u8 *buf)
{
	int i;
	u32 start, sectors;
	u8 type, active;
	u8 valid_parts = 0;

	for (i = 0; i < 4; i++) {
		const u8 *p = &buf[SD0_DIAG_MBR_OFF_PART0 + i * 16];
		active = p[0];
		type = p[4];
		start = sd0_diag_get_le32(&p[8]);
		sectors = sd0_diag_get_le32(&p[12]);

		if (type == 0x0b || type == 0x0c) {
			printf("[SD-DIAG] mbr part%d active=0x%02x type=0x%02x (FAT32) start_lba=%u sectors=%u\n",
			       i, active, type, start, sectors);
		} else if (type == 0x01 || type == 0x04 || type == 0x06 || type == 0x0e) {
			printf("[SD-DIAG] mbr part%d active=0x%02x type=0x%02x (FAT12/16) start_lba=%u sectors=%u\n",
			       i, active, type, start, sectors);
		} else if (type == 0x05 || type == 0x0f) {
			printf("[SD-DIAG] mbr part%d active=0x%02x type=0x%02x (extended, not directly mountable) start_lba=%u sectors=%u\n",
			       i, active, type, start, sectors);
		} else if (type == 0x83) {
			printf("[SD-DIAG] mbr part%d active=0x%02x type=0x%02x (Linux) start_lba=%u sectors=%u\n",
			       i, active, type, start, sectors);
		} else if (type) {
			printf("[SD-DIAG] mbr part%d active=0x%02x type=0x%02x (unknown/non-FAT) start_lba=%u sectors=%u\n",
			       i, active, type, start, sectors);
		} else {
			printf("[SD-DIAG] mbr part%d active=0x%02x type=0x%02x (empty) start_lba=%u sectors=%u\n",
			       i, active, type, start, sectors);
		}

		if ((type == 0x01 || type == 0x04 || type == 0x06 ||
		     type == 0x0b || type == 0x0c || type == 0x0e) &&
		    start && sectors) {
			valid_parts++;
		}
	}
	if (!valid_parts) {
		printf("[SD-DIAG] WARN: no valid FAT partition entry found\n");
	}
}

static void sd0_diag_parse_pbr(const u8 *buf, u32 lba)
{
	u16 bps = sd0_diag_get_le16(&buf[11]);
	u8 spc = buf[13];
	u16 rsvd = sd0_diag_get_le16(&buf[14]);
	u8 fats = buf[16];
	u16 root_ent = sd0_diag_get_le16(&buf[17]);
	u16 tot16 = sd0_diag_get_le16(&buf[19]);
	u32 tot32 = sd0_diag_get_le32(&buf[32]);
	u16 fatsz16 = sd0_diag_get_le16(&buf[22]);
	u32 fatsz32 = sd0_diag_get_le32(&buf[36]);
	u32 root_cluster = sd0_diag_get_le32(&buf[44]);
	u16 fs_ver = sd0_diag_get_le16(&buf[42]);
	u8 media = buf[21];

	printf("[SD-DIAG] pbr lba=%u sig=%02x%02x oem=%.8s bps=%u spc=%u media=0x%02x\n",
	       lba, buf[510], buf[511], &buf[3], bps, spc, media);
	printf("[SD-DIAG] pbr rsvd=%u fats=%u root_ent=%u tot16=%u tot32=%u\n",
	       rsvd, fats, root_ent, tot16, tot32);
	printf("[SD-DIAG] pbr fatsz16=%u fatsz32=%u fs_ver=%u root_clus=%u\n",
	       fatsz16, fatsz32, fs_ver, root_cluster);

	if ((buf[510] == 0x55) && (buf[511] == 0xaa)) {
		printf("[SD-DIAG] pbr boot-signature OK\n");
	} else {
		printf("[SD-DIAG] WARN: pbr missing boot signature\n");
	}

	/* 简单推断 FAT 类型 */
	if (fatsz32 && root_cluster && !root_ent) {
		printf("[SD-DIAG] pbr looks like FAT32\n");
	} else if (fatsz16 && root_ent && !fatsz32) {
		printf("[SD-DIAG] pbr looks like FAT12/16\n");
	} else {
		printf("[SD-DIAG] WARN: pbr cannot determine FAT variant\n");
	}
}

static void sd0_diag_print_summary(const char *phase, enum sd0_diag_stage reached)
{
	sd0_diag_last_stage = (u8)reached;
	printf("[SD-DIAG] summary phase=%s attempt=%u reached=%s before_open_ok=%u ever_open_ok=%u\n",
	       phase, sd0_diag_attempt, sd0_diag_stage_name(reached),
	       sd0_diag_before_open_ok, sd0_diag_ever_open_ok);
	printf("[SD-DIAG] failure-layer-hint: ");
	switch (reached) {
	case SD0_STAGE_UNKNOWN:
		printf("no progress (check power/reset/clock pinmux before CMD0)\n");
		break;
	case SD0_STAGE_ONLINE:
		printf("card detect reports offline (check detect_mode/detect_io/power-on timing/VDD)\n");
		break;
	case SD0_STAGE_OPEN:
		printf("SD protocol init failed (CMD0/ACMD41/CID/RCA/CSD, check electrical/timing/bus-width)\n");
		break;
	case SD0_STAGE_IDENTIFY:
	case SD0_STAGE_GEOMETRY:
		printf("card opened but geometry ioctls failed (driver/card identification error)\n");
		break;
	case SD0_STAGE_LBA0:
		printf("data path broken (cannot read LBA0, check bus width/CRC/clock/data line pull-ups)\n");
		break;
	case SD0_STAGE_PARTITION:
		printf("LBA0 OK but partition table invalid (MBR corrupt or non-partitioned media)\n");
		break;
	case SD0_STAGE_PBR:
		printf("partition found but PBR/boot-sector unreadable/corrupt\n");
		break;
	case SD0_STAGE_MOUNT:
		printf("PBR OK but filesystem mount failed (FS corrupt/unsupported or memory exhausted)\n");
		break;
	case SD0_STAGE_FS_OK:
		printf("all stages passed\n");
		break;
	default:
		printf("unknown stage\n");
		break;
	}
}

static void sd0_diag_probe(const char *phase)
{
	void *fd = NULL;
	u8 *buf = NULL;
	int ret;
	bool online;
	u32 t0_ms, t1_ms;
	u32 probe_t0_ms;
	u32 status = 0;
	u32 capacity = 0;
	u32 block_size = 0;
	u32 sector_size = 0;
	u32 block_number = 0;
	u32 dev_type = 0;
	u32 dev_id = 0;
	u32 part_lba = 0;
	enum sd0_diag_stage stage = SD0_STAGE_UNKNOWN;

	if (!strcmp(phase, "before_mount")) {
		sd0_diag_attempt++;
		sd0_diag_before_open_ok = 0;
	}

	probe_t0_ms = jiffies_msec();
	printf("\n[SD-DIAG] ===== %s begin (attempt=%u) =====\n", phase, sd0_diag_attempt);

	/* 1. 检测层：卡是否被识别为 online */
	online = dev_online("sd0");
	printf("[SD-DIAG] dev_online(sd0)=%d\n", (int)online);
	stage = SD0_STAGE_ONLINE;
	if (!online) {
		printf("[SD-DIAG] hint: card detect reports offline, skip further probe\n");
		sd0_diag_print_summary(phase, stage);
		printf("[SD-DIAG] ===== %s end =====\n\n", phase);
		return;
	}

	/* 2. SD 协议初始化层：raw open 是闭源驱动初始化完成的闸门 */
	t0_ms = jiffies_msec();
	fd = dev_open("sd0", NULL);
	t1_ms = jiffies_msec();
	printf("[SD-DIAG] dev_open(sd0)=%x cost=%ums\n", (int)fd, t1_ms - t0_ms);
	if (!fd) {
		printf("[SD-DIAG] FAIL: raw open failed before filesystem\n");
		sd0_diag_print_summary(phase, stage);
		printf("[SD-DIAG] ===== %s end =====\n\n", phase);
		return;
	}
	stage = SD0_STAGE_OPEN;
	if (!strcmp(phase, "before_mount")) {
		sd0_diag_before_open_ok = 1;
	}
	sd0_diag_ever_open_ok = 1;

	/* 3. 识别层：状态与几何信息 */
	ret = dev_ioctl(fd, IOCTL_GET_STATUS, (u32)&status);
	printf("[SD-DIAG] ioctl STATUS ret=%d val=%u\n", ret, status);
	if (ret == 0 && status != 0) {
		stage = SD0_STAGE_IDENTIFY;
	} else {
		printf("[SD-DIAG] WARN: status=0 may indicate init incomplete\n");
	}

	ret = dev_ioctl(fd, IOCTL_GET_TYPE, (u32)&dev_type);
	printf("[SD-DIAG] ioctl TYPE ret=%d val=%u (optional)\n", ret, dev_type);

	ret = dev_ioctl(fd, IOCTL_GET_ID, (u32)&dev_id);
	printf("[SD-DIAG] ioctl ID ret=%d val=0x%x (optional)\n", ret, dev_id);

	ret = dev_ioctl(fd, IOCTL_GET_CAPACITY, (u32)&capacity);
	printf("[SD-DIAG] ioctl CAPACITY ret=%d val=%u\n", ret, capacity);

	ret = dev_ioctl(fd, IOCTL_GET_BLOCK_SIZE, (u32)&block_size);
	printf("[SD-DIAG] ioctl BLOCK_SIZE ret=%d val=%u\n", ret, block_size);

	ret = dev_ioctl(fd, IOCTL_GET_SECTOR_SIZE, (u32)&sector_size);
	printf("[SD-DIAG] ioctl SECTOR_SIZE ret=%d val=%u\n", ret, sector_size);

	ret = dev_ioctl(fd, IOCTL_GET_BLOCK_NUMBER, (u32)&block_number);
	printf("[SD-DIAG] ioctl BLOCK_NUMBER ret=%d val=%u\n", ret, block_number);

	if (capacity && block_size) {
		stage = SD0_STAGE_GEOMETRY;
		printf("[SD-DIAG] geometry OK: capacity=%u blocks block_size=%u\n",
		       capacity, block_size);
	}

	/* 4. 数据通路层：读 LBA0 */
	buf = dma_malloc(512);
	printf("[SD-DIAG] dma_malloc(512)=%x\n", (int)buf);
	if (!buf) {
		printf("[SD-DIAG] FAIL: no dma buffer\n");
		goto _probe_end;
	}

	ret = sd0_diag_read_lba(fd, buf, 0, "lba0");
	if (ret != 1) {
		printf("[SD-DIAG] FAIL: LBA0 read failed (ret=%d)\n", ret);
		goto _probe_end;
	}
	stage = SD0_STAGE_LBA0;

	/* 5. 分区表层：解析 MBR */
	if ((buf[510] == 0x55) && (buf[511] == 0xaa)) {
		printf("[SD-DIAG] LBA0 has boot signature\n");
		sd0_diag_parse_mbr(buf);
		stage = SD0_STAGE_PARTITION;

		part_lba = sd0_diag_get_le32(&buf[SD0_DIAG_MBR_OFF_PART0 + 8]);
		if (part_lba) {
			ret = sd0_diag_read_lba(fd, buf, part_lba, "pbr");
			if (ret == 1) {
				stage = SD0_STAGE_PBR;
				sd0_diag_parse_pbr(buf, part_lba);
			} else {
				printf("[SD-DIAG] FAIL: cannot read PBR at lba=%u\n", part_lba);
			}
		} else {
			printf("[SD-DIAG] WARN: LBA0 is bootable but part0 start_lba=0 (possible VBR without MBR)\n");
		}
	} else {
		printf("[SD-DIAG] WARN: LBA0 missing 0x55AA, not a valid MBR/VBR\n");
	}

_probe_end:
	if (buf) {
		dma_free(buf);
	}
	if (fd) {
		dev_close(fd);
	}

	sd0_diag_print_summary(phase, stage);
	printf("[SD-DIAG] ===== %s end (cost=%lums) =====\n\n", phase, jiffies_msec() - probe_t0_ms);
}
#endif



static u32 __dev_manager_get_time_stamp(void)
{
	u32 counter = __this->counter;
	__this->counter ++;
	return counter;
}

int __dev_manager_add(char *logo, u8 need_mount)
{
	if (logo == NULL) {
		return DEV_MANAGER_ADD_ERR_PARM;
	}
	int i;
	printf("%s add start\n", logo);
	struct __dev_reg *p = NULL;
	struct __dev_reg *n;

	for(n=(struct __dev_reg *)dev_reg; n->logo != NULL; n++){
		if (!strcmp(n->logo, logo)) {
			p = n;
			break;
		}
	}

	if (p) {
		///挂载文件系统
		if (dev_manager_list_check_by_logo(logo)) {
			printf("dev online aready, err!!!\n");
			return DEV_MANAGER_ADD_IN_LIST_AREADY;
		}
		struct __dev *dev = (struct __dev *)zalloc(sizeof(struct __dev));
		if(dev == NULL){
			return DEV_MANAGER_ADD_ERR_NOMEM;
		}
#if (TCFG_SD0_ENABLE && TCFG_SD0_DIAG_ENABLE)
		if(need_mount && !strcmp(logo, "sd0")) {
			printf("[SD-DIAG] mount(sd0) begin\n");
			/* 诊断 open 可能阻塞数秒，放在设备管理互斥锁外，减少对其它设备流程的影响。 */
			sd0_diag_probe("before_mount");
		}
#endif
		os_mutex_pend(&__this->mutex, 0);
		if(need_mount){
#if (TCFG_SD0_ENABLE && TCFG_SD0_DIAG_ENABLE)
			if (!strcmp(logo, "sd0")) {
				printf("[SD-DIAG] mount(sd0) call (attempt=%u)\n", sd0_diag_attempt);
			}
#endif
			dev->fmnt = mount(p->name, p->storage_path, p->fs_type, 3, NULL);
#if (TCFG_SD0_ENABLE && TCFG_SD0_DIAG_ENABLE)
			if (!strcmp(logo, "sd0")) {
				printf("[SD-DIAG] mount(sd0) attempt=%u result=%x\n", sd0_diag_attempt, (int)dev->fmnt);
			}
#endif

#if (TCFG_SD0_ENABLE && TCFG_SD0_FORMAT_ON_BOOT && TCFG_SD0_AUTO_FORMAT_ON_MOUNT_FAIL_ENABLE)
			if (!strcmp(logo, "sd0")) {
				static u8 _sd0_fmt_done = 0;
				if (!_sd0_fmt_done) {
					_sd0_fmt_done = 1;

#if TCFG_SD0_FORCE_FORMAT_ON_BOOT
					if (dev->fmnt) {
						printf("[SD-FMT] force format test: ignore mount ok\n");
					} else {
						printf("[SD-FMT] force format test: mount already fail\n");
					}
					dev->fmnt = NULL;
#endif

					if (dev->fmnt) {
						printf("[SD-FMT] sd0 mount ok, skip format on boot\n");
					} else {
						u8 fmt_flag = 0;
						int rd = syscfg_read(CFG_SD0_FORMAT_DONE_FLAG, &fmt_flag, sizeof(fmt_flag));
						u8 vm_formatted = (rd == sizeof(fmt_flag) && fmt_flag == TCFG_SD0_FORMAT_DONE_MAGIC);

						printf("[SD-FMT] sd0 mount fail, vm_flag=%s (rd=%d, val=0x%02X)\n",
						       vm_formatted ? "valid" : "invalid", rd, fmt_flag);

						if (vm_formatted) {
							printf("[SD-FMT] fs damaged, recover by format...\n");
						} else {
							printf("[SD-FMT] first boot or vm cleared, format sd0...\n");
						}
						os_time_dly(50);

						int ret = f_format("storage/sd0/C/", "fat", 0);
						if (ret == 0) {
							u32 free_kb = 0;
							fget_free_space("storage/sd0/C/", &free_kb);
							printf("[SD-FMT] format done, free=%u KB (%u MB)\n",
							       free_kb, free_kb / 1024);

							printf("[SD-FMT] remount sd0 after format...\n");
							dev->fmnt = mount(p->name, p->storage_path, p->fs_type, 3, NULL);
							if (dev->fmnt) {
								printf("[SD-FMT] remount ok\n");

								u8 done_flag = TCFG_SD0_FORMAT_DONE_MAGIC;
								int wr = syscfg_write(CFG_SD0_FORMAT_DONE_FLAG, &done_flag, sizeof(done_flag));
								if (wr == sizeof(done_flag)) {
									printf("[SD-FMT] vm flag write ok\n");
								} else {
									printf("[SD-FMT] warning: vm flag write fail (wr=%d), may re-format on next boot\n", wr);
								}
							} else {
								printf("[SD-FMT] warning: remount fail after format, sd0 still unavailable\n");
							}
						} else {
							printf("[SD-FMT] format fail (err=%d)\n", ret);
						}
					}
				}
			}
#endif
		}
		dev->parm = p;
		dev->valid = (dev->fmnt ? 1 : 0);
		dev->active_stamp = __dev_manager_get_time_stamp();
		list_add_tail(&dev->entry, &__this->list);
		os_mutex_post(&__this->mutex);
		printf("%s, %s add ok, dev->fmnt = %x,  %d\n", __FUNCTION__, logo, (int)dev->fmnt, dev->active_stamp);
#if (TCFG_SD0_ENABLE && TCFG_SD0_DIAG_ENABLE)
		if (need_mount && !strcmp(logo, "sd0")) {
			if (dev->fmnt) {
				sd0_diag_last_stage = SD0_STAGE_MOUNT;
				printf("[SD-DIAG] mount(sd0) OK, stage advanced to MOUNT\n");
			} else {
				sd0_diag_last_stage = SD0_STAGE_PBR;
			}
		}
#endif
		if(dev->fmnt == NULL){
#if (TCFG_SD0_ENABLE && TCFG_SD0_DIAG_ENABLE)
			if (need_mount && !strcmp(logo, "sd0")) {
				if (sd0_diag_before_open_ok) {
					sd0_diag_probe("after_mount_fail");
				} else {
					printf("[SD-DIAG] skip after_mount_fail probe because before_mount raw open failed\n");
				}
			}
#endif
			return DEV_MANAGER_ADD_ERR_MOUNT_FAIL;
		}

		return DEV_MANAGER_ADD_OK;
	}
	printf("dev_manager_add can not find logo %s\n",logo);
	return DEV_MANAGER_ADD_ERR_NOT_FOUND;
}

static int __dev_manager_del(char *logo)
{
	if (logo == NULL) {
		return -1;
	}
	struct __dev *dev, *n;
	os_mutex_pend(&__this->mutex, 0);
	list_for_each_entry_safe(dev, n, &__this->list, entry) {
		if (!strcmp(dev->parm->logo, logo)) {
			///卸载文件系统
			if(dev->fmnt){
				unmount(dev->parm->storage_path);
			}
			list_del(&dev->entry);
			free(dev);
			printf("%s, %s del ok\n", __FUNCTION__, logo);
			break;
		}
	}
	os_mutex_post(&__this->mutex);
	return 0;
}

//*----------------------------------------------------------------------------*/
/**@brief    设备增加接口
   @param	 logo:逻辑盘符，如：sd0/sd1/udisk0等
   @return   0:成功，非0是失败
   @note
*/
/*----------------------------------------------------------------------------*/
int dev_manager_add(char *logo)
{
	if(logo == NULL){
		return -1;
	}
	int ret = 0;
#if TCFG_RECORD_FOLDER_DEV_ENABLE
	char rec_dev_logo[16] = {0};
	sprintf(rec_dev_logo, "%s%s", logo, "_rec");
	ret = __dev_manager_add(rec_dev_logo, 1);
	if(ret == DEV_MANAGER_ADD_OK){
		ret = __dev_manager_add(logo, 1);
		if(ret){
			__dev_manager_del(logo);
			__dev_manager_del(rec_dev_logo);
		}
	}else if(ret == DEV_MANAGER_ADD_ERR_NOT_FOUND){
		ret = __dev_manager_add(logo, 1);
	}
	else{
		ret = __dev_manager_add(logo, 0);
	}
#else
	ret = __dev_manager_add(logo, 1);
#endif//TCFG_RECORD_FOLDER_DEV_ENABLE

	return ret;
}

//*----------------------------------------------------------------------------*/
/**@brief    设备删除接口
   @param	 logo:逻辑盘符，如：sd0/sd1/udisk0等
   @return   0:成功，非0是失败
   @note
*/
/*----------------------------------------------------------------------------*/
int dev_manager_del(char *logo)
{
	if(logo == NULL){
		return -1;
	}
	__dev_manager_del(logo);
#if TCFG_RECORD_FOLDER_DEV_ENABLE
    char rec_dev_logo[16] = {0};
	sprintf(rec_dev_logo, "%s%s", logo, "_rec");
	__dev_manager_del(rec_dev_logo);
#endif//TCFG_RECORD_FOLDER_DEV_ENABLE

	return 0;
}
//*----------------------------------------------------------------------------*/
/**@brief    通过设备节点检查设备是否在线
   @param	 dev:设备节点
   @return   成功返回设备节点， 失败返回NULL
   @note     通过设备节点检查设备是否在设备链表中
*/
/*----------------------------------------------------------------------------*/
struct __dev *dev_manager_check(struct __dev *dev)
{
	if (dev == NULL) {
		return NULL;
	}
	struct __dev *p;
	list_for_each_entry(p, &__this->list, entry) {
		if(!(p->fmnt)){
			continue;
		}
		if (dev == p) {
			return p;
		}
	}
	return NULL;
}
//*----------------------------------------------------------------------------*/
/**@brief    通过盘符检查设备是否在线
   @param	 logo:逻辑盘符，如:sd0/sd1/udisk0
   @return   成功返回设备节点， 失败返回NULL
   @note     通过设备节点检查设备是否在设备链表中
*/
/*----------------------------------------------------------------------------*/
struct __dev *dev_manager_check_by_logo(char *logo)
{
	if (logo == NULL) {
		return NULL;
	}
	struct __dev *dev;
	list_for_each_entry(dev, &__this->list, entry) {
		if(!(dev->fmnt)){
			continue;
		}
		if (!strcmp(dev->parm->logo, logo)) {
			return dev;
		}
	}
	return NULL;
}
//*----------------------------------------------------------------------------*/
/**@brief    获取设备总数
   @param	 valid:
   					1：有效可播放设备, 0;所有设备,包括有可播放设备及无可播放设备
   @return   设备总数
   @note     根据使用情景决定接口参数
*/
/*----------------------------------------------------------------------------*/
u32 dev_manager_get_total(u8 valid)
{
	u32 total_valid = 0;
	u32 total = 0;
	struct __dev *dev;
	os_mutex_pend(&__this->mutex, 0);
	list_for_each_entry(dev, &__this->list, entry) {
		if(!(dev->fmnt)){
			continue;
		}
		if (dev->valid) {
			total_valid++;
		}
		total ++;
	}
	os_mutex_post(&__this->mutex);
	return (valid ? total_valid : total);
}
//*----------------------------------------------------------------------------*/
/**@brief    获取设备列表第一个设备
   @param	 valid:
   					1：有效可播放设备, 0;所有设备,包括有可播放设备及无可播放设备
   @return   成功返回设备设备节点,失败返回NULL
   @note     根据使用情景决定接口参数
*/
/*----------------------------------------------------------------------------*/
struct __dev *dev_manager_find_first(u8 valid)
{
	struct __dev *dev = NULL;
	os_mutex_pend(&__this->mutex, 0);
	list_for_each_entry(dev, &__this->list, entry) {
		if(!(dev->fmnt)){
			continue;
		}
		if(valid){
			if (dev->valid) {
				os_mutex_post(&__this->mutex);
				return dev;
			}
		}else{
			os_mutex_post(&__this->mutex);
			return dev;
		}
	}
	os_mutex_post(&__this->mutex);
	return NULL;
}
//*----------------------------------------------------------------------------*/
/**@brief    获取设备列表最后一个设备
   @param	 valid:
   					1：有效可播放设备中查找
					0：所有设备,包括有可播放设备及无可播放设备中查找
   @return   成功返回设备设备节点,失败返回NULL
   @note     根据使用情景决定接口参数
*/
/*----------------------------------------------------------------------------*/
struct __dev *dev_manager_find_last(u8 valid)
{
	struct __dev *dev = NULL;
	os_mutex_pend(&__this->mutex, 0);
	list_for_each_entry_reverse(dev, &__this->list, entry) {
		if(!(dev->fmnt)){
			continue;
		}
		if(valid){
			if (dev->valid) {
				os_mutex_post(&__this->mutex);
				return dev;
			}
		}else{
			os_mutex_post(&__this->mutex);
			return dev;
		}
	}
	os_mutex_post(&__this->mutex);
	return NULL;
}
//*----------------------------------------------------------------------------*/
/**@brief    获取上一个设备节点
   @param
   			dev:当前设备节点
			valid:
   					1：有效可播放设备中查找,
					0：所有设备,包括有可播放设备及无可播放设备中查找
   @return   成功返回设备设备节点,失败返回NULL
   @note     根据当前设置的参数设备节点，找链表中的上一个设备
*/
/*----------------------------------------------------------------------------*/
struct __dev *dev_manager_find_prev(struct __dev *dev, u8 valid)
{
	if (dev == NULL) {
		return NULL;
	}
	struct __dev *p = NULL;
	os_mutex_pend(&__this->mutex, 0);
	if (dev_manager_check(dev) == NULL) {
		///传入的参数无效， 返回活动设备
		os_mutex_post(&__this->mutex);
		return dev_manager_find_active(valid);
	}
	list_for_each_entry(p, &dev->entry, entry) {
        if((void*)p == (void*)(&__this->list)){
            continue;
        }
        if(!(p->fmnt)){
			continue;
		}
		if(valid){
			if (p->valid) {
				os_mutex_post(&__this->mutex);
				return p;
			}
		}else{
			os_mutex_post(&__this->mutex);
			return p;
		}
	}
	os_mutex_post(&__this->mutex);
	return NULL;
}
//*----------------------------------------------------------------------------*/
/**@brief    获取下一个设备节点
   @param
   			dev:当前设备节点
			valid:
   					1：有效可播放设备中查找,
					0：所有设备,包括有可播放设备及无可播放设备中查找
   @return   成功返回设备设备节点,失败返回NULL
   @note     根据当前设置的参数设备节点，找链表中的下一个设备
*/
/*----------------------------------------------------------------------------*/
struct __dev *dev_manager_find_next(struct __dev *dev, u8 valid)
{
	if (dev == NULL) {
		return NULL;
	}
	struct __dev *p = NULL;
	os_mutex_pend(&__this->mutex, 0);
	if (dev_manager_check(dev) == NULL) {
		///传入的参数无效， 返回活动设备
		os_mutex_post(&__this->mutex);
		return dev_manager_find_active(valid);
	}

	list_for_each_entry_reverse(p, &dev->entry, entry) {
        if((void*)p == (void*)(&__this->list)){
            continue;
        }
		if(!(p->fmnt)){
			continue;
		}
		if(valid){
			if (p->valid) {
				os_mutex_post(&__this->mutex);
				return p;
			}
		}else{
			os_mutex_post(&__this->mutex);
			return p;
		}
	}
	os_mutex_post(&__this->mutex);
	return NULL;
}
//*----------------------------------------------------------------------------*/
/**@brief    获取当前活动设备节点
   @param
			valid:
   					1：有效可播放设备中查找,
					0：所有设备,包括有可播放设备及无可播放设备中查找
   @return   成功返回设备设备节点,失败返回NULL
   @note
*/
/*----------------------------------------------------------------------------*/
struct __dev *dev_manager_find_active(u8 valid)
{
	struct __dev *dev = NULL;
	struct __dev *active = NULL;
	os_mutex_pend(&__this->mutex, 0);
	list_for_each_entry(dev, &__this->list, entry) {
		if(!(dev->fmnt)){
			continue;
		}
		if(valid){
			if (dev->valid) {
				if (active) {
					if (active->active_stamp < dev->active_stamp) {
						active = dev;
					}
				} else {
					active = dev;
				}
			}
		}else{
			if (active) {
				if (active->active_stamp < dev->active_stamp) {
					active = dev;
				}
			} else {
				active = dev;
			}

		}
	}
	os_mutex_post(&__this->mutex);
	return active;
}
//*----------------------------------------------------------------------------*/
/**@brief    获取指定设备节点
   @param
   			logo：指定逻辑盘符，如：sd0/sd1/udisk0
			valid:
   					1：有效可播放设备中查找,
					0：所有设备,包括有可播放设备及无可播放设备中查找
   @return   成功返回设备设备节点,失败返回NULL
   @note
*/
/*----------------------------------------------------------------------------*/
struct __dev *dev_manager_find_spec(char *logo, u8 valid)
{
	if (logo == NULL) {
		return NULL;
	}
	struct __dev *dev = NULL;
	os_mutex_pend(&__this->mutex, 0);
	list_for_each_entry(dev, &__this->list, entry) {
		if(!(dev->fmnt)){
			continue;
		}
		if (!strcmp(dev->parm->logo, logo)) {
			if(valid){
				if (dev->valid) {
					os_mutex_post(&__this->mutex);
					return dev;
				}
			}else{
				os_mutex_post(&__this->mutex);
				return dev;
			}
		}
	}
	os_mutex_post(&__this->mutex);
	return NULL;
}
//*----------------------------------------------------------------------------*/
/**@brief    获取指定序号设备节点
   @param
   			index：指定序号，指的是在设备链表中的顺序
			valid:
   					1：有效可播放设备中查找,
					0：所有设备,包括有可播放设备及无可播放设备中查找
   @return   成功返回设备设备节点,失败返回NULL
   @note
*/
/*----------------------------------------------------------------------------*/
struct __dev *dev_manager_find_by_index(u32 index, u8 valid)
{
	struct __dev *dev = NULL;
	u32 i = 0;
	os_mutex_pend(&__this->mutex, 0);
	list_for_each_entry(dev, &__this->list, entry) {
		if(!(dev->fmnt)){
			continue;
		}
		if(valid){
			if (dev->valid) {
				if (i == index) {
					os_mutex_post(&__this->mutex);
					return dev;
				}
				i++;
			}
		}else{
			if (i == index) {
				os_mutex_post(&__this->mutex);
				return dev;
			}
			i++;
		}
	}
	os_mutex_post(&__this->mutex);
	return NULL;
}
#if 0
//*----------------------------------------------------------------------------*/
/**@brief   设备扫盘释放
   @param
   			fsn：扫描句柄
   @return  无
   @note
*/
/*----------------------------------------------------------------------------*/
void dev_manager_scan_disk_release(struct vfscan *fsn)
{
	if (fsn) {
		fscan_release(fsn);
	}
}

//*----------------------------------------------------------------------------*/
/**@brief   设备扫盘
   @param
   			dev：设备节点
   			path：指定扫描目录
   			parm：扫描参数，包括文件后缀等
   			cycle_mode：播放循环模式
			callback：扫描打断回调
   @return  成功返回扫描控制句柄，失败返回NULL
   @note
*/
/*----------------------------------------------------------------------------*/
struct vfscan *dev_manager_scan_disk(struct __dev *dev, const char *path, const char *parm, u8 cycle_mode, struct __scan_callback *callback)
{
	if (dev_manager_check(dev) == NULL) {
		return NULL;
	}
#if TCFG_USB_DM_MULTIPLEX_WITH_SD_DAT0

#if (TCFG_DM_MULTIPLEX_WITH_SD_PORT == 0)     //0:sd0  1:sd1 //dm 参与复用的sd配置
        if(!memcmp(dev->parm->logo ,"sd0",strlen("sd0"))){
#else
        if(!memcmp(dev->parm->logo ,"sd1",strlen("sd1"))){
#endif
            dev_usb_change_sd();
        }

    if(!memcmp(dev->parm->logo ,"udisk",strlen("udisk")))
        dev_sd_change_usb();

    if (dev_manager_online_check(dev, 1) == NULL) {
        printf("mult remount fail !!!\n");
        return NULL;
    }
#endif
	char *fsn_path = NULL;
	char *tmp_path = NULL;
	if (path) {
		if (*path == '/') {
			path++;
		}
		tmp_path = zalloc(strlen(dev->parm->root_path) + strlen(path) + 1);
		if (tmp_path == NULL) {
			return NULL;
		}
		sprintf(tmp_path, "%s%s", dev->parm->root_path, path);
		fsn_path = tmp_path;
	} else {
		fsn_path = dev->parm->root_path;
	}
	printf("fsn_path = %s, scan parm = %s\n", fsn_path, parm);
	struct vfscan *fsn;
	/* clock_add_set(SCAN_DISK_CLK); */
	if(callback && callback->enter){
		callback->enter(dev);//扫描前处理， 可以在注册的回调里提高系统时钟等处理
	}
	fsn = fscan_interrupt(
			(const char *)fsn_path,
			parm,
			DEV_MANAGER_SCAN_DISK_MAX_DEEPTH,
			((callback) ? callback->scan_break : NULL));
	/* clock_remove_set(SCAN_DISK_CLK); */
	if(callback && callback->exit){
		callback->exit(dev);//扫描后处理， 可以在注册的回调里还原到enter前的状态
	}
	if (fsn) {
		if (fsn->file_number == 0) {
			printf("dev nofile\n");
#if (TCFG_DEV_UPDATE_IF_NOFILE_ENABLE)
			///没有文件找升级文件
			dev_update_check(dev->parm->logo);
#endif/*TCFG_DEV_UPDATE_IF_NOFILE_ENABLE*/
			///没有文件,释放fsn， 减少外面流程的处理
			dev_manager_scan_disk_release(fsn);
			fsn = NULL;
		} else {
			fsn->cycle_mode = cycle_mode;
		}
	}

	if (tmp_path) {
		free(tmp_path);
	}
	return fsn;
}
#endif
//*----------------------------------------------------------------------------*/
/**@brief   通过设备节点标记指定设备是否有效
   @param
   			dev：设备节点
			flag:
   					1：设备有效,
					0：设备无效
   @return
   @note    这里有无效是指是否有可播放文件
*/
/*----------------------------------------------------------------------------*/
void dev_manager_set_valid(struct __dev *dev, u8 flag)
{
	os_mutex_pend(&__this->mutex, 0);
	if (dev_manager_check(dev)) {
		dev->valid = flag;
	}
	os_mutex_post(&__this->mutex);
}
//*----------------------------------------------------------------------------*/
/**@brief   通过逻辑盘符标记指定设备是否有效
   @param
   			logo：逻辑盘符，如：sd0/sd1/udisk0
			flag:
   					1：设备有效,
					0：设备无效
   @return
   @note    这里有无效是指是否有可播放文件
*/
/*----------------------------------------------------------------------------*/
void dev_manager_set_valid_by_logo(char *logo, u8 flag)
{
	if (logo == NULL) {
		return ;
	}
	struct __dev *dev = NULL;
	os_mutex_pend(&__this->mutex, 0);
	dev = dev_manager_check_by_logo(logo);
	if (dev) {
		dev->valid = flag;
	}
	os_mutex_post(&__this->mutex);
}
//*----------------------------------------------------------------------------*/
/**@brief   激活指定设备节点的设备
   @param
   			dev：设备节点
   @return
   @note    该接口可以将设备变为最新活动设备
*/
/*----------------------------------------------------------------------------*/
void dev_manager_set_active(struct __dev *dev)
{
	os_mutex_pend(&__this->mutex, 0);
	if (dev_manager_check(dev)) {
		dev->active_stamp = __dev_manager_get_time_stamp();
	}
	os_mutex_post(&__this->mutex);
}
//*----------------------------------------------------------------------------*/
/**@brief   激活指定逻辑盘符的设备
   @param
   			logo：逻辑盘符，如：sd0/sd1/udisk0
   @return
   @note    该接口可以将设备变为最新活动设备
*/
/*----------------------------------------------------------------------------*/
void dev_manager_set_active_by_logo(char *logo)
{
	if (logo == NULL) {
		return ;
	}
	struct __dev *dev = NULL;
	os_mutex_pend(&__this->mutex, 0);
	dev = dev_manager_check_by_logo(logo);
	if (dev) {
		dev->active_stamp = __dev_manager_get_time_stamp();
	}
	os_mutex_post(&__this->mutex);
}
//*----------------------------------------------------------------------------*/
/**@brief   获取设备节点的逻辑盘符
   @param
   			dev：设备节点
   @return  成功返回逻辑盘符，如：sd0/sd1/udisk0，失败返回NULL
   @note
*/
/*----------------------------------------------------------------------------*/
char *dev_manager_get_logo(struct __dev *dev)
{
	char *logo = NULL;
	os_mutex_pend(&__this->mutex, 0);
	if (dev_manager_check(dev) == NULL) {
		os_mutex_post(&__this->mutex);
		return NULL;
	}
	logo = dev->parm->logo;
	os_mutex_post(&__this->mutex);
	return logo;
}
//*----------------------------------------------------------------------------*/
/**@brief   获取物理设备节点的逻辑盘符(去掉_rec后缀)
   @param
   			dev：设备节点
   @return  成功返回逻辑盘符，如：sd0/sd1/udisk0，失败返回NULL
   @note    物理逻辑盘符是指非录音文件夹设备盘符(录音文件夹设备如：sd0_rec)
*/
/*----------------------------------------------------------------------------*/
char *dev_manager_get_phy_logo(struct __dev *dev)
{
    char *logo = dev_manager_get_logo(dev);
    char phy_dev_logo[16] = {0};
    if (logo) {
        char *str = strstr(logo, "_rec");
        if (str) {
            strncpy(phy_dev_logo, logo, strlen(logo) - strlen(str));
			struct __dev *phy_dev = dev_manager_find_spec(phy_dev_logo, 0);
			return dev_manager_get_logo(phy_dev);
        }
	}
	return logo;
}
//*----------------------------------------------------------------------------*/
/**@brief   获取录音文件夹设备节点的逻辑盘符(追加_rec后缀)
   @param
   			dev：设备节点
   @return  成功返回逻辑盘符，如：sd0_rec/sd1_rec/udisk0_rec，失败返回NULL
   @note
*/
/*----------------------------------------------------------------------------*/
char *dev_manager_get_rec_logo(struct __dev *dev)
{
    char *logo = dev_manager_get_logo(dev);
    char rec_dev_logo[16] = {0};
    if (logo) {
        char *str = strstr(logo, "_rec");
		if (!str) {
			sprintf(rec_dev_logo, "%s%s", logo, "_rec");
			struct __dev *rec_dev = dev_manager_find_spec(rec_dev_logo, 0);
			return dev_manager_get_logo(rec_dev);
        }
	}
	return logo;
}
//*----------------------------------------------------------------------------*/
/**@brief   通过设备节点获取设备文件系统根目录
   @param
   			dev：设备节点
   @return  成功返回根目录，失败返回NULL
   @note
*/
/*----------------------------------------------------------------------------*/
char *dev_manager_get_root_path(struct __dev *dev)
{
	char *path = NULL;
	os_mutex_pend(&__this->mutex, 0);
	if (dev_manager_check(dev) == NULL) {
		os_mutex_post(&__this->mutex);
		return NULL;
	}
	path = dev->parm->root_path;
	os_mutex_post(&__this->mutex);
	return path;
}
//*----------------------------------------------------------------------------*/
/**@brief   通过逻辑盘符获取设备文件系统根目录
   @param
   			logo：逻辑盘符，如：sd0/sd1/udisk0
   @return  成功返回根目录，失败返回NULL
   @note
*/
/*----------------------------------------------------------------------------*/
char *dev_manager_get_root_path_by_logo(char *logo)
{
	char *path = NULL;
	os_mutex_pend(&__this->mutex, 0);
	struct __dev *dev = dev_manager_check_by_logo(logo);
	if (dev == NULL) {
		os_mutex_post(&__this->mutex);
		return NULL;
	}
	path = dev->parm->root_path;
	os_mutex_post(&__this->mutex);
	return path;
}

//*----------------------------------------------------------------------------*/
/**@brief   通过设备节点获取设备mount信息
   @param
            dev：设备节点
   @return  成功返回对应指针，失败返回NULL
   @note
*/
/*----------------------------------------------------------------------------*/
struct imount *dev_manager_get_mount_hdl(struct __dev *dev)
{
    if (dev == NULL) {
        return NULL;
    }else{
        return dev->fmnt;
    }
}
//*----------------------------------------------------------------------------*/
/**@brief   通过逻辑盘符判断设备是否在线
   @param
   			logo：逻辑盘符，如：sd0/sd1/udisk0
			valid：
				1：检查有效可播放设备
				0：检查所有设备
   @return  1：在线 0：不在线
   @note
*/
/*----------------------------------------------------------------------------*/
int dev_manager_online_check_by_logo(char *logo, u8 valid)
{
	struct __dev *dev = dev_manager_find_spec(logo, valid);
	return (dev ? 1 : 0);
}
//*----------------------------------------------------------------------------*/
/**@brief   通过设备节点判断设备是否在线
   @param
   			dev：设备节点
			valid：
				1：检查有效可播放设备
				0：检查所有设备
   @return  1：在线 0：不在线
   @note
*/
/*----------------------------------------------------------------------------*/
int dev_manager_online_check(struct __dev *dev, u8 valid)
{
	if (dev_manager_check(dev) == NULL) {
		return 0;
	}else{
		if(valid){
			return (dev->valid ? 1 : 0);
		}else{
			return 1;
		}
	}
}

//*----------------------------------------------------------------------------*/
/**@brief   通过逻辑盘符判断设备是否在设备链表中
   @param
   			logo：逻辑盘符，如：sd0/sd1/udisk0
   @return  1：在设备链表中， 0：不在设备链表中
   @note	该接口会检查所有在设备链表中的设备，忽略mount，valid等状态
*/
/*----------------------------------------------------------------------------*/
struct __dev *dev_manager_list_check_by_logo(char *logo)
{
	if (logo == NULL) {
		return 0;
	}
	struct __dev *dev;
	os_mutex_pend(&__this->mutex, 0);
	list_for_each_entry(dev, &__this->list, entry) {
		if (!strcmp(dev->parm->logo, logo)) {
			os_mutex_post(&__this->mutex);
			return dev;
		}
	}
	os_mutex_post(&__this->mutex);
	return NULL;
}

//*----------------------------------------------------------------------------*/
/**@brief   检查链表中没有挂载的设备并重新挂载
   @param
   			logo：逻辑盘符，如：sd0/sd1/udisk0
   @return  1：在设备链表中， 0：不在设备链表中
   @note	该接口会检查所有在设备链表中的设备，忽略mount，valid等状态
*/
/*----------------------------------------------------------------------------*/
void dev_manager_list_check_mount(void)
{
	struct __dev *dev;
	os_mutex_pend(&__this->mutex, 0);
	list_for_each_entry(dev, &__this->list, entry) {
		if (dev->mount_blocked) {
			continue;
		}

        if(!strcmp(dev->parm->logo,"virfat_flash")){
            continue;
        }

        if(dev->fmnt){
            unmount(dev->parm->storage_path);
            dev->fmnt = NULL;
        }

		if(dev->fmnt == NULL){
			struct __dev_reg *p = dev->parm;
			dev->fmnt = mount(p->name, p->storage_path, p->fs_type, 3, NULL);
			dev->valid = (dev->fmnt ? 1 : 0);
		}
	}
	os_mutex_post(&__this->mutex);
}

//*----------------------------------------------------------------------------*/
/**@brief   设备挂载
   @param
   			logo：逻辑盘符，如：sd0/sd1/udisk0
   @return  0：成功， -1：失败
   @note	需要主动mount设备可以调用改接口
*/
/*----------------------------------------------------------------------------*/
static int __dev_manager_mount(char *logo)
{
	int ret = 0;
	struct __dev *dev = NULL;
	struct __dev *item;

	os_mutex_pend(&__this->mutex, 0);
	list_for_each_entry(item, &__this->list, entry) {
		if (!strcmp(item->parm->logo, logo)) {
			dev = item;
			break;
		}
	}
	if (dev == NULL) {
		os_mutex_post(&__this->mutex);
		return -1;
	}
	if (dev->mount_blocked) {
		os_mutex_post(&__this->mutex);
		return -1;
	}
	if(dev->fmnt == NULL){
		struct __dev_reg *p = dev->parm;
		dev->fmnt = mount(p->name, p->storage_path, p->fs_type, 3, NULL);
		dev->valid = (dev->fmnt ? 1 : 0);
	}
	ret = (dev->valid ? 0 : -1);
	os_mutex_post(&__this->mutex);
	return ret;
}
//*----------------------------------------------------------------------------*/
/**@brief   设备卸载
   @param
   			logo：逻辑盘符，如：sd0/sd1/udisk0
   @return  0：成功， -1：失败
   @note	需要主动unmount设备可以调用改接口
*/
/*----------------------------------------------------------------------------*/
static int __dev_manager_unmount(char *logo)
{
	os_mutex_pend(&__this->mutex, 0);
	struct __dev *dev = NULL;
	struct __dev *item;
	list_for_each_entry(item, &__this->list, entry) {
		if (!strcmp(item->parm->logo, logo)) {
			dev = item;
			break;
		}
	}
	if (dev == NULL) {
		os_mutex_post(&__this->mutex);
		return -1;
	}
	if(dev->fmnt){
		if (unmount(dev->parm->storage_path)) {
			os_mutex_post(&__this->mutex);
			return -1;
		}
		dev->fmnt = NULL;
	}
	dev->valid = 0;
	os_mutex_post(&__this->mutex);
	return 0;
}

//*----------------------------------------------------------------------------*/
/**@brief   设备挂载
   @param
   			logo：逻辑盘符，如：sd0/sd1/udisk0
   @return  0：成功， -1：失败
   @note	慎用
*/
/*----------------------------------------------------------------------------*/
int dev_manager_mount(char *logo)
{
	int ret = __dev_manager_mount(logo);
	if(ret == 0){
#if TCFG_RECORD_FOLDER_DEV_ENABLE
		char rec_dev_logo[16] = {0};
		sprintf(rec_dev_logo, "%s%s", logo, "_rec");
		ret = __dev_manager_mount(rec_dev_logo);
		if(ret){
			__dev_manager_unmount(logo);
		}
#endif
	}
	return ret;
}

//*----------------------------------------------------------------------------*/
/**@brief   设备卸载
   @param
   			logo：逻辑盘符，如：sd0/sd1/udisk0
   @return  0：成功， -1：失败
   @note	慎用
*/
/*----------------------------------------------------------------------------*/
int dev_manager_unmount(char *logo)
{
	int err = 0;
#if TCFG_RECORD_FOLDER_DEV_ENABLE
	char rec_dev_logo[16] = {0};
	sprintf(rec_dev_logo, "%s%s", logo, "_rec");
	err = __dev_manager_unmount(rec_dev_logo);
	if (err) {
		return err;
	}
#endif
	err = __dev_manager_unmount(logo);
	return err;
}

static int __dev_manager_set_mount_blocked(char *logo, u8 blocked)
{
	int ret = -1;
	struct __dev *dev;

	os_mutex_pend(&__this->mutex, 0);
	list_for_each_entry(dev, &__this->list, entry) {
		if (!strcmp(dev->parm->logo, logo)) {
			dev->mount_blocked = blocked;
			ret = 0;
			break;
		}
	}
	os_mutex_post(&__this->mutex);
	return ret;
}

int dev_manager_takeover(char *logo)
{
	if (logo == NULL) {
		return -1;
	}

#if TCFG_RECORD_FOLDER_DEV_ENABLE
	char rec_dev_logo[16] = {0};
	sprintf(rec_dev_logo, "%s%s", logo, "_rec");
	if (__dev_manager_set_mount_blocked(rec_dev_logo, 1)) {
		return -1;
	}
#endif

	if (__dev_manager_set_mount_blocked(logo, 1)) {
#if TCFG_RECORD_FOLDER_DEV_ENABLE
		__dev_manager_set_mount_blocked(rec_dev_logo, 0);
#endif
		return -1;
	}

	int err = dev_manager_unmount(logo);
	if (err) {
		__dev_manager_set_mount_blocked(logo, 0);
#if TCFG_RECORD_FOLDER_DEV_ENABLE
		__dev_manager_set_mount_blocked(rec_dev_logo, 0);
#endif
		dev_manager_mount(logo);
	}
	return err;
}

static int __dev_manager_restore_mount(char *logo)
{
	int ret = -1;
	struct __dev *dev;

	os_mutex_pend(&__this->mutex, 0);
	list_for_each_entry(dev, &__this->list, entry) {
		if (strcmp(dev->parm->logo, logo)) {
			continue;
		}
		if (!dev->mount_blocked) {
			break;
		}
		if (dev->fmnt == NULL) {
			struct __dev_reg *p = dev->parm;
			dev->fmnt = mount(p->name, p->storage_path, p->fs_type, 3, NULL);
			dev->valid = (dev->fmnt ? 1 : 0);
		}
		ret = (dev->valid ? 0 : -1);
		break;
	}
	os_mutex_post(&__this->mutex);
	return ret;
}

int dev_manager_restore(char *logo)
{
	if (logo == NULL) {
		return -1;
	}

#if TCFG_RECORD_FOLDER_DEV_ENABLE
	char rec_dev_logo[16] = {0};
	sprintf(rec_dev_logo, "%s%s", logo, "_rec");
#endif

	/* Mount while blocked; publish the restored nodes only after all mounts pass. */
	if (__dev_manager_restore_mount(logo)) {
		return -1;
	}
#if TCFG_RECORD_FOLDER_DEV_ENABLE
	if (__dev_manager_restore_mount(rec_dev_logo)) {
		__dev_manager_unmount(logo);
		return -1;
	}
#endif

	if (__dev_manager_set_mount_blocked(logo, 0)) {
		__dev_manager_unmount(logo);
		return -1;
	}
#if TCFG_RECORD_FOLDER_DEV_ENABLE
	if (__dev_manager_set_mount_blocked(rec_dev_logo, 0)) {
		__dev_manager_set_mount_blocked(logo, 1);
		__dev_manager_unmount(rec_dev_logo);
		__dev_manager_unmount(logo);
		return -1;
	}
#endif
	return 0;
}
//*----------------------------------------------------------------------------*/
/**@brief   设备消息处理
   @param
   @return
   @note  具体实现放在APP里
*/
/*----------------------------------------------------------------------------*/
void __attribute__((weak))  app_common_device_event_handler(int *msg)
{

}

//*----------------------------------------------------------------------------*/
/**@brief   设备检测线程处理
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
extern void hidden_file(u8 flag);
static void dev_manager_task(void *p)
{
	int res = 0;
	int msg[16] = {0};
	///过滤隐藏 和 .开头名名的字文件
	hidden_file(1);
	///设备初始化，
	devices_init();

#if TCFG_SD_ALWAY_ONLINE_ENABLE
#if (defined(TCFG_SD0_ENABLE) && (TCFG_SD0_ENABLE))
	extern void force_set_sd_online(char *sdx);
	force_set_sd_online("sd0");
	int err = dev_manager_add("sd0");
#elif (defined(TCFG_SD1_ENABLE) && (TCFG_SD1_ENABLE))
	extern void force_set_sd_online(char *sdx);
	force_set_sd_online("sd1");
	int err = dev_manager_add("sd1");
#endif
	if (err != 0) {
		printf("sd add fail\n");
	} else {
#if (TCFG_SD_ALWAY_ONLINE_ENABLE && (TCFG_SD0_ENABLE || TCFG_SD1_ENABLE))
        extern void sdx_dev_detect_timer_del();
        sdx_dev_detect_timer_del();
#endif
    }
#endif

#if 1//SDFILE_STORAGE && TCFG_CODE_FLASH_ENABLE

	dev_manager_add(SDFILE_DEV);
#endif

#if TCFG_NOR_REC
	dev_manager_add("rec_nor");
#endif

#if TCFG_NOR_FAT
    dev_manager_add("fat_nor");
    dev_manager_set_valid_by_logo("fat_nor", 0);///将设备设置为无效设备
#endif

#if TCFG_NOR_FS
	dev_manager_add("res_nor");
#endif

#if TCFG_VIR_UDISK_ENABLE
	dev_manager_add("vir_udisk0");
#endif

#if TCFG_VIRFAT_FLASH_ENABLE
	dev_manager_add("virfat_flash");
    dev_manager_set_valid_by_logo("virfat_flash", 0);///将设备设置为无效设备
#endif

#if FLASH_INSIDE_REC_ENABLE
    set_rec_capacity(512*1024);//需要先设置容量,注意要小于Ini文件设置大小.
    _sdfile_rec_init();
	dev_manager_add("rec_sdfile");
#endif

	os_sem_post(&__this->sem);

	while (1) {
		res = os_taskq_pend("taskq", msg, ARRAY_SIZE(msg));
		switch (res) {
			case OS_TASKQ:
				switch (msg[0]) {
					case MSG_FROM_DEVICE:
						app_common_device_event_handler(msg+1);
						break;
					default:
						break;
				}
				break;
			default:
				break;
		}
	}
}

void dev_manager_var_init()
{
	memset(__this, 0, sizeof(struct __dev_manager));
	INIT_LIST_HEAD(&__this->list);
	os_mutex_create(&__this->mutex);
}

#endif/*TCFG_DEV_MANAGER_ENABLE*/
//*----------------------------------------------------------------------------*/
/**@brief   设备管理器初始化
   @param
   @return
   @note
*/
/*----------------------------------------------------------------------------*/
void dev_manager_init(void)
{
#if (TCFG_DEV_MANAGER_ENABLE)
    dev_manager_var_init();

	os_sem_create(&__this->sem, 0);

	int err = task_create(dev_manager_task, NULL, DEV_MANAGER_TASK_NAME);
	if (err != OS_NO_ERR) {
		ASSERT(0, "task_create fail!!! %x\n", err);
	}
	os_sem_pend(&__this->sem, 0);
#else
	devices_init();
#endif
}
