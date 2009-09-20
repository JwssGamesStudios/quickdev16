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
#include <stdint.h>
#include <string.h>
#include <avr/io.h>
#include <avr/interrupt.h>

#include "pwm.h"
#include "debug.h"
#include "info.h"
#include "sram.h"


#define PWM_SINE_MAX 255
uint8_t pwm_sine_table[] = {
0x7f,0x82,0x85,0x88,0x8b,0x8e,0x91,0x94,0x97,0x9b,0x9e,0xa1,0xa4,0xa7,0xaa,0xad,
0xaf,0xb2,0xb5,0xb8,0xbb,0xbe,0xc0,0xc3,0xc6,0xc8,0xcb,0xcd,0xd0,0xd2,0xd4,0xd7,
0xd9,0xdb,0xdd,0xdf,0xe1,0xe3,0xe5,0xe7,0xe9,0xea,0xec,0xee,0xef,0xf0,0xf2,0xf3,
0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xfb,0xfb,0xfc,0xfc,0xfd,0xfd,0xfd,0xfd,0xfd,
0xfd,0xfd,0xfd,0xfd,0xfd,0xfc,0xfc,0xfb,0xfb,0xfa,0xf9,0xf8,0xf7,0xf6,0xf5,0xf4,
0xf3,0xf2,0xf0,0xef,0xee,0xec,0xea,0xe9,0xe7,0xe5,0xe3,0xe1,0xdf,0xdd,0xdb,0xd9,
0xd7,0xd4,0xd2,0xd0,0xcd,0xcb,0xc8,0xc6,0xc3,0xc0,0xbe,0xbb,0xb8,0xb5,0xb2,0xaf,
0xad,0xaa,0xa7,0xa4,0xa1,0x9e,0x9b,0x97,0x94,0x91,0x8e,0x8b,0x88,0x85,0x82,0x7f,
0x7c,0x79,0x76,0x73,0x70,0x6d,0x6a,0x67,0x63,0x60,0x5d,0x5a,0x57,0x54,0x51,0x4f,
0x4c,0x49,0x46,0x43,0x40,0x3e,0x3b,0x38,0x36,0x33,0x31,0x2e,0x2c,0x2a,0x27,0x25,
0x23,0x21,0x1f,0x1d,0x1b,0x19,0x17,0x15,0x14,0x12,0x10,0x0f,0x0e,0x0c,0x0b,0x0a,
0x09,0x08,0x07,0x06,0x05,0x04,0x03,0x03,0x02,0x02,0x01,0x01,0x01,0x01,0x01,0x01,
0x01,0x01,0x01,0x01,0x02,0x02,0x03,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,
0x0c,0x0e,0x0f,0x10,0x12,0x14,0x15,0x17,0x19,0x1b,0x1d,0x1f,0x21,0x23,0x25,0x27,
0x2a,0x2c,0x2e,0x31,0x33,0x36,0x38,0x3b,0x3e,0x40,0x43,0x46,0x49,0x4c,0x4f,0x51,
0x54,0x57,0x5a,0x5d,0x60,0x63,0x67,0x6a,0x6d,0x70,0x73,0x76,0x79,0x7c,0x7f
};

 volatile uint8_t pwm_setting;                    // Einstellungen für die einzelnen PWM-Kanäle
 volatile uint8_t pwm_overflow;                    // Einstellungen für die einzelnen PWM-Kanäle
 volatile uint8_t pwm_idx;                    // Einstellungen für die einzelnen PWM-Kanäle
 
 ISR(TIMER2_COMPA_vect) {
    static uint8_t pwm_cnt=0;
    OCR2A += (uint16_t)T_PWM;
        
    if (pwm_setting> pwm_cnt) 
        led_pwm_on();
    else
        led_pwm_off();
    
    if (pwm_cnt==(uint8_t)(PWM_STEPS-1))
        pwm_cnt=0;
    else
        pwm_cnt++;
    if (!pwm_overflow++){
        pwm_setting = pwm_sine_table[pwm_idx++];
        if (PWM_SINE_MAX == pwm_idx)
            pwm_idx = 0;
    }
}

void pwm_set(uint8_t val) {
    pwm_setting = val;
}

void pwm_stop(void) {
    while(pwm_setting!=0xfd);
    //cli();
    TIMSK2 = 0;
    //sei();       
}

void pwm_init(void) {
    pwm_setting = 0x7f;
    pwm_overflow = 0;
    //cli();
    TCCR2B = 1;         
    TIMSK2 |= (1<<OCIE2A);       
    //sei();                  
}