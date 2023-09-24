// SPDX-License-Identifier: GPL-2.0-or-later

/**************************************************************************
*   Copyright (C) 2015 Jeff Ciesielski <jeffciesielski@gmail.com>         *
***************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "imp.h"
#include <helper/binarybuffer.h>
#include <target/algorithm.h>
#include <target/armv7m.h>
#include "xmc4xxx.h"

struct xmc4xxx_flash_bank {
	bool probed;

	/* We need the flash controller ID to choose the sector layout */
	uint32_t fcon_id;

	/* Passwords used for protection operations */
	uint32_t pw1;
	uint32_t pw2;
	bool pw_set;

	/* Protection flags */
	bool read_protected;

	bool write_prot_otp[MAX_XMC_SECTORS];
};

struct xmc4xxx_command_seq {
	uint32_t address;
	uint32_t magic;
};

/* Sector capacities.  See section 8 of xmc4x00_rm */
static const unsigned int sector_capacity_8[8] = {
	16, 16, 16, 16, 16, 16, 16, 128
};

static const unsigned int sector_capacity_9[9] = {
	16, 16, 16, 16, 16, 16, 16, 128, 256
};

static const unsigned int sector_capacity_12[12] = {
	16, 16, 16, 16, 16, 16, 16, 16, 128, 256, 256, 256
};

static const unsigned int sector_capacity_16[16] = {
	16, 16, 16, 16, 16, 16, 16, 16, 128, 256, 256, 256, 256, 256, 256, 256
};

static int xmc4xxx_get_sector_start_addr(struct flash_bank *bank,
	unsigned int sector, uint32_t *ret_addr)
{
	/* Make sure we understand this sector */
	if (sector > bank->num_sectors)
		return ERROR_FAIL;

	*ret_addr = bank->base + bank->sectors[sector].offset;
	return ERROR_OK;

}

static int xmc4xxx_clear_flash_status(struct flash_bank *bank)
{
	/* TODO: Do we need to check for sequence error? */
	LOG_INFO("Clearing flash status");
	int res = target_write_u32(bank->target, FLASH_CMD_CLEAR_STATUS, 0xf5);
	if (res != ERROR_OK) {
		LOG_ERROR("Unable to write erase command sequence");
		return res;
	}
	return ERROR_OK;
}

static int xmc4xxx_get_flash_status(struct flash_bank *bank, uint32_t *status)
{
	int res = target_read_u32(bank->target, FLASH_REG_FLASH0_FSR, status);
	if (res != ERROR_OK)
		LOG_ERROR("Cannot read flash status register.");
	return res;
}

static int xmc4xxx_write_command_sequence(struct flash_bank *bank,
	struct xmc4xxx_command_seq *seq, int seq_len)
{
	for (int i = 0; i < seq_len; i++) {
		int res = target_write_u32(bank->target, seq[i].address, seq[i].magic);
		if (res != ERROR_OK)
			return res;
	}
	return ERROR_OK;
}

static int xmc4xxx_wait_status_busy(struct flash_bank *bank, int timeout)
{
	uint32_t status;
	int res = xmc4xxx_get_flash_status(bank, &status);
	if (res != ERROR_OK)
		return res;

	/* While the flash controller is busy, wait */
	while (status & FSR_PBUSY_MASK) {
		res = xmc4xxx_get_flash_status(bank, &status);
		if (res != ERROR_OK)
			return res;

		if (timeout-- <= 0) {
			LOG_ERROR("Timed out waiting for flash");
			return ERROR_FAIL;
		}
		alive_sleep(1);
		keep_alive();
	}

	if (status & FSR_PROER_MASK) {
		LOG_ERROR("XMC4xxx flash protected");
		res = ERROR_FAIL;
	}
	return res;
}

static int xmc4xxx_load_bank_layout(struct flash_bank *bank)
{
	/* At this point, we know which flash controller ID we're
	 * talking to and simply need to fill out the bank structure accordingly */
	LOG_DEBUG("%u sectors", bank->num_sectors);

	const unsigned int *capacity;
	switch (bank->num_sectors) {
	case 8:
		capacity = sector_capacity_8;
		break;
	case 9:
		capacity = sector_capacity_9;
		break;
	case 12:
		capacity = sector_capacity_12;
		break;
	case 16:
		capacity = sector_capacity_16;
		break;
	default:
		LOG_ERROR("Unexpected number of sectors, %u\n", bank->num_sectors);
		return ERROR_FAIL;
	}

	/* This looks like a bank that we understand, now we know the
	 * corresponding sector capacities and we can add those up into the
	 * bank size. */
	uint32_t total_offset = 0;
	bank->sectors = calloc(bank->num_sectors, sizeof(struct flash_sector));
	for (unsigned int i = 0; i < bank->num_sectors; i++) {
		bank->sectors[i].size = capacity[i] * 1024;
		bank->sectors[i].offset = total_offset;
		bank->sectors[i].is_erased = -1;
		bank->sectors[i].is_protected = -1;

		bank->size += bank->sectors[i].size;
		LOG_DEBUG("\t%d: %uk", i, capacity[i]);
		total_offset += bank->sectors[i].size;
	}

	/* This part doesn't follow the typical standard of 0xff
	 * being the erased value.*/
	bank->default_padded_value = bank->erased_value = 0x00;
	return ERROR_OK;
}

static int xmc4xxx_probe(struct flash_bank *bank)
{
	struct xmc4xxx_flash_bank *fb = bank->driver_priv;

	if (fb->probed)
		return ERROR_OK;

	/* It's not possible for the DAP to access the OTP locations needed for
	 * probing the part info and Flash geometry so we require that the target
	 * be halted before proceeding. */
	if (bank->target->state != TARGET_HALTED) {
		LOG_WARNING("Cannot communicate... target not halted.");
		return ERROR_TARGET_NOT_HALTED;
	}

	uint32_t devid;
	/* The SCU registers contain the ID of the chip */
	int res = target_read_u32(bank->target, SCU_REG_BASE + SCU_ID_CHIP, &devid);
	if (res != ERROR_OK) {
		LOG_ERROR("Cannot read device identification register.");
		return res;
	}

	/* Make sure this is a XMC4000 family device */
	if ((devid & 0xF0000) != 0x40000 && devid != 0) {
		LOG_ERROR("Platform ID doesn't match XMC4xxx: 0x%08" PRIx32, devid);
		return ERROR_FAIL;
	}

	LOG_DEBUG("Found XMC4xxx with devid: 0x%08" PRIx32, devid);

	uint32_t config;
	/* Now sanity-check the Flash controller itself. */
	res = target_read_u32(bank->target, FLASH_REG_FLASH0_ID, &config);
	if (res != ERROR_OK) {
		LOG_ERROR("Cannot read Flash bank configuration.");
		return res;
	}
	uint8_t flash_id = (config & 0xff0000) >> 16;

	/* The Flash configuration register is our only means of
	 * determining the sector layout. We need to make sure that
	 * we understand the type of controller we're dealing with */
	switch (flash_id) {
	case FLASH_ID_XMC4100_4200:
		bank->num_sectors = 8;
		LOG_DEBUG("XMC4xxx: XMC4100/4200 detected.");
		break;
	case FLASH_ID_XMC4400:
		bank->num_sectors = 9;
		LOG_DEBUG("XMC4xxx: XMC4400 detected.");
		break;
	case FLASH_ID_XMC4500:
		bank->num_sectors = 12;
		LOG_DEBUG("XMC4xxx: XMC4500 detected.");
		break;
	case FLASH_ID_XMC4300_XMC4700_4800:
		bank->num_sectors = 16;
		LOG_DEBUG("XMC4xxx: XMC4700/4800 detected.");
		break;
	default:
		LOG_ERROR("XMC4xxx: Unexpected flash ID. got %02" PRIx8, flash_id);
		return ERROR_FAIL;
	}

	/* Retrieve information about the particular bank we're probing and fill in
	 * the bank structure accordingly. */
	res = xmc4xxx_load_bank_layout(bank);
	if (res == ERROR_OK) {
		/* We're done */
		fb->probed = true;
	} else {
		LOG_ERROR("Unable to load bank information.");
		return ERROR_FAIL;
	}

	return ERROR_OK;
}

FLASH_BANK_COMMAND_HANDLER(xmc4xxx_flash_bank_command)
{
	bank->driver_priv = malloc(sizeof(struct xmc4xxx_flash_bank));
	if (!bank->driver_priv)
		return ERROR_FLASH_OPERATION_FAILED;

	memset(bank->driver_priv, 0, sizeof(struct xmc4xxx_flash_bank));
	return ERROR_OK;
}

static int xmc4xxx_get_info_command(struct flash_bank *bank, struct command_invocation *cmd)
{
	uint32_t scu_idcode;
	struct xmc4xxx_flash_bank *fb = bank->driver_priv;

	if (bank->target->state != TARGET_HALTED) {
		LOG_WARNING("Cannot communicate... target not halted.");
		return ERROR_TARGET_NOT_HALTED;
	}

	/* The SCU registers contain the ID of the chip */
	int res = target_read_u32(bank->target, SCU_REG_BASE + SCU_ID_CHIP, &scu_idcode);
	if (res != ERROR_OK) {
		LOG_ERROR("Cannot read device identification register.");
		return res;
	}

	const char *dev_str;
	const char *rev_str = NULL;
	uint16_t rev_id = scu_idcode & 0x000f;
	uint16_t dev_id = (scu_idcode & 0xfff0) >> 4;
	switch (dev_id) {
	case 0x100:
		dev_str = "XMC4100";
		switch (rev_id) {
		case 0x1:
			rev_str = "AA";
			break;
		case 0x2:
			rev_str = "AB";
			break;
		}
		break;
	case 0x200:
		dev_str = "XMC4200";
		switch (rev_id) {
		case 0x1:
			rev_str = "AA";
			break;
		case 0x2:
			rev_str = "AB";
			break;
		}
		break;
	case 0x300:
		dev_str = "XMC4300";
		switch (rev_id) {
		case 0x1:
			rev_str = "AA";
		}
		break;
	case 0x400:
		dev_str = "XMC4400";
		switch (rev_id) {
		case 0x1:
			rev_str = "AA";
			break;
		case 0x2:
			rev_str = "AB";
			break;
		}
		break;
	case 0:
		/* XMC4500 EES AA13 with date codes before GE212
		 * had zero SCU_IDCHIP
		 */
		dev_str = "XMC4500 EES";
		rev_str = "AA13";
		break;
	case 0x500:
		dev_str = "XMC4500";
		switch (rev_id) {
		case 0x2:
			rev_str = "AA";
			break;
		case 0x3:
			rev_str = "AB";
			break;
		case 0x4:
			rev_str = "AC";
			break;
		}
		break;
	case 0x700:
		dev_str = "XMC4700";
		switch (rev_id) {
		case 0x1:
			rev_str = "EES-AA";
			break;
		}
		break;
	case 0x800:
		dev_str = "XMC4800";
		switch (rev_id) {
		case 0x1:
			rev_str = "EES-AA";
			break;
		}
		break;
	default:
		command_print_sameline(cmd, "Cannot identify target as an XMC4xxx. SCU_ID: %"PRIx32 "\n", scu_idcode);
		return ERROR_OK;
	}

	/* String to declare protection data held in the private driver */
	char prot_str[512] = { 0 };
	if (fb->read_protected)
		snprintf(prot_str, sizeof(prot_str), "\nFlash is read protected");

	bool otp_enabled = false;
	for (unsigned int i = 0; i < bank->num_sectors; i++) {
		if (fb->write_prot_otp[i])
			otp_enabled = true;
	}

	/* If OTP Write protection is enabled (User 2), list each
	 * sector that has it enabled */
	char otp_str[14];
	if (otp_enabled) {
		strcat(prot_str, "\nOTP Protection is enabled for sectors:\n");
		for (unsigned int i = 0; i < bank->num_sectors; i++) {
			if (fb->write_prot_otp[i]) {
				snprintf(otp_str, sizeof(otp_str), "- %d\n", i);
				strncat(prot_str, otp_str, sizeof(prot_str) - strlen(prot_str) - 1);
			}
		}
	}

	if (rev_str)
		command_print_sameline(cmd, "%s - Rev: %s%s", dev_str, rev_str, prot_str);
	else
		command_print_sameline(cmd, "%s - Rev: unknown (0x%01x)%s", dev_str, rev_id, prot_str);

	return ERROR_OK;
}

static int xmc4xxx_erase_sector(struct flash_bank *bank, uint32_t address, bool user_config)
{
	/* See reference manual table 8.4: Command Sequences for Flash Control */
	struct xmc4xxx_command_seq erase_cmd_seq[] =
	{
		{ FLASH_CMD_ERASE_1, 0xaa },
		{ FLASH_CMD_ERASE_2, 0x55 },
		{ FLASH_CMD_ERASE_3, 0x80 },
		{ FLASH_CMD_ERASE_4, 0xaa },
		{ FLASH_CMD_ERASE_5, 0x55 },
		{ 0xff,              0xff } /* Needs filled in */
	};

	/* We need to fill in the base address of the sector we'll be
	 * erasing, as well as the magic code that determines whether
	 * this is a standard flash sector or a user configuration block */

	erase_cmd_seq[5].address = address;
	if (user_config) {
		/* Removing flash protection requires the addition of
		 * the base address */
		erase_cmd_seq[5].address += bank->base;
		erase_cmd_seq[5].magic = 0xc0;
	} else {
		erase_cmd_seq[5].magic = 0x30;
	}

	int res = xmc4xxx_write_command_sequence(bank, erase_cmd_seq, 
		ARRAY_SIZE(erase_cmd_seq));
	if (res != ERROR_OK)
		return res;

	uint32_t status;
	/* Read the flash status register */
	res = target_read_u32(bank->target, FLASH_REG_FLASH0_FSR, &status);
	if (res != ERROR_OK) {
		LOG_ERROR("Cannot read flash status register.");
		return res;
	}

	/* Check for a sequence error */
	if (status & FSR_SQER_MASK) {
		LOG_ERROR("Error with flash erase sequence");
		return ERROR_FAIL;
	}

	/* Make sure a flash erase was triggered */
	if (!(status & FSR_ERASE_MASK)) {
		LOG_ERROR("Flash failed to erase");
		return ERROR_FAIL;
	}

	/* Now we must wait for the erase operation to end */
	res = xmc4xxx_wait_status_busy(bank, FLASH_OP_TIMEOUT);
	return res;
}

static int xmc4xxx_flash_unprotect(struct flash_bank *bank, int32_t level)
{
	uint32_t addr;
	switch (level) {
	case 0:
		addr = UCB0_BASE;
		break;
	case 1:
		addr = UCB1_BASE;
		break;
	default:
		LOG_ERROR("Invalid user level. Must be 0-1");
		return ERROR_FAIL;
	}

	int res = xmc4xxx_erase_sector(bank, addr, true);
	if (res != ERROR_OK)
		LOG_ERROR("Error erasing user configuration block");
	return res;
}

static int xmc4xxx_temp_unprotect(struct flash_bank *bank, int user_level)
{
	struct xmc4xxx_flash_bank *fb = bank->driver_priv;

	struct xmc4xxx_command_seq temp_unprot_seq[] = {
		{ FLASH_CMD_TEMP_UNPROT_1, 0xaa },
		{ FLASH_CMD_TEMP_UNPROT_2, 0x55 },
		{ FLASH_CMD_TEMP_UNPROT_3, 0xff }, /* Needs filled in */
		{ FLASH_CMD_TEMP_UNPROT_4, 0xff }, /* Needs filled in */
		{ FLASH_CMD_TEMP_UNPROT_5, 0xff }, /* Needs filled in */
		{ FLASH_CMD_TEMP_UNPROT_6, 0x05 }
	};

	if (user_level < 0 || user_level > 2) {
		LOG_ERROR("Invalid user level, must be 0-2");
		return ERROR_FAIL;
	}

	/* Fill in the user level and passwords */
	temp_unprot_seq[2].magic = user_level;
	temp_unprot_seq[3].magic = fb->pw1;
	temp_unprot_seq[4].magic = fb->pw2;

	int res = xmc4xxx_write_command_sequence(bank, temp_unprot_seq, 
		ARRAY_SIZE(temp_unprot_seq));
	if (res != ERROR_OK) {
		LOG_ERROR("Unable to write temp unprotect sequence");
		return res;
	}

	uint32_t status = 0;
	res = xmc4xxx_get_flash_status(bank, &status);
	if (res != ERROR_OK)
		return res;

	if (status & FSR_WPRODIS0) {
		LOG_INFO("Flash is temporarily unprotected");
	} else {
		LOG_INFO("Unable to disable flash protection");
		res = ERROR_FAIL;
	}
	return res;
}

/* count is the size divided by xmc4xxx_info->data_width */
static int xmc4xxx_write_block(struct flash_bank *bank, const uint8_t *buffer,
	uint32_t offset, uint32_t count)
{
	static const uint8_t xmc4xxx_flash_write_code[] = {
		#include "../../../contrib/loaders/flash/xmc4xxx/write.inc"
	};

	struct working_area *write_algorithm;
	if (target_alloc_working_area(bank->target, sizeof(xmc4xxx_flash_write_code), 
		&write_algorithm) != ERROR_OK) {
		LOG_WARNING("no working area available, can't do block memory writes");
		return ERROR_TARGET_RESOURCE_NOT_AVAILABLE;
	}

	int retval = target_write_buffer(bank->target, write_algorithm->address,
		sizeof(xmc4xxx_flash_write_code), xmc4xxx_flash_write_code);
	if (retval != ERROR_OK) {
		target_free_working_area(bank->target, write_algorithm);
		return retval;
	}

	const size_t extra_size = sizeof(struct xmc4xxx_work_area);
	uint32_t buffer_size = target_get_working_area_avail(bank->target) - extra_size;
	/* buffer_size should be multiple of XMC4XXX_FLASH_WORD_SIZE */
	buffer_size &= ~(XMC4XXX_FLASH_WORD_SIZE - 1);

	if (buffer_size < 256) {
		LOG_WARNING("large enough working area not available, can't do block memory writes");
		target_free_working_area(bank->target, write_algorithm);
		return ERROR_TARGET_RESOURCE_NOT_AVAILABLE;
	} else if (buffer_size > (16 * 1024)) {
		/* probably won't benefit from more than 16k ... */
		buffer_size = (16 * 1024);
	}

	struct working_area *source;
	if (target_alloc_working_area_try(bank->target, buffer_size + extra_size, &source) != ERROR_OK) {
		LOG_ERROR("allocating working area failed");
		target_free_working_area(bank->target, write_algorithm);
		return ERROR_TARGET_RESOURCE_NOT_AVAILABLE;
	}

	/* contrib/loaders/flash/xmc4xxx.c:write() arguments */
	struct reg_param reg_params[5];
	init_reg_param(&reg_params[0], "r0", 32, PARAM_IN_OUT);	/* xmc4xxx_work_area ptr , status (out) */
	init_reg_param(&reg_params[1], "r1", 32, PARAM_OUT);	/* buffer end */
	init_reg_param(&reg_params[2], "r2", 32, PARAM_OUT);	/* target address */
	init_reg_param(&reg_params[3], "r3", 32, PARAM_OUT);	/* count (of xmc4xxx_info->data_width) */
	init_reg_param(&reg_params[4], "sp", 32, PARAM_OUT);

	buf_set_u32(reg_params[0].value, 0, 32, source->address);
	buf_set_u32(reg_params[1].value, 0, 32, source->address + source->size);
	buf_set_u32(reg_params[2].value, 0, 32, bank->base + offset);
	buf_set_u32(reg_params[3].value, 0, 32, count);
	buf_set_u32(reg_params[4].value, 0, 32, source->address + 
		offsetof(struct xmc4xxx_work_area, stack) + LDR_STACK_SIZE);

	struct armv7m_algorithm armv7m_info;
	armv7m_info.core_mode = ARM_MODE_THREAD;
	armv7m_info.common_magic = ARMV7M_COMMON_MAGIC;
	retval = target_run_flash_async_algorithm(bank->target, buffer, count, XMC4XXX_FLASH_WORD_SIZE,
		0, NULL, ARRAY_SIZE(reg_params), reg_params, source->address + offsetof(struct xmc4xxx_work_area, fifo),
		source->size - offsetof(struct xmc4xxx_work_area, fifo), write_algorithm->address, 0, &armv7m_info);
	if (retval == ERROR_FLASH_OPERATION_FAILED) {
		LOG_ERROR("error executing stm32l4 flash write algorithm");
		retval = ERROR_FAIL;
	}

	target_free_working_area(bank->target, source);
	target_free_working_area(bank->target, write_algorithm);

	destroy_reg_param(&reg_params[0]);
	destroy_reg_param(&reg_params[1]);
	destroy_reg_param(&reg_params[2]);
	destroy_reg_param(&reg_params[3]);
	destroy_reg_param(&reg_params[4]);

	xmc4xxx_clear_flash_status(bank);
	return retval;
}

/* Reference: "XMC4500 Flash Protection.pptx" app note */
static int xmc4xxx_flash_protect(struct flash_bank *bank, int level, bool read_protect,
	unsigned int first, unsigned int last)
{
	struct xmc4xxx_flash_bank *fb = bank->driver_priv;

	/* Read protect only works for user 0, make sure we don't try
	 * to do something silly */
	if (level != 0 && read_protect) {
		LOG_ERROR("Read protection is for user level 0 only!");
		return ERROR_FAIL;
	}

	/* Check to see if protection is already installed for the
	 * specified user level.  If it is, the user configuration
	 * block will need to be erased before we can continue */

	uint32_t status = 0;
	/* Grab the flash status register*/
	int res = xmc4xxx_get_flash_status(bank, &status);
	if (res != ERROR_OK)
		return res;

	bool proin = false;
	switch (level) {
	case 0:
		if ((status & FSR_RPROIN_MASK) || (status & FSR_WPROIN0_MASK))
			proin = true;
		break;
	case 1:
		if (status & FSR_WPROIN1_MASK)
			proin = true;
		break;
	case 2:
		if (status & FSR_WPROIN2_MASK)
			proin = true;
		break;
	}

	if (proin) {
		LOG_ERROR("Flash protection is installed for user %d"
			" and must be removed before continuing", level);
		return ERROR_FAIL;
	}

	/* If this device has 12 flash sectors, protection for
	 * sectors 10 & 11 are handled jointly. If we are trying to
	 * write all sectors, we should decrement
	 * last to ensure we don't write to a register bit that
	 * doesn't exist*/
	if ((bank->num_sectors == 12) && (last == 12))
		last--;

	uint32_t procon = 0;
	/*  We need to fill out the procon register representation
	 *   that we will be writing to the device */
	for (unsigned int i = first; i <= last; i++)
		procon |= 1 << i;

	/* If read protection is requested, set the appropriate bit
	 * (we checked that this is allowed above) */
	if (read_protect)
		procon |= PROCON_RPRO_MASK;

	LOG_DEBUG("Setting flash protection with procon:");
	LOG_DEBUG("PROCON: %"PRIx32, procon);

	/* User configuration block buffers */
	uint8_t ucp0_buf[8 * sizeof(uint32_t)] = { 0 };

	/* First we need to copy in the procon register to the buffer
	 * we're going to attempt to write.  This is written twice */
	target_buffer_set_u32(bank->target, &ucp0_buf[0 * 4], procon);
	target_buffer_set_u32(bank->target, &ucp0_buf[2 * 4], procon);

	/* Now we must copy in both flash passwords.  As with the
	 * procon data, this must be written twice (4 total words
	 * worth of data) */
	target_buffer_set_u32(bank->target, &ucp0_buf[4 * 4], fb->pw1);
	target_buffer_set_u32(bank->target, &ucp0_buf[5 * 4], fb->pw2);
	target_buffer_set_u32(bank->target, &ucp0_buf[6 * 4], fb->pw1);
	target_buffer_set_u32(bank->target, &ucp0_buf[7 * 4], fb->pw2);

	/* Finally, (if requested) we copy in the confirmation
	 * code so that the protection is permanent and will
	 * require a password to undo. */
	target_buffer_set_u32(bank->target, &ucp0_buf[0 * 4], FLASH_PROTECT_CONFIRMATION_CODE);
	target_buffer_set_u32(bank->target, &ucp0_buf[2 * 4], FLASH_PROTECT_CONFIRMATION_CODE);

	/* Now that the data is copied into place, we must write
	 * these pages into flash */

	uint32_t ucb_base = 0;
	/* The user configuration block base depends on what level of
	 * protection we're trying to install, select the proper one */
	switch (level) {
	case 0:
		ucb_base = UCB0_BASE;
		break;
	case 1:
		ucb_base = UCB1_BASE;
		break;
	case 2:
		ucb_base = UCB2_BASE;
		break;
	}

	/* Write the user config pages */
	res = xmc4xxx_write_block(bank, ucp0_buf, ucb_base, sizeof(ucp0_buf) / XMC4XXX_FLASH_WORD_SIZE);
	if (res != ERROR_OK) {
		LOG_ERROR("Error writing user configuration block 0");
		return res;
	}
	return ERROR_OK;
}

static int xmc4xxx_protect_check(struct flash_bank *bank)
{
	struct xmc4xxx_flash_bank *fb = bank->driver_priv;

	uint32_t protection[3] = { 0 };
	int ret = target_read_u32(bank->target, FLASH_REG_FLASH0_PROCON0, &protection[0]);
	if (ret != ERROR_OK) {
		LOG_ERROR("Unable to read flash User0 protection register");
		return ret;
	}

	ret = target_read_u32(bank->target, FLASH_REG_FLASH0_PROCON1, &protection[1]);
	if (ret != ERROR_OK) {
		LOG_ERROR("Unable to read flash User1 protection register");
		return ret;
	}

	ret = target_read_u32(bank->target, FLASH_REG_FLASH0_PROCON2, &protection[2]);
	if (ret != ERROR_OK) {
		LOG_ERROR("Unable to read flash User2 protection register");
		return ret;
	}

	/* On devices with 12 sectors, sectors 10 & 11 are protected
	 * together instead of individually */
	unsigned int sectors = bank->num_sectors;
	if (sectors == 12)
		sectors--;

	/* Clear the protection status */
	for (unsigned int i = 0; i < bank->num_sectors; i++) {
		bank->sectors[i].is_protected = 0;
		fb->write_prot_otp[i] = false;
	}
	fb->read_protected = false;

	/* The xmc4xxx series supports 3 levels of user protection
	 * (User0, User1 (low priority), and User 2(OTP), we need to
	 * check all 3 */
	for (unsigned int i = 0; i < ARRAY_SIZE(protection); i++) {

		/* Check for write protection on every available
		*  sector */
		for (unsigned int j = 0; j < sectors; j++) {
			int set = (protection[i] & (1 << j)) ? 1 : 0;
			bank->sectors[j].is_protected |= set;

			/* Handle sector 11 */
			if (j == 10)
				bank->sectors[j + 1].is_protected |= set;

			/* User 2 indicates this protection is
			 * permanent, make note in the private driver structure */
			if (i == 2 && set) {
				fb->write_prot_otp[j] = true;

				/* Handle sector 11 */
				if (j == 10)
					fb->write_prot_otp[j + 1] = true;
			}

		}
	}

	/* XMC4xxx also supports read protection, make a note
	 * in the private driver structure */
	if (protection[0] & PROCON_RPRO_MASK)
		fb->read_protected = true;

	return ERROR_OK;
}

static int xmc4xxx_erase(struct flash_bank *bank, unsigned int first, unsigned int last)
{
	struct xmc4xxx_flash_bank *fb = bank->driver_priv;

	if (bank->target->state != TARGET_HALTED) {
		LOG_ERROR("Unable to erase, target is not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	int res;
	if (!fb->probed) {
		res = xmc4xxx_probe(bank);
		if (res != ERROR_OK)
			return res;
	}

	uint32_t tmp_addr;
	/* Loop through the sectors and erase each one */
	for (unsigned int i = first; i <= last; i++) {
		res = xmc4xxx_get_sector_start_addr(bank, i, &tmp_addr);
		if (res != ERROR_OK) {
			LOG_ERROR("Invalid sector %u", i);
			return res;
		}

		LOG_DEBUG("Erasing sector %u @ 0x%08"PRIx32, i, tmp_addr);

		res = xmc4xxx_erase_sector(bank, tmp_addr, false);
		if (res != ERROR_OK) {
			LOG_ERROR("Unable to write erase command sequence");
			goto clear_status_and_exit;
		}

		/* Now we must wait for the erase operation to end */
		res = xmc4xxx_wait_status_busy(bank, FLASH_OP_TIMEOUT);
		if (res != ERROR_OK)
			goto clear_status_and_exit;
	}

clear_status_and_exit:
	res = xmc4xxx_clear_flash_status(bank);
	return res;
}

static int xmc4xxx_write(struct flash_bank *bank, const uint8_t *buffer,
	uint32_t offset, uint32_t count)
{
	int retval;
	struct xmc4xxx_flash_bank *fb = bank->driver_priv;

	if (bank->target->state != TARGET_HALTED) {
		LOG_ERROR("Unable to erase, target is not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	if (!fb->probed) {
		retval = xmc4xxx_probe(bank);
		if (retval != ERROR_OK)
			return retval;
	}

	/* Make sure we won't run off the end of the flash bank */
	if (offset + count > bank->size) {
		LOG_ERROR("Attempting to write past the end of flash");
		return ERROR_FAIL;
	}

	retval = xmc4xxx_write_block(bank, buffer, offset, count / XMC4XXX_FLASH_WORD_SIZE);
	return retval;
}

static int xmc4xxx_protect(struct flash_bank *bank, int set, unsigned int first,
	unsigned int last)
{
	int ret;
	struct xmc4xxx_flash_bank *fb = bank->driver_priv;

	/* Check for flash passwords */
	if (!fb->pw_set) {
		LOG_ERROR("Flash passwords not set, use xmc4xxx flash_password to set them");
		return ERROR_FAIL;
	}

	/* We want to clear flash protection temporarily*/
	if (set == 0) {
		LOG_WARNING("Flash protection will be temporarily disabled"
			" for all pages (User 0 only)!");
		ret = xmc4xxx_temp_unprotect(bank, 0);
		return ret;
	}

	/* Install write protection for user 0 on the specified pages */
	ret = xmc4xxx_flash_protect(bank, 0, false, first, last);
	return ret;
}

COMMAND_HANDLER(xmc4xxx_handle_flash_unprotect_command)
{
	if (CMD_ARGC < 2)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct flash_bank *bank;
	int res = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	if (res != ERROR_OK)
		return res;

	int32_t level;
	COMMAND_PARSE_NUMBER(s32, CMD_ARGV[1], level);

	res = xmc4xxx_flash_unprotect(bank, level);
	return res;
}

COMMAND_HANDLER(xmc4xxx_handle_flash_password_command)
{
	if (CMD_ARGC < 3)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct flash_bank *bank;
	int res = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	if (res != ERROR_OK)
		return res;

	struct xmc4xxx_flash_bank *fb = bank->driver_priv;

	errno = 0;
	/* We skip over the flash bank */
	COMMAND_PARSE_NUMBER(u32, CMD_ARGV[1], fb->pw1);
	if (errno)
		return ERROR_COMMAND_SYNTAX_ERROR;

	COMMAND_PARSE_NUMBER(u32, CMD_ARGV[2], fb->pw2);
	if (errno)
		return ERROR_COMMAND_SYNTAX_ERROR;

	fb->pw_set = true;

	command_print(CMD, "XMC4xxx flash passwords set to:\n");
	command_print(CMD, "-0x%08"PRIx32"\n", fb->pw1);
	command_print(CMD, "-0x%08"PRIx32"\n", fb->pw2);
	return ERROR_OK;
}

static const struct command_registration xmc4xxx_exec_command_handlers[] = {
	{
		.name = "flash_password",
		.handler = xmc4xxx_handle_flash_password_command,
		.mode = COMMAND_EXEC,
		.usage = "bank_id password1 password2",
		.help = "Set the flash passwords used for protect operations. "
			"Passwords should be in standard hex form (0x00000000). "
			"(You must call this before any other protect commands) "
			"NOTE: The xmc4xxx's UCB area only allows for FOUR cycles. "
			"Please use protection carefully!",
	},
	{
		.name = "flash_unprotect",
		.handler = xmc4xxx_handle_flash_unprotect_command,
		.mode = COMMAND_EXEC,
		.usage = "bank_id user_level[0-1]",
		.help = "Permanently Removes flash protection (read and write) "
			"for the specified user level",
	},
	COMMAND_REGISTRATION_DONE
};

static const struct command_registration xmc4xxx_command_handlers[] = {
	{
		.name = "xmc4xxx",
		.mode = COMMAND_ANY,
		.help = "xmc4xxx flash command group",
		.usage = "",
		.chain = xmc4xxx_exec_command_handlers,
	},
	COMMAND_REGISTRATION_DONE
};

const struct flash_driver xmc4xxx_flash = {
	.name = "xmc4xxx",
	.flash_bank_command = xmc4xxx_flash_bank_command,
	.info = xmc4xxx_get_info_command,
	.probe = xmc4xxx_probe,
	.auto_probe = xmc4xxx_probe,
	.protect_check = xmc4xxx_protect_check,
	.read = default_flash_read,
	.erase = xmc4xxx_erase,
	.erase_check = default_flash_blank_check,
	.write = xmc4xxx_write,
	.free_driver_priv = default_flash_free_driver_priv,
	.commands = xmc4xxx_command_handlers,
	.protect = xmc4xxx_protect,
};
