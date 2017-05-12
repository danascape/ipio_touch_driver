#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/fd.h>
#include <linux/file.h>
#include <linux/version.h>
#include <asm/uaccess.h>


#include "../chip.h"
#include "config.h"
#include "i2c.h"
#include "firmware.h"

CORE_FIRMWARE *core_firmware;
extern CORE_CONFIG *core_config;
extern uint32_t SUP_CHIP_LIST[SUPP_CHIP_NUM];

uint8_t fwdata_buffer[ILITEK_ILI21XX_FIRMWARE_SIZE * 1024] = {0};
uint8_t fwdata[ILITEK_MAX_UPDATE_FIRMWARE_BUFFER_SIZE * 1024] = {0};

static uint32_t HexToDec(char *pHex, int32_t nLength)
{
    uint32_t nRetVal = 0, nTemp = 0, i;
    int32_t nShift = (nLength - 1) * 4;

    for (i = 0; i < nLength; nShift -= 4, i ++)
    {
        if ((pHex[i] >= '0') && (pHex[i] <= '9'))
        {
            nTemp = pHex[i] - '0';
        }
        else if ((pHex[i] >= 'a') && (pHex[i] <= 'f'))
        {
            nTemp = (pHex[i] - 'a') + 10;
        }
        else if ((pHex[i] >= 'A') && (pHex[i] <= 'F'))
        {
            nTemp = (pHex[i] - 'A') + 10;
        }
        else
        {
            return -1;
        }
        
        nRetVal |= (nTemp << nShift);
    }
    
    return nRetVal;
}

static int CheckSum(uint32_t nStartAddr, uint32_t nEndAddr)
{
	u16 i = 0, nInTimeCount = 100;
	u8 szBuf[64] = {0};

	core_config_ice_mode_write(0x4100B, 0x23, 1);
	core_config_ice_mode_write(0x41009, nEndAddr, 2);
	core_config_ice_mode_write(0x41000, 0x3B | (nStartAddr << 8), 4);
	core_config_ice_mode_write(0x041004, 0x66AA5500, 4);

	for (i = 0; i < nInTimeCount; i++)
	{
		szBuf[0] = core_config_read_write_onebyte(0x41011);

		if ((szBuf[0] & 0x01) == 0)
		{
			break;
		}
		mdelay(100);
	} 		

	return core_config_ice_mode_read(0x41018);
}

static int32_t convert_firmware(uint8_t *pBuf, uint32_t nSize)
{
    uint32_t i = 0, j = 0, k = 0;
    uint32_t nApStartAddr = 0xFFFF, nDfStartAddr = 0xFFFF, nStartAddr = 0xFFFF, nExAddr = 0;
    uint32_t nApEndAddr = 0x0, nDfEndAddr = 0x0, nEndAddr = 0x0;
    uint32_t nApChecksum = 0x0, nDfChecksum = 0x0, nChecksum = 0x0, nLength = 0, nAddr = 0, nType = 0;

	DBG_INFO("size = %d", nSize);

	core_firmware->ap_start_addr = 0;
	core_firmware->ap_end_addr = 0;
	core_firmware->ap_checksum = 0;

	if(nSize != 0)
	{
		memset(fwdata, 0xFF, ILITEK_MAX_UPDATE_FIRMWARE_BUFFER_SIZE*1024);

		for(; i < nSize ; )
		{
			int32_t nOffset;

			nLength = HexToDec(&pBuf[i + 1], 2);
			nAddr = HexToDec(&pBuf[i + 3], 4);
			nType = HexToDec(&pBuf[i + 7], 2);

			// calculate checksum
			for (j = 8; j < (2 + 4 + 2 + (nLength * 2)); j += 2)
			{
				if (nType == 0x00)
				{
					// for ice mode write method
					nChecksum = nChecksum + HexToDec(&pBuf[i + 1 + j], 2);

					if (nAddr + (j - 8) / 2 < nDfStartAddr)
					{
						nApChecksum = nApChecksum + HexToDec(&pBuf[i + 1 + j], 2);
					}
					else
					{
						nDfChecksum = nDfChecksum + HexToDec(&pBuf[i + 1 + j], 2);
					}		
				}
			}
			if (nType == 0x04)
			{
				nExAddr = HexToDec(&pBuf[i + 9], 4);
			}

			nAddr = nAddr + (nExAddr << 16);

			if (pBuf[i+1+j+2] == 0x0D)
			{
				nOffset = 2;
			}
			else
			{
				nOffset = 1;
			}	

			if (nType == 0x00)
			{
				if (nAddr > (ILITEK_MAX_UPDATE_FIRMWARE_BUFFER_SIZE*1024))
				{
					DBG_ERR("Invalid hex format");

					return -EINVAL;
				}

				if (nAddr < nStartAddr)
				{
					nStartAddr = nAddr;
				}
				if ((nAddr + nLength) > nEndAddr)
				{
					nEndAddr = nAddr + nLength;
				}

				// for Bl protocol 1.4+, nApStartAddr and nApEndAddr
				if (nAddr < nApStartAddr)
				{
					nApStartAddr = nAddr;
				}

				if ((nAddr + nLength) > nApEndAddr && (nAddr < nDfStartAddr))
				{
					nApEndAddr = nAddr + nLength - 1;

					if (nApEndAddr > nDfStartAddr)
					{
						nApEndAddr = nDfStartAddr - 1;
					}
				}

				// for Bl protocol 1.4+, bl_end_addr
				if ((nAddr + nLength) > nDfEndAddr && (nAddr >= nDfStartAddr))
				{
					nDfEndAddr = nAddr + nLength;
				}

				// fill data
				for (j = 0, k = 0; j < (nLength * 2); j += 2, k ++)
				{
					fwdata[nAddr + k] = HexToDec(&pBuf[i + 9 + j], 2);
					//*(core_firmware->fw_data_max_buff+nAddr+k) = HexToDec(&pBuf[i + 9 + j], 2);
				}
			}

			i += 1 + 2 + 4 + 2 + (nLength * 2) + 2 + nOffset;        
		}


		core_firmware->ap_start_addr = nApStartAddr;
		core_firmware->ap_end_addr = nApEndAddr;
		core_firmware->ap_checksum = nApChecksum;
		core_firmware->df_start_addr = nDfStartAddr;
		core_firmware->df_end_addr = nDfEndAddr;
		core_firmware->df_checksum = nDfChecksum;


		DBG_INFO("nStartAddr = 0x%06X, nEndAddr = 0x%06X, nChecksum = 0x%06X",
				nStartAddr, nEndAddr, nChecksum);
		DBG_INFO("nApStartAddr = 0x%06X, nApEndAddr = 0x%06X, nApChecksum = 0x%06X",
				nApStartAddr, nApEndAddr, nApChecksum);
		DBG_INFO("nDfStartAddr = 0x%06X, nDfEndAddr = 0x%06X, nDfChecksum = 0x%06X",
				nDfStartAddr, nDfEndAddr, nDfChecksum);

		return 0;
	}

	return -1;
}

static int firmware_upgrade_ili7807(uint8_t *pszFwData)
{
	DBG_INFO();

	return 0;
}

static int firmware_upgrade_ili2121(uint8_t *pszFwData)
{
    int32_t nUpdateRetryCount = 0, nUpgradeStatus = 0, nUpdateLength = 0;
	int32_t	nCheckFwFlag = 0, nChecksum = 0, i = 0, j = 0, k = 0;
    uint8_t szFwVersion[4] = {0};
    uint8_t szBuf[512] = {0};
	uint8_t szCmd[2] = {0};
    uint32_t nApStartAddr = 0, nDfStartAddr = 0, nApEndAddr = 0, nDfEndAddr = 0;
	uint32_t nApChecksum = 0, nDfChecksum = 0, nTemp = 0, nIcChecksum = 0;
	int res = 0;

	nApStartAddr = core_firmware->ap_start_addr;
	nApEndAddr = core_firmware->ap_end_addr;
	nUpdateLength = core_firmware->ap_end_addr;
	nApChecksum = core_firmware->ap_checksum;

	DBG_INFO("AP_Start_Addr = 0x%06X, AP_End_Addr = 0x%06X, AP_checksum = 0x%06X",
			core_firmware->ap_start_addr, 
			core_firmware->ap_end_addr,
			core_firmware->ap_checksum);

	DBG_INFO("Enter to ICE Mode before updating firmware ... ");

	res = core_config_ice_mode();

	core_config->IceModeInit();

    mdelay(5);

    for (i = 0; i <= 0xd000; i += 0x1000)
    {
		res = core_config_ice_mode_write(0x041000, 0x06, 1); 
		if(res < 0)
			return res;

        mdelay(3);

		res = core_config_ice_mode_write(0x041004, 0x66aa5500, 4); 
		if(res < 0)
			return res;

        mdelay(3);
        
        nTemp = (i << 8) + 0x20;

		res = core_config_ice_mode_write(0x041000, nTemp, 4); 
		if(res < 0)
			return res;

        mdelay(3);
        
		res = core_config_ice_mode_write(0x041004, 0x66aa5500, 4); 
		if(res < 0)
			return res;

        mdelay(20);
        
        for (j = 0; j < 50; j ++)
        {
			res = core_config_ice_mode_write(0x041000, 0x05, 1); 
			if(res < 0)
				return res;

			res = core_config_ice_mode_write(0x041004, 0x66aa5500, 4); 
			if(res < 0)
				return res;

            mdelay(1);
            
            szBuf[0] = core_config_ice_mode_read(0x041013);
            if (szBuf[0] == 0)
                break;
            else
                mdelay(2);
        }
    }

    mdelay(100);		

	DBG_INFO("Start to upgrade firmware from 0x%x to 0x%x in each size of %d",
			nApStartAddr, nApEndAddr, ILITEK_UPDATE_FIRMWARE_PAGE_LENGTH);

    for (i = nApStartAddr; i < nApEndAddr; i += ILITEK_UPDATE_FIRMWARE_PAGE_LENGTH)
    {
		res = core_config_ice_mode_write(0x041000, 0x06, 1); 
		if(res < 0)
			return res;

		res = core_config_ice_mode_write(0x041004, 0x66aa5500, 4); 
		if(res < 0)
			return res;

        nTemp = (i << 8) + 0x02;

		res = core_config_ice_mode_write(0x041000, nTemp, 4); 
		if(res < 0)
			return res;

        res = core_config_ice_mode_write(0x041004, 0x66aa5500 + ILITEK_UPDATE_FIRMWARE_PAGE_LENGTH - 1, 4);
		if(res < 0)
			return res;

        szBuf[0] = 0x25;
        szBuf[3] = (char)((0x041020 & 0x00FF0000) >> 16);
        szBuf[2] = (char)((0x041020 & 0x0000FF00) >> 8);
        szBuf[1] = (char)((0x041020 & 0x000000FF));
        
        for (k = 0; k < ILITEK_UPDATE_FIRMWARE_PAGE_LENGTH; k ++)
        {
            szBuf[4 + k] = pszFwData[i + k];
        }

        if (core_i2c_write(core_config->slave_i2c_addr, szBuf, ILITEK_UPDATE_FIRMWARE_PAGE_LENGTH + 4) < 0) {
            DBG_INFO("Failed to write data via i2c, address = 0x%X, start_addr = 0x%X, end_addr = 0x%X", 
					(int)i, (int)nApStartAddr, (int)nApEndAddr);
			res = -EIO;
            return res;
        }

        nUpgradeStatus = (i * 100) / nUpdateLength;
        printk("%cupgrade firmware(ap code), %02d%c", 0x0D, nUpgradeStatus, '%');

        mdelay(3);
    }

	nIcChecksum = 0;
    nIcChecksum = CheckSum(nApStartAddr, nApEndAddr);

    DBG_INFO("nIcChecksum = 0x%X, nApChecksum = 0x%X\n", nIcChecksum, nApChecksum);

	if (nIcChecksum != nApChecksum)
	{
		//TODO: may add a retry func as protection.
		
		core_config_ice_mode_exit();
		DBG_INFO("Both checksum didn't match");
		res = -1;
		return res;
	}

	core_config_ice_mode_exit();

    szCmd[0] = ILITEK_TP_CMD_READ_DATA;
    res = core_i2c_write(core_config->slave_i2c_addr, &szCmd[0], 1);
    if (res < 0)
    {
        DBG_ERR("ILITEK_TP_CMD_READ_DATA failed, res = %d", res);
		return res;
    }

    mdelay(10);

    res = core_i2c_read(core_config->slave_i2c_addr, &szBuf[0], 3);
    if (res < 0)
    {
        DBG_ERR("ILITEK_TP_CMD_READ_DATA failed, res = %d\n", res);
		return res;
    }

    DBG_INFO("szBuf[0][1][2] = 0x%X, 0x%X, 0x%X", 
						szBuf[0], szBuf[1], szBuf[2]);

    if (szBuf[1] < 0x80)
    {
		//TODO: may add a retry fun as protection.
		
        DBG_ERR("Upgrade FW Failed");
		res = -1;
        return res;
    }

    DBG_INFO("Upgrade FW Success");
	mdelay(100);

	return res;
}

int core_firmware_upgrade(const char *pFilePath)
{
	int res = 0;
    struct file *pfile = NULL;
    struct inode *inode;
    s32 fsize = 0;
    mm_segment_t old_fs;
    loff_t pos = 0;

	DBG_INFO("file path = %s", pFilePath);

	core_firmware->isUpgraded = false;

//	core_firmware->old_fw_ver = core_config_GetFWVer();

	DBG_INFO("Firmware Version = %d.%d.%d.%d", 
			core_config->firmware_ver[0], 
			core_config->firmware_ver[1], 
			core_config->firmware_ver[2],
			core_config->firmware_ver[3]);

    pfile = filp_open(pFilePath, O_RDONLY, 0);
    if (IS_ERR(pfile))
    {
        DBG_ERR("Error occurred while opening file %s.", pFilePath);
		res = -ENOENT;
    }
	else
	{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3, 18, 0)
		inode = pfile->f_dentry->d_inode;
#else
		inode = pfile->f_path.dentry->d_inode;
#endif

		fsize = inode->i_size;

		DBG_INFO("fsize = %d", fsize);

		if (fsize <= 0)
		{
			DBG_ERR("The size of file is zero");
			res = -1;
		}
		else
		{

			memset(fwdata_buffer, 0, ILITEK_ILI21XX_FIRMWARE_SIZE*1024);

			// store current userspace mem segment.
			old_fs = get_fs();

			// set userspace mem segment equal to kernel's one.
			set_fs(KERNEL_DS);

			// read firmware data from userspace mem segment
			vfs_read(pfile, fwdata_buffer, fsize, &pos);

			// restore userspace mem segment after read.
			set_fs(old_fs);

			res == convert_firmware(fwdata_buffer, fsize);
			if( res < 0)
			{
				DBG_ERR("Failed to covert firmware data, res = %d", res);
				return res;
			}
			else
			{
				res = core_firmware->upgrade_func(fwdata);
				if(res < 0)
				{
					DBG_ERR("Failed to upgrade firmware, res = %d", res);
					return res;
				}

				core_firmware->isUpgraded = true;
			}
		}
	}

	// update firmware version if upgraded
	if(core_firmware->isUpgraded)
	{
		core_config_get_fw_ver();
	}

	filp_close(pfile, NULL);
	return res;
}


int core_firmware_init(uint32_t id)
{
	int i = 0;	

	DBG_INFO();

	for(; i < SUPP_CHIP_NUM; i++)
	{
		if(SUP_CHIP_LIST[i] == id)
		{
			core_firmware = (CORE_FIRMWARE*)kmalloc(sizeof(*core_firmware), GFP_KERNEL);

			if(SUP_CHIP_LIST[i] == CHIP_TYPE_ILI2121)
			{
				core_firmware->chip_id			= id;

				core_firmware->new_fw_ver		= 0x0;
				core_firmware->old_fw_ver		= 0x0;

				core_firmware->ap_start_addr	= 0x0;
				core_firmware->ap_end_addr		= 0x0;
				core_firmware->df_start_addr	= 0x0;
				core_firmware->df_end_addr		= 0x0;
				core_firmware->ap_checksum		= 0x0;
				core_firmware->ap_crc			= 0x0;
				core_firmware->df_checksum		= 0x0;
				core_firmware->df_crc			= 0x0;

				core_firmware->isUpgraded		= false;
				core_firmware->isCRC			= false;
				core_firmware->upgrade_func		= firmware_upgrade_ili2121;
			}

			if(SUP_CHIP_LIST[i] == CHIP_TYPE_ILI7807)
			{
				core_firmware->chip_id			= id;

				core_firmware->new_fw_ver		= 0x0;
				core_firmware->old_fw_ver		= 0x0;

				core_firmware->ap_start_addr	= 0x0;
				core_firmware->ap_end_addr		= 0x0;
				core_firmware->df_start_addr	= 0x0;
				core_firmware->df_end_addr		= 0x0;
				core_firmware->ap_checksum		= 0x0;
				core_firmware->ap_crc			= 0x0;
				core_firmware->df_checksum		= 0x0;
				core_firmware->df_crc			= 0x0;

				core_firmware->isUpgraded		= false;
				core_firmware->isCRC			= false;
				core_firmware->upgrade_func		= firmware_upgrade_ili7807;
			}
		}
	}

	if(core_firmware == NULL) 
	{
		DBG_ERR("Can't find an id from the support list, init core_firmware failed");
		return -EINVAL;
	}

	return 0;
}

void core_firmware_remove(void)
{
	DBG_INFO();

	kfree(core_firmware);
}
