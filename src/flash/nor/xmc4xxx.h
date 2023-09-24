#ifndef OPENOCD_FLASH_NOR_XMC4XXX
#define OPENOCD_FLASH_NOR_XMC4XXX

#define XMC4XXX_FLASH_WORD_SIZE		8

#define FLASH_FSR_PBUSY_Msk		(0x1UL)
#define XMC_FLASH_WORDS_PER_PAGE	(64UL)
#define XMC_FLASH_UNCACHED_BASE		(0x0C000000U)
#define FLASH0				((FLASH0_GLOBAL_TypeDef*)0x58001000UL)

/* Maximum number of sectors */
#define MAX_XMC_SECTORS 12

/* System control unit registers */
#define SCU_REG_BASE 0x50004000

#define SCU_ID_CHIP 0x04

/* Base of the non-cached flash memory */
#define PFLASH_BASE	0x0C000000

/* User configuration block offsets */
#define UCB0_BASE       0x00000000
#define UCB1_BASE       0x00000400
#define UCB2_BASE       0x00000800

/* Flash register base */
#define FLASH_REG_BASE 0x58000000

/* PMU ID Registers */
#define FLASH_REG_PMU_ID	(FLASH_REG_BASE | 0x0508)

/* PMU Fields */
#define PMU_MOD_REV_MASK	0xFF
#define PMU_MOD_TYPE_MASK	0xFF00
#define PMU_MOD_NO_MASK		0xFFFF0000

/* Prefetch Config */
#define FLASH_REG_PREF_PCON	(FLASH_REG_BASE | 0x4000)

/* Prefetch Fields */
#define PCON_IBYP	(1 << 0)
#define PCON_IINV	(1 << 1)

/* Flash ID Register */
#define FLASH_REG_FLASH0_ID	(FLASH_REG_BASE | 0x2008)

/* Flash Status Register */
#define FLASH_REG_FLASH0_FSR	(FLASH_REG_BASE | 0x2010)

#define FSR_PBUSY	(0)
#define FSR_FABUSY	(1)
#define FSR_PROG	(4)
#define FSR_ERASE	(5)
#define FSR_PFPAGE	(6)
#define FSR_PFOPER	(8)
#define FSR_SQER	(10)
#define FSR_PROER	(11)
#define FSR_PFSBER	(12)
#define FSR_PFDBER	(14)
#define FSR_PROIN	(16)
#define FSR_RPROIN	(18)
#define FSR_RPRODIS	(19)
#define FSR_WPROIN0	(21)
#define FSR_WPROIN1	(22)
#define FSR_WPROIN2	(23)
#define FSR_WPRODIS0	(25)
#define FSR_WPRODIS1	(26)
#define FSR_SLM		(28)
#define FSR_VER		(31)

#define FSR_PBUSY_MASK		(0x01 << FSR_PBUSY)
#define FSR_FABUSY_MASK		(0x01 << FSR_FABUSY)
#define FSR_PROG_MASK		(0x01 << FSR_PROG)
#define FSR_ERASE_MASK		(0x01 << FSR_ERASE)
#define FSR_PFPAGE_MASK		(0x01 << FSR_PFPAGE)
#define FSR_PFOPER_MASK		(0x01 << FSR_PFOPER)
#define FSR_SQER_MASK		(0x01 << FSR_SQER)
#define FSR_PROER_MASK		(0x01 << FSR_PROER)
#define FSR_PFSBER_MASK		(0x01 << FSR_PFSBER)
#define FSR_PFDBER_MASK		(0x01 << FSR_PFDBER)
#define FSR_PROIN_MASK		(0x01 << FSR_PROIN)
#define FSR_RPROIN_MASK		(0x01 << FSR_RPROIN)
#define FSR_RPRODIS_MASK	(0x01 << FSR_RPRODIS)
#define FSR_WPROIN0_MASK	(0x01 << FSR_WPROIN0)
#define FSR_WPROIN1_MASK	(0x01 << FSR_WPROIN1)
#define FSR_WPROIN2_MASK	(0x01 << FSR_WPROIN2)
#define FSR_WPRODIS0_MASK	(0x01 << FSR_WPRODIS0)
#define FSR_WPRODIS1_MASK	(0x01 << FSR_WPRODIS1)
#define FSR_SLM_MASK		(0x01 << FSR_SLM)
#define FSR_VER_MASK		(0x01 << FSR_VER)

/* Flash Config Register */
#define FLASH_REG_FLASH0_FCON	(FLASH_REG_BASE | 0x2014)

#define FCON_WSPFLASH           (0)
#define FCON_WSECPF             (4)
#define FCON_IDLE               (13)
#define FCON_ESLDIS             (14)
#define FCON_SLEEP              (15)
#define FCON_RPA                (16)
#define FCON_DCF                (17)
#define FCON_DDF                (18)
#define FCON_VOPERM             (24)
#define FCON_SQERM              (25)
#define FCON_PROERM             (26)
#define FCON_PFSBERM            (27)
#define FCON_PFDBERM            (29)
#define FCON_EOBM               (31)

#define FCON_WSPFLASH_MASK      (0x0f << FCON_WSPFLASH)
#define FCON_WSECPF_MASK        (0x01 << FCON_WSECPF)
#define FCON_IDLE_MASK          (0x01 << FCON_IDLE)
#define FCON_ESLDIS_MASK        (0x01 << FCON_ESLDIS)
#define FCON_SLEEP_MASK         (0x01 << FCON_SLEEP)
#define FCON_RPA_MASK           (0x01 << FCON_RPA)
#define FCON_DCF_MASK           (0x01 << FCON_DCF)
#define FCON_DDF_MASK           (0x01 << FCON_DDF)
#define FCON_VOPERM_MASK        (0x01 << FCON_VOPERM)
#define FCON_SQERM_MASK         (0x01 << FCON_SQERM)
#define FCON_PROERM_MASK        (0x01 << FCON_PROERM)
#define FCON_PFSBERM_MASK       (0x01 << FCON_PFSBERM)
#define FCON_PFDBERM_MASK       (0x01 << FCON_PFDBERM)
#define FCON_EOBM_MASK          (0x01 << FCON_EOBM)

/* Flash Margin Control Register */
#define FLASH_REG_FLASH0_MARP	(FLASH_REG_BASE | 0x2018)

#define MARP_MARGIN		(0)
#define MARP_TRAPDIS		(15)

#define MARP_MARGIN_MASK        (0x0f << MARP_MARGIN)
#define MARP_TRAPDIS_MASK       (0x01 << MARP_TRAPDIS)

/* Flash Protection Registers */
#define FLASH_REG_FLASH0_PROCON0	(FLASH_REG_BASE | 0x2020)
#define FLASH_REG_FLASH0_PROCON1	(FLASH_REG_BASE | 0x2024)
#define FLASH_REG_FLASH0_PROCON2	(FLASH_REG_BASE | 0x2028)

#define PROCON_S0L             (0)
#define PROCON_S1L             (1)
#define PROCON_S2L             (2)
#define PROCON_S3L             (3)
#define PROCON_S4L             (4)
#define PROCON_S5L             (5)
#define PROCON_S6L             (6)
#define PROCON_S7L             (7)
#define PROCON_S8L             (8)
#define PROCON_S9L             (9)
#define PROCON_S10_S11L        (10)
#define PROCON_RPRO            (15)

#define PROCON_S0L_MASK        (0x01 << PROCON_S0L)
#define PROCON_S1L_MASK        (0x01 << PROCON_S1L)
#define PROCON_S2L_MASK        (0x01 << PROCON_S2L)
#define PROCON_S3L_MASK        (0x01 << PROCON_S3L)
#define PROCON_S4L_MASK        (0x01 << PROCON_S4L)
#define PROCON_S5L_MASK        (0x01 << PROCON_S5L)
#define PROCON_S6L_MASK        (0x01 << PROCON_S6L)
#define PROCON_S7L_MASK        (0x01 << PROCON_S7L)
#define PROCON_S8L_MASK        (0x01 << PROCON_S8L)
#define PROCON_S9L_MASK        (0x01 << PROCON_S9L)
#define PROCON_S10_S11L_MASK   (0x01 << PROCON_S10_S11L)
#define PROCON_RPRO_MASK       (0x01 << PROCON_RPRO)

#define FLASH_PROTECT_CONFIRMATION_CODE 0x8AFE15C3

/* Flash controller configuration values */
#define FLASH_ID_XMC4500        0xA2
#define FLASH_ID_XMC4300_XMC4700_4800   0x92
#define FLASH_ID_XMC4100_4200   0x9C
#define FLASH_ID_XMC4400        0x9F

/* Timeouts */
#define FLASH_OP_TIMEOUT 5000

/* Flash commands (write/erase/protect) are performed using special
 * command sequences that are written to magic addresses in the flash controller */
/* Command sequence addresses.  See reference manual, section 8: Flash Command Sequences */
#define FLASH_CMD_ERASE_1 0x0C005554
#define FLASH_CMD_ERASE_2 0x0C00AAA8
#define FLASH_CMD_ERASE_3 FLASH_CMD_ERASE_1
#define FLASH_CMD_ERASE_4 FLASH_CMD_ERASE_1
#define FLASH_CMD_ERASE_5 FLASH_CMD_ERASE_2
/* ERASE_6 is the sector base address */

#define FLASH_CMD_CLEAR_STATUS FLASH_CMD_ERASE_1

#define FLASH_CMD_ENTER_PAGEMODE FLASH_CMD_ERASE_1

#define FLASH_CMD_LOAD_PAGE_1 0x0C0055F0
#define FLASH_CMD_LOAD_PAGE_2 0x0C0055F4

#define FLASH_CMD_WRITE_PAGE_1 FLASH_CMD_ERASE_1
#define FLASH_CMD_WRITE_PAGE_2 FLASH_CMD_ERASE_2
#define FLASH_CMD_WRITE_PAGE_3 FLASH_CMD_ERASE_1
/* WRITE_PAGE_4 is the page base address */

#define FLASH_CMD_TEMP_UNPROT_1 FLASH_CMD_ERASE_1
#define FLASH_CMD_TEMP_UNPROT_2 FLASH_CMD_ERASE_2
#define FLASH_CMD_TEMP_UNPROT_3 0x0C00553C
#define FLASH_CMD_TEMP_UNPROT_4 FLASH_CMD_ERASE_2
#define FLASH_CMD_TEMP_UNPROT_5 FLASH_CMD_ERASE_2
#define FLASH_CMD_TEMP_UNPROT_6 0x0C005558

/* 100 bytes as loader stack should be large enough for the loader to operate */
#define LDR_STACK_SIZE			100

// Set package alignment to 1 byte
#pragma pack(push, 1)

struct xmc4xxx_work_area
{
	uint8_t stack[LDR_STACK_SIZE];
	struct flash_async_algorithm_circbuf
	{
#ifdef OPENOCD_CONTRIB_LOADERS_FLASH_XMC4XXX
		uint8_t *wp;
		uint8_t *rp;
#else
		uint32_t wp;
		uint32_t rp;
#endif
	} fifo;
};

#pragma pack(pop)

#endif
