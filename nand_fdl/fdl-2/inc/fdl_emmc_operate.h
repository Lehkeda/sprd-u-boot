#ifndef _FDL_EMMC_OPERATE_H
#define _FDL_EMMC_OPERATE_H

#include <asm/arch/fdl_stdio.h>
#include <asm/arch/cmd_def.h>
#include <asm/arch/packet.h>
#ifdef CONFIG_EMMC_BOOT

typedef enum _PARTITION_IMG_TYPE
{
	IMG_RAW = 0,
	IMG_WITH_SPARSE = 1,
	IMG_TYPE_MAX
}PARTITION_IMG_TYPE;

typedef enum _PARTITION_PURPOSE
{
	PARTITION_PURPOSE_NORMAL,
	PARTITION_PURPOSE_NV,
	PARTITION_PURPOSE_MAX
}PARTITION_PURPOSE;

typedef struct DL_EMMC_STATUS_TAG
{
	uint32 part_total_size ;
	uint32 base_sector;
	wchar_t *curUserPartitionName;
	PARTITION_PURPOSE partitionpurpose;
	uint8 curEMMCArea;
	PARTITION_IMG_TYPE curImgType; 
} DL_EMMC_STATUS;

typedef struct DL_FILE_STATUS_TAG
{
	unsigned long   total_size;
	unsigned long   total_recv_size;
	unsigned long   unsave_recv_size;
} DL_EMMC_FILE_STATUS;

typedef struct _SPECIAL_PARTITION_CFG
{
	wchar_t* partition;
	wchar_t* bak_partition;
	PARTITION_IMG_TYPE imgattr;
	PARTITION_PURPOSE purpose;
}SPECIAL_PARTITION_CFG;

#if defined(CONFIG_TIGER) || defined(CONFIG_SC7710G2) || defined(CONFIG_SC8830)
#define BOOTLOADER_HEADER_OFFSET 0x20
typedef struct{
	uint32 version;
	uint32 magicData;
	uint32 checkSum;
	uint32 hashLen;
}EMMC_BootHeader;
#endif

PUBLIC int fdl2_emmc_download_start(wchar_t * partition_name, unsigned long size, unsigned long nv_checksum);
PUBLIC int fdl2_emmc_download(unsigned short size, char *buf);
PUBLIC int fdl2_emmc_download_end(void);
PUBLIC int fdl2_emmc_read_start(wchar_t* partition_name, unsigned long size);
PUBLIC int fdl2_emmc_read_midst(unsigned long size, unsigned long off, unsigned char * buf);
PUBLIC int fdl2_emmc_read_end(void);
PUBLIC int fdl2_emmc_erase(wchar_t * partition_name, unsigned long size);
PUBLIC int fdl2_emmc_repartition(unsigned short * partition_cfg, unsigned short total_partition_num);

#endif  //CONFIG_EMMC_BOOT
#endif  //_FDL_EMMC_OPERATE_H