/* linker.ld - Linker command/script file */

/*
 * Copyright (c) 2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _LINKER
#define _ASMLANGUAGE

#include <autoconf.h>
#include <linker/sections.h>
#include <generated_dts_board.h>

#include <linker/linker-defs.h>
#include <linker/linker-tool.h>

#ifdef CONFIG_SECTION_OVERLAY
#include <section_overlay.h>
#endif

IBANK_SIZE = 2K;

/* physical address of RAM */
#ifdef CONFIG_XIP
  #define ROMABLE_REGION FLASH
  #define RAMABLE_REGION PSRAM
  #define EXT_RAMABLE_REGION SRAM
#else
  #define ROMABLE_REGION SRAM
  #define RAMABLE_REGION SRAM
#endif

#if defined(CONFIG_XIP)
  #define _DATA_IN_ROM __data_rom_start
#else
  #define _DATA_IN_ROM
#endif

#if !defined(SKIP_TO_KINETIS_FLASH_CONFIG)
  #define SKIP_TO_KINETIS_FLASH_CONFIG
#endif

#define ROM_ADDR (CONFIG_FLASH_BASE_ADDRESS + CONFIG_FLASH_LOAD_OFFSET)
#if CONFIG_FLASH_LOAD_SIZE > 0
  #define ROM_SIZE CONFIG_FLASH_LOAD_SIZE
#else
  #define ROM_SIZE (CONFIG_FLASH_SIZE*1K - CONFIG_FLASH_LOAD_OFFSET)
#endif

#define RAM_SIZE   CONFIG_SRAM_SIZE * 1K
#define RAM_ADDR   CONFIG_SRAM_BASE_ADDRESS
#define PSRAM_SIZE CONFIG_SOC_MAPPING_PSRAM_SIZE
#define PSRAM_ADDR CONFIG_SOC_MAPPING_PSRAM_ADDR
#define BTCON_MEMPOOL_REGION PSRAM


MEMORY
    {
    FLASH                 (rx) : ORIGIN = ROM_ADDR, LENGTH = ROM_SIZE
    SRAM                  (wx) : ORIGIN = RAM_ADDR, LENGTH = RAM_SIZE
    PSRAM                 (wx) : ORIGIN = PSRAM_ADDR, LENGTH = 0x40000
    ram_mpool             (wx) : ORIGIN = PSRAM_ADDR + 0x40000, LENGTH = 0xE000
    ram_eds_data          (wx) : ORIGIN = PSRAM_ADDR + 0x4E000, LENGTH = 0x100
    ram_pool2	          (wx) : ORIGIN = PSRAM_ADDR + 0x4E100, LENGTH = 0x8700
    ram_hw_pcmbuf         (wx) : ORIGIN = 0x00034000, LENGTH = 0x1000
    /*算法占用76K空间*/
    ram_dsp               (wx) : ORIGIN = 0x00069000, LENGTH = 0x13000

    /* Used by and documented in include/linker/intlist.ld */
    IDT_LIST  (wx)      : ORIGIN = 0xFFFFEFFF, LENGTH = 4K
    }

SECTIONS
    {
    GROUP_START(ROMABLE_REGION)

	_image_rom_start = ROM_ADDR;

    SECTION_PROLOGUE(_TEXT_SECTION_NAME,,)
	{
	/* flash image header */
	. = 0x0;
	KEEP(*(.img_header))

	_vector_start = .;

	. = CONFIG_TEXT_SECTION_OFFSET;
	KEEP(*(.exp_table))
	KEEP(*(".exp_table.*"))

	KEEP(*(IRQ_VECTOR_TABLE))

	KEEP(*(.openocd_dbg))
	KEEP(*(".openocd_dbg.*"))

	/* Kinetis has to write 16 bytes at 0x400 */
	SKIP_TO_KINETIS_FLASH_CONFIG
	KEEP(*(.kinetis_flash_config))
	KEEP(*(".kinetis_flash_config.*"))

#if defined(CONFIG_GEN_SW_ISR_TABLE) && !defined(CONFIG_IRQ_STAT)
	KEEP(*(SW_ISR_TABLE))
#endif
	_vector_end = .;

	_image_text_start = .;
	*(.text)
	*(".text.*")
	*(.gnu.linkonce.t.*)
	KEEP(*(.BTCON_FIX_CODE))
	} GROUP_LINK_IN(ROMABLE_REGION)

	_image_text_end = .;

#include <linker/common-rom.ld>

    SECTION_PROLOGUE(_RODATA_SECTION_NAME,,)
	{
	*(.rodata)
	*(".rodata.*")
	*(.gnu.linkonce.r.*)

#ifdef CONFIG_DSP
	*(.dsp_code)
#endif

#ifdef CONFIG_ACTIONS_TEST
    . = ALIGN(4);
    __test_case_start = .;
    KEEP(*(.test_case_info))
    __test_case_end = .;
#endif

    . = ALIGN(4);
    crash_dump_start = .;
    KEEP(*(SORT(.crash_dump_[_A-Z0-9]*)))
    crash_dump_end = .;

#ifdef CONFIG_SECTION_OVERLAY
    . = ALIGN(4);
	__overlay_table = .;
	LONG(OVERLAY_TABLE_MAGIC)
	/* overlay items count */
	LONG(1)

	/* for libdecact.a */
	LONG(OVERLAY_ID_LIBDECACT);
	LONG(0);
	LONG(0);
	LONG(0);
	LONG(ABSOLUTE(ADDR(.overlay.data.act)));
	LONG(SIZEOF(.overlay.data.act));
	LONG(LOADADDR(.overlay.data.act));
	LONG(ABSOLUTE(ADDR(.overlay.bss.act)));
	LONG(SIZEOF(.overlay.bss.act));
	. = ALIGN(4);
#endif

#ifdef CONFIG_CUSTOM_RODATA_LD
/* Located in project source directory */
#include <custom-rodata.ld>
#endif
	/*
	 * For XIP images, in order to avoid the situation when __data_rom_start
	 * is 32-bit aligned, but the actual data is placed right after rodata
	 * section, which may not end exactly at 32-bit border, pad rodata
	 * section, so __data_rom_start points at data and it is 32-bit aligned.
	 *
	 * On non-XIP images this may enlarge image size up to 3 bytes. This
	 * generally is not an issue, since modern ROM and FLASH memory is
	 * usually 4k aligned.
	 */
	. = ALIGN(4);
	} GROUP_LINK_IN(ROMABLE_REGION)

	_image_rom_end = .;
    __data_rom_start = .;    /* XIP imaged DATA ROM start addr */

    GROUP_END(ROMABLE_REGION)

    GROUP_START(RAMABLE_REGION)

    SECTION_DATA_PROLOGUE(_DATA_SECTION_NAME,,)
	{
	_image_ram_start = .;
	__data_ram_start = .;

	__app_entry_table = .;
	KEEP(*(.app_entry))
	__app_entry_end = .;

	__service_entry_table = .;
	KEEP(*(.service_entry))
	__service_entry_end = .;

	__stream_entry_table = .;
	KEEP(*(.stream_entry))
	__stream_entry_end = .;

#ifdef CONFIG_SECTION_OVERLAY
    *(EXCLUDE_FILE (*libdecsbc.a:* *libdecaac.a:* *libdecape.a:* *libdecflac.a:* *libdecamr.a:* *libdecwav.a:* *libdecmp3.a:* *libdecwma.a:* *libdecact.a:* *libencamr.a:* *libwav.a:* *libkshow.a:* *libbtdrv.a:* *libencpp.a:* *libencopus.a:* *libencspeex.a:* *libdnoise.a:* *libresample.a:*) .data)
    *(EXCLUDE_FILE (*libdecsbc.a:* *libdecaac.a:* *libdecape.a:* *libdecflac.a:* *libdecamr.a:* *libdecwav.a:* *libdecmp3.a:* *libdecwma.a:* *libdecact.a:* *libencamr.a:* *libwav.a:* *libkshow.a:* *libbtdrv.a:* *libencpp.a:* *libencopus.a:* *libencspeex.a:* *libdnoise.a:* *libresample.a:*) .data.*)
#else
	*(.data)
	*(".data.*")
#endif

#ifdef CONFIG_CUSTOM_RWDATA_LD
/* Located in project source directory */
#include <custom-rwdata.ld>
#endif

	} GROUP_DATA_LINK_IN(RAMABLE_REGION, ROMABLE_REGION)

#include <linker/common-ram.ld>

    __data_ram_end = .;

#ifdef CONFIG_SECTION_OVERLAY
    OVERLAY : NOCROSSREFS
    {
        .overlay.data.act {
			*libdecact.a:*(.data .data.*)
			*libresample.a:*(.data .data.*)
		}
	} GROUP_DATA_LINK_IN(RAMABLE_REGION, ROMABLE_REGION)
#endif
    SECTION_PROLOGUE(.RAM_BSS_DATA,(NOLOAD),) {
		__btcon_rom_bss_start = .;
	    KEEP(*(.bss.puppet_data))
		__btcon_rom_bss_end = ALIGN(4);
    } GROUP_LINK_IN(EXT_RAMABLE_REGION)

    SECTION_DATA_PROLOGUE(_BSS_SECTION_NAME,(NOLOAD),)
	{
        /*
         * For performance, BSS section is assumed to be 4 byte aligned and
         * a multiple of 4 bytes
         */
        . = ALIGN(4);
	__bss_start = .;

#ifdef CONFIG_SECTION_OVERLAY
    *(EXCLUDE_FILE (*libdecaac.a:* *libdecape.a:* *libdecflac.a:* *libdecamr.a:* *libdecwav.a:* *libdecmp3.a:* *libdecwma.a:* *libdecact.a:* *libencamr.a:* *libwav.a:* *libkshow.a:* *libencpp.a:* *libencopus.a:* *libencspeex.a:* *libdnoise.a:* *libCSpotter_16k24d.a:* *libresample.a:*) .bss)
    *(EXCLUDE_FILE (*libdecaac.a:* *libdecape.a:* *libdecflac.a:* *libdecamr.a:* *libdecwav.a:* *libdecmp3.a:* *libdecwma.a:* *libdecact.a:* *libencamr.a:* *libwav.a:* *libkshow.a:* *libencpp.a:* *libencopus.a:* *libencspeex.a:* *libdnoise.a:* *libCSpotter_16k24d.a:* *libresample.a:*) .bss.*)
    *(EXCLUDE_FILE (*libdecaac.a:* *libdecape.a:* *libdecflac.a:* *libdecamr.a:* *libdecwav.a:* *libdecmp3.a:* *libdecwma.a:* *libdecact.a:* *libencamr.a:* *libwav.a:* *libkshow.a:* *libencpp.a:* *libencopus.a:* *libencspeex.a:* *libdnoise.a:* *libCSpotter_16k24d.a:* *libresample.a:*) .scommon)
    *(EXCLUDE_FILE (*libdecaac.a:* *libdecape.a:* *libdecflac.a:* *libdecamr.a:* *libdecwav.a:* *libdecmp3.a:* *libdecwma.a:* *libdecact.a:* *libencamr.a:* *libwav.a:* *libkshow.a:* *libencpp.a:* *libencopus.a:* *libencspeex.a:* *libdnoise.a:* *libCSpotter_16k24d.a:* *libresample.a:*) COMMON)
#else
    *(.scommon)
    *(.bss)
    *(".bss.*")
    COMMON_SYMBOLS
#endif
        /*
         * As memory is cleared in words only, it is simpler to ensure the BSS
         * section ends on a 4 byte boundary. This wastes a maximum of 3 bytes.
		 */
	KEEP(*(SORT_BY_NAME(".stacknoinit.*")))
	__bss_end = ALIGN(4);
	} GROUP_DATA_LINK_IN(RAMABLE_REGION, RAMABLE_REGION)

    SECTION_PROLOGUE(_NOINIT_SECTION_NAME,(NOLOAD),)
        {
        /*
         * This section is used for non-initialized objects that
         * will not be cleared during the boot process.
         */
        *(.noinit)
        *(".noinit.*")
        *(.bottom_of_image_ram)
        *(.bottom_of_image_ram.*)
		KEEP(*(SORT_BY_NAME("._net_buf_pool.noinit.acl_in_pool")))
		KEEP(*(SORT_BY_NAME("._net_buf_pool.noinit.hci_rx_pool")))
		KEEP(*(SORT_BY_NAME("._net_buf_pool.noinit.hci_cmd_pool")))
		KEEP(*(SORT_BY_NAME("._net_buf_pool.noinit.acl_tx_pool")))
		KEEP(*(SORT_BY_NAME("._net_buf_pool.noinit.prep_pool")))
		KEEP(*(SORT_BY_NAME("._net_buf_pool.noinit.br_sig_pool")))
		KEEP(*(SORT_BY_NAME("._net_buf_pool.noinit.sdp_pool")))
        } GROUP_LINK_IN(RAMABLE_REGION)


#ifdef CONFIG_SECTION_OVERLAY
        OVERLAY : NOCROSSREFS
        {
            .overlay.bss.act {
				*libdecact.a:*(.bss .bss.* .scommon COMMON)
				*libresample.a:*(.bss .bss.* .scommon COMMON)
			}
        }  GROUP_LINK_IN(RAMABLE_REGION)
#endif


    /* Define linker symbols */
#ifdef CONFIG_APP_USED_MEM_PAGE
    __ram_mpool0_start = ORIGIN(ram_mpool);
    __ram_mpool0_size = LENGTH(ram_mpool);
    __ram_mpool0_num = (LENGTH(ram_mpool) + IBANK_SIZE - 1) / IBANK_SIZE;
#endif

    __ram_dsp_start = ORIGIN(ram_dsp);
    __ram_dsp_size = LENGTH(ram_dsp);


    SECTION_DATA_PROLOGUE(.RAM_SRAM_BSS,(NOLOAD),) {
        *(.SRAM_BSS_DSP*)
		*(.SRAM_BSS_SQT*)
		*(.SRAM_NO_INIT*)
		*(.SRAM_BSS_SHARE_BUFF*)
    } GROUP_LINK_IN(EXT_RAMABLE_REGION)


 	SECTION_DATA_PROLOGUE(.ramfunc.data,,)
	{
		__ramfunc_ram_start = .;
	    /* sections for function run in ram when XIP is enabled */
		_image_text_ramfunc_start = .;
	    KEEP(*(SORT_BY_NAME(".ramfunc.*")))
	    /* BT controller fix code */
	    KEEP(*(.BTCON_FIX_RODATA))
		KEEP(*(SORT_BY_NAME(".coredata.*")))
		KEEP(*(.BTCON_FIX_DATA))

#if defined(CONFIG_GEN_SW_ISR_TABLE) && defined(CONFIG_IRQ_STAT)
	    . = ALIGN(4);
	    KEEP(*(SW_ISR_TABLE))
#endif
		_image_text_ramfunc_end = .;
		__ramfunc_ram_end = .;

	}GROUP_DATA_LINK_IN(EXT_RAMABLE_REGION, ROMABLE_REGION)
    __ramfunc_rom_start = LOADADDR(.ramfunc.data);

    SECTION_DATA_PROLOGUE(.RAM_PCMBUF,(NOLOAD),) {
        *(.AUDIO_PCM_BUF*)
    } GROUP_LINK_IN(ram_hw_pcmbuf)

    SECTION_PROLOGUE(.RAM_PCMBUF,(NOLOAD),) {
        *(.ESD_DATA*)
    } GROUP_LINK_IN(EXT_RAMABLE_REGION)

    SECTION_DATA_PROLOGUE(.RAM_POOL2,(NOLOAD),) {
        *(.LINEIN_BUF*)
        *(.BTCON_TO_HOST_BUF*)
    } GROUP_LINK_IN(ram_pool2)

    SECTION_DATA_PROLOGUE(.BTCON_MEMPOOL,(NOLOAD),) {
        *(.BTCON_MEMPOOL*)
    } GROUP_LINK_IN(EXT_RAMABLE_REGION)

	_image_ram_end = .;
    _end = .; /* end of image */

    GROUP_END(RAMABLE_REGION)

#ifdef CONFIG_CUSTOM_SECTIONS_LD
/* Located in project source directory */
#include <custom-sections.ld>
#endif

#ifdef CONFIG_GEN_ISR_TABLES
#include <linker/intlist.ld>
#endif
    }

