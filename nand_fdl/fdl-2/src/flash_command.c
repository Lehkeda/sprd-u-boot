#include "sci_types.h"
#include "dload_op.h"
#include "flash_command.h"
#include "fdl_nand.h"
#include "packet.h"
#include "fdl_conf.h"
#include "fdl_crc.h"
#include "fdl_stdio.h"
#include "parsemtdparts.h"


#include "asm/arch/sci_types.h"
#include "asm/arch/nand_controller.h"
#include <linux/mtd/mtd.h>
#include <nand.h>
#include <linux/mtd/nand.h>
#include <jffs2/jffs2.h>
#include <malloc.h>

extern void cmd_yaffs_mount(char *mp);
extern void cmd_yaffs_umount(char *mp);
extern int cmd_yaffs_ls_chk(const char *dirfilename);
extern void cmd_yaffs_mread_file(char *fn, unsigned char *addr);
extern void cmd_yaffs_mwrite_file(char *fn, char *addr, int size);

#define FIXNV_SIZE		(64 * 1024)
#define PHASECHECK_SIZE		(3 * 1024)
#define TRANS_CODE_SIZE		(MAX_PKT_SIZE - (2 * 1024))

typedef struct _DL_FILE_STATUS
{
    unsigned long   total_size;
    unsigned long   recv_size;
} DL_FILE_STATUS, *PDL_FILE_STATUS;
static unsigned long g_checksum;
static unsigned long g_sram_addr;
static int read_yaffs_flag = 0;
__align(4) unsigned char g_fixnv_buf[FIXNV_SIZE + 4];
__align(4) unsigned char g_fixnv_buf_yaffs[FIXNV_SIZE + 4];

#define CHECKSUM_OTHER_DATA       0x5555aaaa
static DL_FILE_STATUS g_status;
static int g_prevstatus;
static __inline void FDL2_SendRep (unsigned long err)
{
    FDL_SendAckPacket (convert_err (err));
}
unsigned long FDL2_GetRecvDataSize (void)
{
    return g_status.recv_size;
}

struct real_mtd_partition phy_partition;
static unsigned int is_nbl_write;
static unsigned int is_phasecheck_write;
static unsigned int g_NBLFixBufDataSize = 0;
static unsigned char g_FixNBLBuf[0x8000];
static unsigned int g_PhasecheckBUFDataSize = 0;
static unsigned char g_PhasecheckBUF[0x2000];
static DL_OP_RECORD_S g_dl_op_table[DL_OP_MTD_COUNT];
static unsigned long g_dl_op_index = 0;
static unsigned long is_factorydownload_tools = 0;

#ifdef  TRANS_CODE_SIZE
#define min(A,B)		(((A) < (B)) ? (A) : (B))
#define PAGE_SIZE		(2048)
#define PAGE_OOB		(64)
#define	DATA_BUFFER_SIZE	(TRANS_CODE_SIZE * 2)
#define YAFFS_BUFFER_SIZE	(DATA_BUFFER_SIZE + (DATA_BUFFER_SIZE / PAGE_SIZE) * PAGE_OOB)
static unsigned long g_BigSize = 0;
static unsigned long code_yaffs_buflen	= 0;
static unsigned long code_yaffs_onewrite = 0;
static unsigned char *g_BigBUF = NULL;
#endif
typedef struct _CUSTOM2LOG
{
    unsigned long   custom;
    unsigned long   log;
} CUSTOM2LOG;

static CUSTOM2LOG custom2log_table[] = {
	{0x90000001, 0x80000005}, 
	{0x90000003, 0x80000008}, 
	{0x90000002, 0x80000011},
	{0xffffffff, 0xffffffff}
};
#define ECC_NBL_SIZE 0x4000
//bootloader header flag offset from the beginning
#define BOOTLOADER_HEADER_OFFSET   32
#define NAND_PAGE_LEN              512
#define NAND_MAGIC_DATA            0xaa55a5a5
//define nand data bus len
#define NAND_BUS_SIZE_8              8
#define NAND_BUS_SIZE_16              16
#define NAND_BUS_SIZE_32              32

void set_dl_op_val(unsigned long addr, unsigned long size, DL_OP_TYPE_E type, DL_OP_STATUS_E status, unsigned long cnt)
{
	if (g_dl_op_index >= DL_OP_MTD_COUNT) {
		printf("\nmtd count is beyoned %d\n", DL_OP_MTD_COUNT);
		return;
	}
    
	switch (type) {
		case STARTDATA:
			if (status == FAIL) {
				memset(&(g_dl_op_table[g_dl_op_index]), 0, sizeof(DL_OP_RECORD_S));
				g_dl_op_table[g_dl_op_index].base = addr;
    				g_dl_op_table[g_dl_op_index].size = size;
    				g_dl_op_table[g_dl_op_index].type = type;
    				g_dl_op_table[g_dl_op_index].status = status;
    				g_dl_op_table[g_dl_op_index].status_cnt = cnt;
			} else {
    				g_dl_op_table[g_dl_op_index].status = status;
    				g_dl_op_table[g_dl_op_index].status_cnt = cnt;
				g_dl_op_index++;
				memset(&(g_dl_op_table[g_dl_op_index]), 0, sizeof(DL_OP_RECORD_S));
				g_dl_op_table[g_dl_op_index].base = addr;
    				g_dl_op_table[g_dl_op_index].size = size;
			}
		break;
		case MIDSTDATA:
			g_dl_op_table[g_dl_op_index].type = type;
			g_dl_op_table[g_dl_op_index].status = status;
			if (status == FAIL)
    				g_dl_op_table[g_dl_op_index].status_cnt = cnt;
			else
				g_dl_op_table[g_dl_op_index].status_cnt++;
		break;
		case ENDDATA:
			if ((status == FAIL) && (cnt == 1)) {
				g_dl_op_index++;
				memset(&(g_dl_op_table[g_dl_op_index]), 0, sizeof(DL_OP_RECORD_S));
				g_dl_op_table[g_dl_op_index].base = g_dl_op_table[g_dl_op_index - 1].base;
				g_dl_op_table[g_dl_op_index].size = g_dl_op_table[g_dl_op_index - 1].size;
				g_dl_op_table[g_dl_op_index].type = type;
				g_dl_op_table[g_dl_op_index].status = status;
				g_dl_op_table[g_dl_op_index].status_cnt = cnt;
			} else {
				g_dl_op_table[g_dl_op_index].status = status;
				g_dl_op_table[g_dl_op_index].status_cnt = cnt;
				if (status == SUCCESS) {
					g_dl_op_index++;
					memset(&(g_dl_op_table[g_dl_op_index]), 0, sizeof(DL_OP_RECORD_S));
				}
			}
		break;
		case ERASEFLASH:
			if (status == FAIL) {
				memset(&(g_dl_op_table[g_dl_op_index]), 0, sizeof(DL_OP_RECORD_S));
				g_dl_op_table[g_dl_op_index].base = addr;
				g_dl_op_table[g_dl_op_index].size = size;
				g_dl_op_table[g_dl_op_index].type = type;
				g_dl_op_table[g_dl_op_index].status = status;
				g_dl_op_table[g_dl_op_index].status_cnt = cnt;
			} else {
				g_dl_op_table[g_dl_op_index].status = status;
				g_dl_op_table[g_dl_op_index].status_cnt = cnt;
				g_dl_op_index++;
				memset(&(g_dl_op_table[g_dl_op_index]), 0, sizeof(DL_OP_RECORD_S));
			}
		break;
		case READFLASH:
		break;
	}	
}

DL_OP_STATUS_E check_dl_data_status(unsigned long addr)
{
	int cnt;
	DL_OP_STATUS_E status;
	
	status = FAIL;
	for (cnt = 0; cnt < g_dl_op_index; cnt ++)
		if ((g_dl_op_table[cnt].base == addr) && (g_dl_op_table[cnt].type == STARTDATA) && (g_dl_op_table[cnt].status == SUCCESS) && (g_dl_op_table[cnt].status_cnt == 1)) {
			if ((cnt + 2) < g_dl_op_index) {
				if ((g_dl_op_table[cnt + 1].base == addr) && (g_dl_op_table[cnt + 1].type == MIDSTDATA) && (g_dl_op_table[cnt + 1].status == SUCCESS) && (g_dl_op_table[cnt + 1].status_cnt >= 1)) {
					if ((g_dl_op_table[cnt + 2].base == addr) && (g_dl_op_table[cnt + 2].type == ENDDATA) && (g_dl_op_table[cnt + 2].status == SUCCESS) && (g_dl_op_table[cnt + 2].status_cnt == 1)) {
						status = SUCCESS;
						break;
					}
				}
			}
		}

	return status;
}

DL_OP_STATUS_E check_dl_erase_status(unsigned long addr)
{
	int cnt;
	DL_OP_STATUS_E status;
	
	status = FAIL;
	for (cnt = 0; cnt < g_dl_op_index; cnt ++)
		if ((g_dl_op_table[cnt].base == addr) && (g_dl_op_table[cnt].type == ERASEFLASH) && (g_dl_op_table[cnt].status == SUCCESS) && (g_dl_op_table[cnt].status_cnt == 1)) {
			status = SUCCESS;
			break;
		}

	return status;
}

unsigned long custom2log(unsigned long custom)
{
	unsigned long idx, log = 0xffffffff;

	if ((custom & 0xf0000000) == 0x80000000)
		return custom;
	for (idx = 0; custom2log_table[idx].custom != 0xffffffff; idx ++) {
		if (custom2log_table[idx].custom == custom) {
			log = custom2log_table[idx].log;
			break;
		}
	}
	
	return log;
}

void phy_partition_info(struct real_mtd_partition phy)
{
	int i;

	if (phy.offset == 0xffffffff) {
		printf("\n\nInvaild partition address\n\n");
		return;
	}

	//printf("name : %20s, offset : 0x%08x, size : 0x%08x, yaffs : %d\n", phy.name, phy.offset, phy.size, phy.yaffs);

	return;
}
/*
* retval : -1 is wrong  ;  1 is correct
*/
int nv_is_correct(unsigned char *array, unsigned long size)
{
	if ((array[size] == 0x5a) && (array[size + 1] == 0x5a) && (array[size + 2] == 0x5a) && (array[size + 3] == 0x5a)) {
		array[size] = 0xff; array[size + 1] = 0xff;
		array[size + 2] = 0xff; array[size + 3] = 0xff;	
		return 1;
	} else
		return -1;
}

/*
   1 ; success
   2 : error
*/
int nand_read_fdl_yaffs(struct real_mtd_partition *phypart, unsigned int off, unsigned int size, unsigned char *buf)
{
	int ret = 0;
	int pos;
	unsigned long addr = phypart->offset;

	if (strcmp(phypart->name, "fixnv") == 0) {
		/* for fixnv, read total 64KB */
		char *fixnvpoint = "/fixnv";
		char *fixnvfilename = "/fixnv/fixnv.bin";
		char *fixnvfilename2 = "/fixnv/fixnvchange.bin";

		if (read_yaffs_flag == 0) {
			memset(g_fixnv_buf_yaffs, 0xff, FIXNV_SIZE + 4);
			/* read fixnv */
    			cmd_yaffs_mount(fixnvpoint);
			ret = cmd_yaffs_ls_chk(fixnvfilename);
			if (ret == (FIXNV_SIZE + 4)) {
				cmd_yaffs_mread_file(fixnvfilename, g_fixnv_buf_yaffs);
				if (-1 == nv_is_correct(g_fixnv_buf_yaffs, FIXNV_SIZE)) {
					memset(g_fixnv_buf_yaffs, 0xff, FIXNV_SIZE + 4);
					/* read fixnv backup */
					ret = cmd_yaffs_ls_chk(fixnvfilename2);
					if (ret == (FIXNV_SIZE + 4)) {
						cmd_yaffs_mread_file(fixnvfilename2, g_fixnv_buf_yaffs);
						if (-1 == nv_is_correct(g_fixnv_buf_yaffs, FIXNV_SIZE)) {
							memset(g_fixnv_buf_yaffs, 0xff, FIXNV_SIZE + 4);
							read_yaffs_flag = 2;
						} else
							read_yaffs_flag = 1;
					} else
						read_yaffs_flag = 2;//error
					/* read fixnv backup */
				} else
					read_yaffs_flag = 1;//success
			} else {
				/* read fixnv backup */
				ret = cmd_yaffs_ls_chk(fixnvfilename2);
				if (ret == (FIXNV_SIZE + 4)) {
					cmd_yaffs_mread_file(fixnvfilename2, g_fixnv_buf_yaffs);
					if (-1 == nv_is_correct(g_fixnv_buf_yaffs, FIXNV_SIZE)) {
						memset(g_fixnv_buf_yaffs, 0xff, FIXNV_SIZE + 4);
						read_yaffs_flag = 2;
					} else
						read_yaffs_flag = 1;
				} else
					read_yaffs_flag = 2;//error
				/* read fixnv backup */
			}
			cmd_yaffs_umount(fixnvpoint);
		}

		memcpy(buf, (unsigned char *)(g_fixnv_buf_yaffs + off), size);

		if (read_yaffs_flag == 1)
			return NAND_SUCCESS;
		return NAND_SYSTEM_ERROR;
	}//if (strcmp(phypart->name, "fixnv") == 0)

	if (strcmp(phypart->name, "productinfo") == 0) {
		/* for dlstatus, read real length */
		char *productinfopoint = "/productinfo";
		char *productinfofilename = "/productinfo/dlstatus.txt";

		if (read_yaffs_flag == 0) {
			memset(g_PhasecheckBUF, 0, 0x2000);
			/* read dlstatus */
    			cmd_yaffs_mount(productinfopoint);
			ret = cmd_yaffs_ls_chk(productinfofilename);
			if (ret >= DL_OP_RECORD_LEN) {
				cmd_yaffs_mread_file(productinfofilename, g_PhasecheckBUF);
				read_yaffs_flag = 1;//success
			}
			cmd_yaffs_umount(productinfopoint);
		}

		memcpy(buf, (unsigned char *)(g_PhasecheckBUF + off), size);

		if (read_yaffs_flag == 1)
			return NAND_SUCCESS;
		return NAND_SYSTEM_ERROR;
	}//if (strcmp(phypart->name, "dlstatus") == 0)
}


int FDL2_DataStart (PACKET_T *packet, void *arg)
{
    unsigned long *data = (unsigned long *) (packet->packet_body.content);
    unsigned long start_addr = *data;
    unsigned long size = * (data + 1);
    int           ret;
#if defined(CHIP_ENDIAN_LITTLE)
    start_addr = EndianConv_32 (start_addr);
    size = EndianConv_32 (size);
#endif

    set_dl_op_val(start_addr, size, STARTDATA, FAIL, 1);

    if (packet->packet_body.size == 12)
    {
	memset(g_fixnv_buf, 0xff, FIXNV_SIZE + 4);
        g_checksum = * (data+2);
        g_sram_addr = (unsigned long) g_fixnv_buf;
    } else {
	        g_checksum = CHECKSUM_OTHER_DATA;
    }
    if (0 == (g_checksum & 0xffffff))
    {
        //The fixnv checksum is error.
        SEND_ERROR_RSP (BSL_EEROR_CHECKSUM); /*lint !e527*/
    }

    do
    {
	memset(&phy_partition, 0, sizeof(struct real_mtd_partition));
	phy_partition.offset = custom2log(start_addr);
	ret = log2phy_table(&phy_partition);
	phy_partition_info(phy_partition);

	if (NAND_SUCCESS != ret)
        	break;

	if (size >= phy_partition.size) {
		printf("\n\nimage file size : 0x%08x is bigger than partition size : 0x%08x\n", size, phy_partition.size);
		ret = NAND_INVALID_SIZE;
	}

	if (NAND_SUCCESS != ret)
        	break;

        ret = nand_start_write (&phy_partition, size);

        if (NAND_SUCCESS != ret)
            break;

	is_nbl_write = 0;
	is_phasecheck_write = 0;

	if (strcmp(phy_partition.name, "spl") == 0) {
		is_nbl_write = 1;
		g_NBLFixBufDataSize = 0;
	} else if (strcmp(phy_partition.name, "productinfo") == 0) {
		is_phasecheck_write = 1;
		g_PhasecheckBUFDataSize = 0;
		memset(g_PhasecheckBUF, 0xff, 0x2000);
	}

#ifdef TRANS_CODE_SIZE
	if (phy_partition.yaffs == 0) {
		code_yaffs_buflen = DATA_BUFFER_SIZE;
		code_yaffs_onewrite = PAGE_SIZE;
	} else if (phy_partition.yaffs == 1) {
		code_yaffs_buflen = YAFFS_BUFFER_SIZE;
		code_yaffs_onewrite = PAGE_SIZE + PAGE_OOB;
	}

	g_BigSize = 0;
	if (g_BigBUF == NULL)
		g_BigBUF = (unsigned char *)malloc(YAFFS_BUFFER_SIZE);

	if (g_BigBUF == NULL) {
		printf("malloc is wrong : %d\n", YAFFS_BUFFER_SIZE);
		ret = NAND_SYSTEM_ERROR;		
		break;
	}
	memset(g_BigBUF, 0xff, YAFFS_BUFFER_SIZE);
#endif

#ifdef TRANS_CODE_SIZE
	if (phy_partition.yaffs == 0) {
		code_yaffs_buflen = DATA_BUFFER_SIZE;
		code_yaffs_onewrite = PAGE_SIZE;
	} else if (phy_partition.yaffs == 1) {
		code_yaffs_buflen = YAFFS_BUFFER_SIZE;
		code_yaffs_onewrite = PAGE_SIZE + PAGE_OOB;
	}

	g_BigSize = 0;
	if (g_BigBUF == NULL)
		g_BigBUF = (unsigned char *)malloc(YAFFS_BUFFER_SIZE);

	if (g_BigBUF == NULL) {
		printf("malloc is wrong : %d\n", YAFFS_BUFFER_SIZE);
		ret = NAND_SYSTEM_ERROR;		
		break;
	}
	memset(g_BigBUF, 0xff, YAFFS_BUFFER_SIZE);
#endif

        g_status.total_size  = size;
        g_status.recv_size   = 0;
        g_prevstatus = NAND_SUCCESS;

	set_dl_op_val(start_addr, size, STARTDATA, SUCCESS, 1);
        FDL_SendAckPacket (BSL_REP_ACK);	
        return 1;
    }
    while (0);

    FDL2_SendRep (ret);
    return 0;
}

/******************************************************************************
 * make the checksum of one packet
 ******************************************************************************/
unsigned short CheckSum(const unsigned int *src, int len)
{
    unsigned int   sum = 0;
    unsigned short *src_short_ptr = PNULL;

    while (len > 3)
    {
        sum += *src++;
        len -= 4;
    }

    src_short_ptr = (unsigned short *) src;

    if (0 != (len&0x2))
    {
        sum += * (src_short_ptr);
        src_short_ptr++;
    }

    if (0 != (len&0x1))
    {
        sum += * ( (unsigned char *) (src_short_ptr));
    }

    sum  = (sum >> 16) + (sum & 0x0FFFF);
    sum += (sum >> 16);

    return (unsigned short) (~sum);
}

/******************************************************************************
 * change the header of first bootloader page
 ******************************************************************************/
int NandChangeBootloaderHeader(unsigned int *bl_start_addr)
{
    unsigned int       *start_addr = bl_start_addr;
    unsigned short     check_sum = 0;

    unsigned short 	g_PageAttr = 0x1;
    unsigned short      nCycleDev = 0x5;
    unsigned int        nAdvance = 1;


    //set pointer to nand parameter config start address
    start_addr += BOOTLOADER_HEADER_OFFSET / 4;

    //set nand page attribute
    * (start_addr + 1) = g_PageAttr;
    //set nand address cycle
    * (start_addr+2) = nCycleDev;

    //set nand data bus len
    //* (start_addr + 3) = NAND_BUS_SIZE_8;
    * (start_addr + 3) = NAND_BUS_SIZE_16;
    //* (start_addr + 3) = NAND_BUS_SIZE_32;

    if (0) // for 6800h
    {
        //set magic data
        * (start_addr+4) = NAND_MAGIC_DATA;
        //make checksum of first 504 bytes
        check_sum = CheckSum ((unsigned int *) (start_addr + 1), (NAND_PAGE_LEN - BOOTLOADER_HEADER_OFFSET - 4));
    }
    else
    {
        if (nAdvance)
            * (start_addr + 4) = 1;
        else
            * (start_addr + 4) = 0;

        //set magic data
        * (start_addr + 5) = NAND_MAGIC_DATA;

        //make checksum of first 504 bytes
        check_sum = CheckSum((unsigned int *)(start_addr + 1), (NAND_PAGE_LEN - BOOTLOADER_HEADER_OFFSET - 4));
    }

    //set checksum
    * (start_addr) = (unsigned int) check_sum;

    return 1;

}

int NandWriteAndCheck(unsigned int size, unsigned char *buf)
{

    memcpy (g_FixNBLBuf + g_NBLFixBufDataSize, buf, size); /* copy the data to the temp buffer */
    g_NBLFixBufDataSize += size;

    if ((g_NBLFixBufDataSize) <= ECC_NBL_SIZE)
    {
        return NAND_SUCCESS;
    }
#ifndef CONFIG_NAND_SC8810	
    NandChangeBootloaderHeader((unsigned int *) g_FixNBLBuf);
#endif	
    return NAND_SUCCESS;
}

int FDL2_DataMidst (PACKET_T *packet, void *arg)
{
    	unsigned long size;
	unsigned long ii;

    /* The previous download step failed. */
    if (NAND_SUCCESS != g_prevstatus)
    {
	set_dl_op_val(0, 0, MIDSTDATA, FAIL, 1);
        FDL2_SendRep (g_prevstatus);
        return 0;
    }

    size = packet->packet_body.size;
	//printf("size = %d  recv_size = %d   total_size = %d\n", size, g_status.recv_size, g_status.total_size);
    if ( (g_status.recv_size + size) > g_status.total_size)
    {
        g_prevstatus = NAND_INVALID_SIZE;
	set_dl_op_val(0, 0, MIDSTDATA, FAIL, 2);
        FDL2_SendRep (g_prevstatus);
        return 0;
    }

    if (CHECKSUM_OTHER_DATA == g_checksum)
    {
        if (is_nbl_write == 1) {
		g_prevstatus = NandWriteAndCheck( (unsigned int) size, (unsigned char *) (packet->packet_body.content));
	} else if (is_phasecheck_write == 1) {
		//printf("g_PhasecheckBUFDataSize = %d\n", g_PhasecheckBUFDataSize);
        	memcpy((g_PhasecheckBUF + g_PhasecheckBUFDataSize), (char *)(packet->packet_body.content), size);
        	g_PhasecheckBUFDataSize += size;
		g_prevstatus = NAND_SUCCESS;
	} else {
#ifdef TRANS_CODE_SIZE
		//printf("g_BigSize = %d  buflen = %d, onewrite = %d  size = %d\n", g_BigSize, code_yaffs_buflen, code_yaffs_onewrite, size);
		memcpy((g_BigBUF + g_BigSize), (char *)(packet->packet_body.content), size);
		g_BigSize += size;

		if (g_BigSize < code_yaffs_buflen) {
			//printf("continue to big buffer\n");
			g_prevstatus = NAND_SUCCESS;
		} else {
			//printf("big buffer is full. g_BigSize = %d\n", g_BigSize);
			for (ii = 0; ii < g_BigSize; ii += code_yaffs_onewrite) {
				//printf(".");
				g_prevstatus = nand_write_fdl( (unsigned int) code_yaffs_onewrite, (unsigned char *) (g_BigBUF + ii));
				if (NAND_SUCCESS != g_prevstatus) {
					//printf("\n");
					printf("big buffer write error.\n");				
					break;
				}
			}
			//printf("\n");
			g_BigSize = 0;
			memset(g_BigBUF, 0xff, YAFFS_BUFFER_SIZE);
		}
#else
        	g_prevstatus = nand_write_fdl( (unsigned int) size, (unsigned char *) (packet->packet_body.content));
#endif
	}

        if (NAND_SUCCESS == g_prevstatus)
        {
            g_status.recv_size += size;

            if (!packet->ack_flag)
            {
                packet->ack_flag = 1;
		set_dl_op_val(0, 0, MIDSTDATA, SUCCESS, 8);
		FDL_SendAckPacket (BSL_REP_ACK);
                return NAND_SUCCESS == g_prevstatus;
            }
        }

	set_dl_op_val(0, 0, MIDSTDATA, FAIL, 4);
        FDL2_SendRep (g_prevstatus);
        return NAND_SUCCESS == g_prevstatus;
    }
    else //It's fixnv data. We should backup it.
    {
        memcpy ( (unsigned char *) g_sram_addr, (char *) (packet->packet_body.content), size);
        g_sram_addr += size;
	g_status.recv_size += size;
	set_dl_op_val(0, 0, MIDSTDATA, SUCCESS, 8);
        FDL_SendAckPacket (BSL_REP_ACK);
        return 1;
    }
}

int FDL2_DataEnd (PACKET_T *packet, void *arg)
{
	unsigned long pos, size, ret;
    	unsigned long i, fix_nv_size, fix_nv_checksum, ii, realii;

	set_dl_op_val(0, 0, ENDDATA, FAIL, 1);
    	if (CHECKSUM_OTHER_DATA != g_checksum) {
		/* It's fixnv data */
        	fix_nv_size = g_sram_addr - (unsigned long) g_fixnv_buf;
        	fix_nv_checksum = Get_CheckSum ( (unsigned char *) g_fixnv_buf, fix_nv_size);
        	fix_nv_checksum = EndianConv_32 (fix_nv_checksum);
        	if (fix_nv_checksum != g_checksum)
            		SEND_ERROR_RSP(BSL_CHECKSUM_DIFF);
		
		//////////////////////////////
		/* write fixnv to yaffs2 format : orginal */
		char *fixnvpoint = "/fixnv";
		char *fixnvfilename = "/fixnv/fixnv.bin";

		/* g_fixnv_buf : (FIXNV_SIZE + 4) instead of fix_nv_size */
		g_fixnv_buf[FIXNV_SIZE + 0] = g_fixnv_buf[FIXNV_SIZE + 1] = 0x5a;
		g_fixnv_buf[FIXNV_SIZE + 2] = g_fixnv_buf[FIXNV_SIZE + 3] = 0x5a;
		cmd_yaffs_mount(fixnvpoint);
    		cmd_yaffs_mwrite_file(fixnvfilename, (char *)g_fixnv_buf, (FIXNV_SIZE + 4));
		ret = cmd_yaffs_ls_chk(fixnvfilename);
		cmd_yaffs_umount(fixnvpoint);

		/* write fixnv to yaffs2 format : backup */
		char *backupfixnvpoint = "/backupfixnv";
		char *backupfixnvfilename = "/backupfixnv/fixnv.bin";

		/* g_fixnv_buf : (FIXNV_SIZE + 4) instead of fix_nv_size */
		g_fixnv_buf[FIXNV_SIZE + 0] = g_fixnv_buf[FIXNV_SIZE + 1] = 0x5a;
		g_fixnv_buf[FIXNV_SIZE + 2] = g_fixnv_buf[FIXNV_SIZE + 3] = 0x5a;
		cmd_yaffs_mount(backupfixnvpoint);
    		cmd_yaffs_mwrite_file(backupfixnvfilename, (char *)g_fixnv_buf, (FIXNV_SIZE + 4));
		ret = cmd_yaffs_ls_chk(backupfixnvfilename);
		cmd_yaffs_umount(backupfixnvpoint);
		g_prevstatus = NAND_SUCCESS;
		//////////////////////////////
    	} else if (is_nbl_write == 1) {
#ifdef CONFIG_NAND_SC8810	//only for sc8810 to write spl
		nand_write_fdl(0x0, g_FixNBLBuf);
#else
	   	/* write the spl loader image to the nand*/
		for (i = 0; i < 3; i++) {
			pos = 0;
			while (pos < g_NBLFixBufDataSize) {
				if ((g_NBLFixBufDataSize - pos) >= 2048)
					size = 2048;
				else
					size = g_NBLFixBufDataSize - pos;
				//printf("pos = %d  size = %d\n", pos, size);
				if (size == 0)
					break;
#if 1
				g_prevstatus = nand_write_fdl (size, g_FixNBLBuf + pos);
#else
				if (nand_write_fdl (size, g_FixNBLBuf + pos) == NAND_SUCCESS)
                			g_prevstatus = NAND_SUCCESS;
#endif
				pos += size;
			}

        	}//for (i = 0; i < 3; i++)
#endif
		is_nbl_write = 0;
   	} else if (is_phasecheck_write == 1) {
		/* write phasecheck to yaffs2 format */
		char *productinfopoint = "/productinfo";
		char *productinfofilename = "/productinfo/productinfo.bin";

		/* g_PhasecheckBUF : (PHASECHECK_SIZE + 4) instead of g_PhasecheckBUFDataSize */
		g_PhasecheckBUF[PHASECHECK_SIZE + 0] = g_PhasecheckBUF[PHASECHECK_SIZE + 1] = 0x5a;
		g_PhasecheckBUF[PHASECHECK_SIZE + 2] = g_PhasecheckBUF[PHASECHECK_SIZE + 3] = 0x5a;
		cmd_yaffs_mount(productinfopoint);
    		cmd_yaffs_mwrite_file(productinfofilename, g_PhasecheckBUF, (PHASECHECK_SIZE + 4));
		ret = cmd_yaffs_ls_chk(productinfofilename);
		cmd_yaffs_umount(productinfopoint);
		g_prevstatus = NAND_SUCCESS;
		/* factorydownload tools */
		is_factorydownload_tools = 1;
    	}
#ifdef	TRANS_CODE_SIZE
	else {
		//printf("data end, g_BigSize = %d\n", g_BigSize);
		ii = 0;
		while (ii < g_BigSize) {
			realii = min(g_BigSize - ii, code_yaffs_onewrite);
			//printf(".");
			g_prevstatus = nand_write_fdl( (unsigned int) realii, (unsigned char *) (g_BigBUF + ii));
			if (NAND_SUCCESS != g_prevstatus) {
				//printf("\n");
				printf("big buffer write error.\n");				
				break;
			}
			ii += realii;
		}
		//printf("\n");
		g_BigSize = 0;
	}
#endif

    	if (NAND_SUCCESS != g_prevstatus) {
		set_dl_op_val(0, 0, ENDDATA, FAIL, 2);
        	FDL2_SendRep (g_prevstatus);
        	return 0;
    	}

    	g_prevstatus = nand_end_write();
	set_dl_op_val(0, 0, ENDDATA, SUCCESS, 1);
    	FDL2_SendRep (g_prevstatus);
    	return (NAND_SUCCESS == g_prevstatus);
}

int FDL2_ReadFlash (PACKET_T *packet, void *arg)
{
    	unsigned long *data = (unsigned long *) (packet->packet_body.content);
    	unsigned long addr = *data;
    	unsigned long size = * (data + 1);
    	unsigned long off = 0;
    	int           ret;

#if defined(CHIP_ENDIAN_LITTLE)
    	addr = EndianConv_32 (addr);
    	size = EndianConv_32 (size);
#endif
	memset(&phy_partition, 0, sizeof(struct real_mtd_partition));
	phy_partition.offset = custom2log(addr);
	ret = log2phy_table(&phy_partition);
	phy_partition_info(phy_partition);
    
	if (size > MAX_PKT_SIZE) {
        	FDL_SendAckPacket (BSL_REP_DOWN_SIZE_ERROR);
        	return 0;
    	}

    	if (packet->packet_body.size > 8)
        	off = EndianConv_32 (* (data + 2));
	//printf("addr = 0x%08x  size = 0x%08x  off = 0x%08x  name = %s\n", addr, size, off, phy_partition.name);

	if ((strcmp(phy_partition.name, "fixnv") == 0) || (strcmp(phy_partition.name, "productinfo") == 0))
		ret = nand_read_fdl_yaffs(&phy_partition, off, size, (unsigned char *)(packet->packet_body.content));
    	else if ((strcmp(phy_partition.name, "spl") == 0) || (strcmp(phy_partition.name, "2ndbl") == 0))
    		ret = nand_read_fdl(&phy_partition, off, size, (unsigned char *)(packet->packet_body.content));
    	else
		ret = NAND_INVALID_ADDR;

    	if (NAND_SUCCESS == ret) {
        	packet->packet_body.type = BSL_REP_READ_FLASH;
        	packet->packet_body.size = size;
        	FDL_SendPacket (packet);
        	return 1;
    	} else {
        	FDL2_SendRep (ret);
        	return 0;
    	}
}

int FDL2_EraseFlash (PACKET_T *packet, void *arg)
{
    unsigned long *data = (unsigned long *) (packet->packet_body.content);
    unsigned long addr = *data;
    unsigned long size = * (data + 1);
    int           ret;
    int           cnt;
    int           dl_op_buf_len = 0;
    int 	  dl_item_cnt;
    DL_OP_STATUS_E	dl_data_status, dl_erase_status;
	
    addr = EndianConv_32 (addr);
    size = EndianConv_32 (size);
	
	set_dl_op_val(addr, size, ERASEFLASH, FAIL, 1);
	if ((addr == 0) && (size = 0xffffffff)) {
		printf("Scrub to erase all of flash\n");
		nand_erase_allflash();
		ret = NAND_SUCCESS;
		set_dl_op_val(addr, size, ERASEFLASH, SUCCESS, 1);
	} else {
		memset(&phy_partition, 0, sizeof(struct real_mtd_partition));
		phy_partition.offset = custom2log(addr);
		ret = log2phy_table(&phy_partition);
		phy_partition_info(phy_partition);
		if (NAND_SUCCESS == ret)
			ret = nand_erase_partition(phy_partition.offset, phy_partition.size);

		if (NAND_SUCCESS == ret)
			set_dl_op_val(addr, size, ERASEFLASH, SUCCESS, 1);	
	}
	
	if ((is_factorydownload_tools == 1) && (addr == FactoryDownloadTool_EndPartition_Address)) {
		printf("\nSave dload status into dlstatus.txt\n");
		dl_op_buf_len = DL_OP_RECORD_LEN * (g_dl_op_index + 1);
		if (dl_op_buf_len > 0x2000) {
			printf("dload status is too long and does not save it.\n");
		} else {
			memset(g_PhasecheckBUF, 0, 0x2000);
			for (cnt = 0; cnt <= g_dl_op_index; cnt++)
				sprintf((g_PhasecheckBUF + cnt * DL_OP_RECORD_LEN), 
				"{%02d Base:0x%08x Size:0x%08x Op:%s Status:%s Scnt:0x%08x}", 
				cnt, 
				g_dl_op_table[cnt].base, 
				g_dl_op_table[cnt].size, 
				Dl_Op_Type_Name[g_dl_op_table[cnt].type], 
				Dl_Op_Status_Name[g_dl_op_table[cnt].status], 
				g_dl_op_table[cnt].status_cnt);
			/* printf("%s\n", g_PhasecheckBUF); the line will result in dead here, so mask it */
			/* write dload_status to yaffs2 format */
			char *productinfopoint = "/productinfo";
			char *productinfofilename = "/productinfo/dlstatus.txt";
			cmd_yaffs_mount(productinfopoint);
 			cmd_yaffs_mwrite_file(productinfofilename, g_PhasecheckBUF, dl_op_buf_len);
			cmd_yaffs_ls_chk(productinfofilename);
			cmd_yaffs_umount(productinfopoint);
		}

		/* check factorydownload status */
		printf("\nCheck dload status\n");
		for (cnt = 0; cnt <= g_dl_op_index; cnt++)
				printf("%02d Base:0x%08x Size:0x%08x Op:%d Status:%d Scnt:0x%08x\n", 
				cnt, 
				g_dl_op_table[cnt].base, 
				g_dl_op_table[cnt].size, 
				g_dl_op_table[cnt].type, 
				g_dl_op_table[cnt].status, 
				g_dl_op_table[cnt].status_cnt);
		
		dl_data_status = FAIL; 
		dl_erase_status = FAIL;
		dl_item_cnt = sizeof(Dl_Data_Address) / sizeof(unsigned long);
		for (cnt = 0; cnt < dl_item_cnt; cnt++) {
			dl_data_status = check_dl_data_status(Dl_Data_Address[cnt]);
			if (dl_data_status == FAIL) {
				printf("check address:0x%08x download status error\n", Dl_Data_Address[cnt]);
				break;
			}
		}

		dl_item_cnt = sizeof(Dl_Erase_Address) / sizeof(unsigned long);
		for (cnt = 0; cnt < dl_item_cnt; cnt++) {
			dl_erase_status = check_dl_erase_status(Dl_Erase_Address[cnt]);
			if (dl_erase_status == FAIL) {
			   	printf("check address:0x%08x erase status error\n", Dl_Erase_Address[cnt]);
				break;
			}
		}
		
		if ((dl_data_status == SUCCESS) && (dl_erase_status == SUCCESS))
			ret = NAND_SUCCESS;
		else
			ret = NAND_SYSTEM_ERROR;
	}

    FDL2_SendRep (ret);
    return (NAND_SUCCESS == ret);
}

int FDL2_FormatFlash (PACKET_T *pakcet, void *arg)
{
    int ret = nand_format();
    FDL2_SendRep (ret);
    return (NAND_SUCCESS == ret);
}