#include <asm/arch/sci_types.h>
#include <asm/arch/sc_reg.h>
#include <boot_mode.h>
#include <asm/arch/sprd_reg.h>
#include <asm/arch/sprd_eic.h>
#include <asm/arch/rtc_reg_v3.h>
#include <asm/arch/regs_adi.h>
#include <asm/arch/adi_hal_internal.h>

#define   HWRST_STATUS_POWERON_MASK 		(0xf0)
#define   HWRST_STATUS_RECOVERY 		(0x20)
#define   HWRST_STATUS_FASTBOOT 		(0X30)
#define   HWRST_STATUS_NORMAL 			(0X40)
#define   HWRST_STATUS_ALARM 			(0X50)
#define   HWRST_STATUS_SLEEP 			(0X60)
#define   HWRST_STATUS_SPECIAL			(0x70)
#define   HWRST_STATUS_CALIBRATION			(0x90)
#define   HWRST_STATUS_PANIC			(0x80)
#define   HWRST_STATUS_NORMAL2			(0Xf0)

unsigned check_reboot_mode(void)
{
	unsigned rst_mode= 0;

	rst_mode = ANA_REG_GET(ANA_REG_GLB_POR_RST_MONITOR);
	rst_mode &= 0x7FFF;
	ANA_REG_SET(ANA_REG_GLB_POR_RST_MONITOR, 0); //clear flag

	printf("rst_mode==%x\n",rst_mode);
	if(rst_mode == HWRST_STATUS_RECOVERY)
		return RECOVERY_MODE;
	else if(rst_mode == HWRST_STATUS_FASTBOOT)
		return FASTBOOT_MODE;
	else if(rst_mode == HWRST_STATUS_NORMAL)
		return NORMAL_MODE;
	else if(rst_mode == HWRST_STATUS_NORMAL2)
		return WATCHDOG_REBOOT;
	else if(rst_mode == HWRST_STATUS_ALARM)
		return ALARM_MODE;
	else if(rst_mode == HWRST_STATUS_SLEEP)
		return SLEEP_MODE;
	else if(rst_mode == HWRST_STATUS_CALIBRATION)
		return CALIBRATION_MODE;
	else if(rst_mode == HWRST_STATUS_PANIC)
		return PANIC_REBOOT;
	else if(rst_mode == HWRST_STATUS_SPECIAL)
		return SPECIAL_MODE;
	else{
		printf(" a boot mode not supported\n");
		return 0;
	}
}

void reboot_devices(unsigned reboot_mode)
{
	unsigned rst_mode = 0;
	if(reboot_mode == RECOVERY_MODE){
		rst_mode = HWRST_STATUS_RECOVERY;
	}
	else if(reboot_mode == FASTBOOT_MODE){
		rst_mode = HWRST_STATUS_FASTBOOT;
	}else if(reboot_mode == NORMAL_MODE){
		rst_mode = HWRST_STATUS_NORMAL;
	}else{
		rst_mode = 0;
	}

	ANA_REG_SET(ANA_REG_GLB_POR_RST_MONITOR, rst_mode);
	reset_cpu(0);
}
void power_down_devices(unsigned pd_cmd)
{
	power_down_cpu(0);
}

int power_button_pressed(void)
{
	//sprd_eic_request(EIC_PBINT);
	//udelay(3000);
	//printf("eica status %x\n", sprd_eic_get(EIC_PBINT));
	//return !sprd_eic_get(EIC_PBINT);
	return 0;
}

int charger_connected(void)
{
	sprd_eic_request(EIC_CHG_INT);
	udelay(3000);
	printf("eica status %x\n", sprd_eic_get(EIC_CHG_INT));
	return !!sprd_eic_get(EIC_CHG_INT);
}

int alarm_triggered(void)
{
	//printf("ANA_RTC_INT_RSTS is 0x%x\n", ANA_RTC_INT_RSTS);
	printf("value of it 0x%x\n", ANA_REG_GET(ANA_RTC_INT_RSTS));
	return ANA_REG_GET(ANA_RTC_INT_RSTS) & BIT_4;
}

