#include <stdint.h>

#define OPENOCD_CONTRIB_LOADERS_FLASH_XMC4XXX
#include "../../../../src/flash/nor/xmc4xxx.h"

#pragma pack(push, 1)
typedef struct
{
	uint32_t RESERVED[1026];
	uint32_t ID;
	uint32_t RESERVED1;
	volatile uint32_t FSR;
	uint32_t FCON;
	uint32_t MARP;
	uint32_t RESERVED2;
	uint32_t PROCON0;
	uint32_t PROCON1;
	uint32_t PROCON2;
} FLASH0_GLOBAL_TypeDef;
#pragma pack(pop)

static void XMC_FLASH_lClearStatusCommand();
static void XMC_FLASH_lEnterPageModeCommand();
static void XMC_FLASH_lWritePageCommand(uint32_t *page_start_address);
static void XMC_FLASH_lLoadPageCommand(uint32_t low_word, uint32_t high_word);

/* this function is assumes that fifo_size is multiple of flash_word_size
 * this condition is ensured by target_run_flash_async_algorithm */
void write(volatile struct xmc4xxx_work_area *work_area, uint8_t *fifo_end,
	uint32_t *target_address, uint32_t count)
{
	/* optimization to avoid reading from memory each time */
	uint8_t *rp_cache = work_area->fifo.rp;

	/* fifo_start is used to wrap when we reach fifo_end */
	uint8_t *fifo_start = rp_cache;

	uint32_t idx = 0;
	while(count)
	{
		/* optimization to avoid reading from memory each time */
		uint8_t *wp_cache  = work_area->fifo.wp;
		if (wp_cache == 0)
			break; /* aborted by target_run_flash_async_algorithm */

		int32_t fifo_size = wp_cache - rp_cache;
		if (fifo_size < 0)
		{
			/* consider the linear fifo, we will wrap later */
			fifo_size = fifo_end - rp_cache;
		}

		/* wait for at least a flash word */
		while(fifo_size >= XMC4XXX_FLASH_WORD_SIZE)
		{
			if (idx == 0)
			{
				XMC_FLASH_lClearStatusCommand();
				XMC_FLASH_lEnterPageModeCommand();
			}
		
			XMC_FLASH_lLoadPageCommand(((uint32_t*)rp_cache)[0], ((uint32_t*)rp_cache)[1]);
			idx += 2;

			if (idx == XMC_FLASH_WORDS_PER_PAGE)
			{
				XMC_FLASH_lWritePageCommand(target_address);			
				while((FLASH0->FSR & (uint32_t)FLASH_FSR_PBUSY_Msk) != 0);

				target_address += idx;
				idx = 0;
			}
			
			rp_cache += XMC4XXX_FLASH_WORD_SIZE;
			if (rp_cache >= fifo_end)
				rp_cache = fifo_start;

			/* flush the rp cache value,
			 * so target_run_flash_async_algorithm can fill the circular fifo */
			work_area->fifo.rp = rp_cache;

			/* update fifo_size and count */
			fifo_size -= XMC4XXX_FLASH_WORD_SIZE;
			count--;
		}
	}

	/* soft break the loader */
	__asm("bkpt 0");
}

void XMC_FLASH_lClearStatusCommand()
{
	volatile uint32_t *address;
	address = (uint32_t*)(XMC_FLASH_UNCACHED_BASE + 0x5554U);
	*address = 0xf5U;
}

void XMC_FLASH_lEnterPageModeCommand()
{
	volatile uint32_t *address;
	address = (uint32_t*)(XMC_FLASH_UNCACHED_BASE + 0x5554U);
	*address = (uint32_t)0x50U;
}

void XMC_FLASH_lWritePageCommand(uint32_t *page_start_address)
{
	volatile uint32_t *address;
	address = (uint32_t*)(XMC_FLASH_UNCACHED_BASE + 0x5554U);
	*address = 0xaaU;
	address = (uint32_t*)(XMC_FLASH_UNCACHED_BASE + 0xaaa8U);
	*address = 0x55U;
	address = (uint32_t*)(XMC_FLASH_UNCACHED_BASE + 0x5554U);
	*address = 0xa0U;
	address = page_start_address;
	*address = 0xaaU;
}

void XMC_FLASH_lLoadPageCommand(uint32_t low_word, uint32_t high_word)
{
	volatile uint32_t *address;
	address = (uint32_t*)(XMC_FLASH_UNCACHED_BASE + 0x55f0U);
	*address = low_word;
	address = (uint32_t*)(XMC_FLASH_UNCACHED_BASE + 0x55f4U);
	*address = high_word;
}

