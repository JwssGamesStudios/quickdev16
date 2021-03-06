/*
 * =====================================================================================
 *
 * ________        .__        __    ________               ____  ________
 * \_____  \  __ __|__| ____ |  | __\______ \   _______  _/_   |/  _____/
 *  /  / \  \|  |  \  |/ ___\|  |/ / |    |  \_/ __ \  \/ /|   /   __  \
 * /   \_/.  \  |  /  \  \___|    <  |    `   \  ___/\   / |   \  |__\  \
 * \_____\ \_/____/|__|\___  >__|_ \/_______  /\___  >\_/  |___|\_____  /
 *        \__>             \/     \/        \/     \/                 \/
 *
 *                                  www.optixx.org
 *
 *
 *        Version:  1.0
 *        Created:  07/21/2009 03:32:16 PM
 *         Author:  david@optixx.org
 *
 * =====================================================================================
 */


#include <stdlib.h>
#include <stdint.h>
#include <avr/io.h>
#include <util/delay.h>         /* for _delay_ms() */


#include "config.h"
#include "sram.h"
#include "uart.h"
#include "debug.h"
#include "info.h"

uint32_t addr_current = 0;
uint32_t addr_stash = 0;

void sram_init(void)
{
    /*-------------------------------------------------*/

    DDRA = 0x00;
    PORTA = 0x00;

    /*-------------------------------------------------*/

    DDRC |= ((1 << AVR_ADDR_LATCH_PIN)
             | (1 << AVR_ADDR_SCK_PIN)
             | (1 << AVR_ADDR_SER_PIN)
             | (1 << AVR_ADDR_LOAD_PIN)
             | (1 << AVR_ADDR_UP_PIN));

    DDRC &= ~((1 << SNES_WR_PIN)
              | (1 << AVR_BTLDR_EN_PIN));

    PORTC &= ~((1 << AVR_ADDR_LATCH_PIN)
               | (1 << AVR_ADDR_SCK_PIN)
               | (1 << SNES_WR_PIN));


    PORTC |= ((1 << AVR_ADDR_UP_PIN)
              | (1 << AVR_ADDR_LOAD_PIN));

    // | (1 << SNES_WR_PIN));
    /*-------------------------------------------------*/

    DDRB |= ((1 << AVR_RD_PIN)
             | (1 << AVR_WR_PIN)
             | (1 << AVR_CS_PIN)
             | (1 << SNES_IRQ_PIN));


    PORTB |= ((1 << AVR_RD_PIN)
              | (1 << AVR_WR_PIN)
              | (1 << AVR_CS_PIN)
              | (1 << SNES_IRQ_PIN));

    /*-------------------------------------------------*/


    DDRD |= ((1 << AVR_SNES_SW_PIN)
             | (1 << HI_LOROM_SW_PIN)
             | (1 << SNES_WR_EN_PIN));

    PORTD |= (1 << HI_LOROM_SW_PIN);

    PORTD &= ~((1 << AVR_SNES_SW_PIN)
               | (1 << SNES_WR_EN_PIN));

    /*-------------------------------------------------*/


}

void sreg_set(uint32_t addr)
{
    uint8_t i = 24;
    debug_P(DEBUG_SREG, PSTR("sreg_set: addr=0x%08lx"), addr);
    while (i--) {
        if ((addr & (1L << i))) {
            debug_P(DEBUG_SREG, PSTR("1"));
            AVR_ADDR_SER_PORT |= (1 << AVR_ADDR_SER_PIN);
        } else {
            AVR_ADDR_SER_PORT &= ~(1 << AVR_ADDR_SER_PIN);
            debug_P(DEBUG_SREG, PSTR("0"));
        }
        AVR_ADDR_SCK_PORT |= (1 << AVR_ADDR_SCK_PIN);
        AVR_ADDR_SCK_PORT &= ~(1 << AVR_ADDR_SCK_PIN);
    }
    debug_P(DEBUG_SREG, PSTR("\n"));
    AVR_ADDR_LATCH_PORT |= (1 << AVR_ADDR_LATCH_PIN);
    AVR_ADDR_LATCH_PORT &= ~(1 << AVR_ADDR_LATCH_PIN);

    counter_load();

}

void sram_bulk_addr_save()
{
    addr_stash = addr_current;
    debug_P(DEBUG_SRAM, PSTR("sram_bulk_addr_save: addr=0x%08lx\n\r"),
            addr_stash);
}

inline void sram_bulk_addr_restore()
{
    debug_P(DEBUG_SRAM, PSTR("sram_bulk_addr_restore: addr=0x%08lx\n\r"),
            addr_stash);
    sram_bulk_write_start(addr_stash);
}


void sram_bulk_read_start(uint32_t addr)
{
    debug_P(DEBUG_SRAM, PSTR("sram_bulk_read_start: addr=0x%08lx\n\r"), addr);

    addr_current = addr;

    avr_data_in();

    AVR_CS_PORT &= ~(1 << AVR_CS_PIN);
    AVR_WR_PORT |= (1 << AVR_WR_PIN);
    AVR_RD_PORT |= (1 << AVR_RD_PIN);

    sreg_set(addr);

    AVR_RD_PORT &= ~(1 << AVR_RD_PIN);
    asm volatile ("nop");
    asm volatile ("nop");
    asm volatile ("nop");
    asm volatile ("nop");
    asm volatile ("nop");
    asm volatile ("nop");

}

inline void sram_bulk_read_next(void)
{
    addr_current++;
    AVR_RD_PORT |= (1 << AVR_RD_PIN);
    counter_up();
    AVR_RD_PORT &= ~(1 << AVR_RD_PIN);

    asm volatile ("nop");
    asm volatile ("nop");
    asm volatile ("nop");
    asm volatile ("nop");
    asm volatile ("nop");
    asm volatile ("nop");

}


inline uint8_t sram_bulk_read(void)
{
    return AVR_DATA_PIN;
}

void sram_bulk_read_end(void)
{
    debug_P(DEBUG_SRAM, PSTR("sram_bulk_read_end:\n"));

    AVR_RD_PORT |= (1 << AVR_RD_PIN);
    AVR_CS_PORT |= (1 << AVR_CS_PIN);
    avr_data_in();
}

uint8_t sram_read(uint32_t addr)
{
    uint8_t byte;
    debug_P(DEBUG_SRAM_RAW, PSTR("sram_read: addr=0x%08lx\n\r"), addr);

    avr_data_in();

    AVR_CS_PORT &= ~(1 << AVR_CS_PIN);

    AVR_WR_PORT |= (1 << AVR_WR_PIN);
    AVR_RD_PORT |= (1 << AVR_RD_PIN);

    sreg_set(addr);

    AVR_RD_PORT &= ~(1 << AVR_RD_PIN);

    asm volatile ("nop");
    asm volatile ("nop");
    asm volatile ("nop");
    asm volatile ("nop");
    asm volatile ("nop");
    asm volatile ("nop");

    byte = AVR_DATA_PIN;

    AVR_RD_PORT |= (1 << AVR_RD_PIN);
    AVR_CS_PORT |= (1 << AVR_CS_PIN);

    avr_data_in();
    return byte;

}

uint16_t sram_read16_be(uint32_t addr)
{
    uint8_t hi = sram_read(addr);
    uint8_t lo = sram_read(addr + 1);
    return (hi << 8 | lo);
}

void sram_bulk_write_start(uint32_t addr)
{
    debug_P(DEBUG_SRAM, PSTR("sram_bulk_write_start: addr=0x%08lx\n\r"), addr);
    addr_current = addr;

    avr_data_out();

    AVR_CS_PORT &= ~(1 << AVR_CS_PIN);
    AVR_WR_PORT |= (1 << AVR_WR_PIN);
    AVR_RD_PORT |= (1 << AVR_RD_PIN);

    sreg_set(addr);

}

inline void sram_bulk_write_next(void)
{
    addr_current++;
    counter_up();
}

inline void sram_bulk_write(uint8_t data)
{
    AVR_WR_PORT &= ~(1 << AVR_WR_PIN);
    AVR_DATA_PORT = data;
    AVR_WR_PORT |= (1 << AVR_WR_PIN);
}

void sram_bulk_write_end(void)
{
    debug_P(DEBUG_SRAM, PSTR("sram_bulk_write_end:"));
    AVR_WR_PORT |= (1 << AVR_WR_PIN);
    AVR_CS_PORT |= (1 << AVR_CS_PIN);
    avr_data_in();
}


void sram_write(uint32_t addr, uint8_t data)
{
    debug_P(DEBUG_SRAM_RAW, PSTR("sram_write: addr=0x%08lx data=%x\n\r"), addr,
            data);

    avr_data_out();

    AVR_CS_PORT &= ~(1 << AVR_CS_PIN);
    AVR_WR_PORT |= (1 << AVR_WR_PIN);
    AVR_RD_PORT |= (1 << AVR_RD_PIN);

    sreg_set(addr);

    AVR_WR_PORT &= ~(1 << AVR_WR_PIN);


    AVR_DATA_PORT = data;


    AVR_WR_PORT |= (1 << AVR_WR_PIN);
    asm volatile ("nop");
    asm volatile ("nop");
    asm volatile ("nop");
    asm volatile ("nop");
    asm volatile ("nop");
    asm volatile ("nop");
    AVR_CS_PORT |= (1 << AVR_CS_PIN);

    avr_data_in();
}


void sram_bulk_copy_from_buffer(uint32_t addr, uint8_t * src, uint32_t len)
{

    uint32_t i;
    uint8_t *ptr = src;
    debug_P(DEBUG_SRAM,
            PSTR
            ("sram_bulk_copy_from_buffer: addr=0x%08lx src=0x%p len=%li\n\r"),
            addr, src, len);
    sram_bulk_write_start(addr);
    for (i = addr; i < (addr + len); i++) {
        sram_bulk_write(*ptr);
        // hack
        if ((i + 1) < (addr + len))
            sram_bulk_write_next();
        ptr++;
    }
    sram_bulk_write_end();
}

void sram_bulk_copy_into_buffer(uint32_t addr, uint8_t * dst, uint32_t len)
{

    uint32_t i;
    // uint8_t *ptr = dst;
    debug_P(DEBUG_SRAM,
            PSTR
            ("sram_bulk_copy_into_buffer: addr=0x%08lx dst=0x%p len=%li\n\r"),
            addr, dst, len);
    sram_bulk_read_start(addr);
    for (i = addr; i < (addr + len); i++) {
        dst[i] = sram_bulk_read();
        sram_bulk_read_next();
    }
    sram_bulk_read_end();
}

void sram_bulk_set(uint32_t addr, uint32_t len, uint8_t value)
{
    uint32_t i;
    debug_P(DEBUG_SRAM, PSTR("sram_bulk_set: addr=0x%08lx len=%li\n\r"), addr,
            len);
    sram_bulk_write_start(addr);
    for (i = addr; i < (addr + len); i++) {
        if (0 == i % 0xfff)
            debug_P(DEBUG_SRAM, PSTR("sram_bulk_set: addr=0x%08lx\n\r"), i);
        sram_bulk_write(value);
        sram_bulk_write_next();
    }
    sram_bulk_write_end();
}
