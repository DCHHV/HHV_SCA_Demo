/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2019 KBEmbedded and DEF CON Hardware Hacking Village */

/*
 * Basic pinmap reference
 * RA0: TXD
 * RA1: RXD
 * RA2: Analog button input
 * RA3: Unused
 * RA4: Green LED#
 * RA5: Red LED#
 */

#include <xc.h>

//#define ECHO_UART             // Uncomment to enable echo of recv'ed chars

#define _XTAL_FREQ 1000000   // Oscillator frequency.

#pragma config FOSC = INTOSC  // INTOSC oscillator: I/O function on CLKIN pin.
#pragma config WDTE = OFF     // Watchdog Timer disable.
#pragma config PWRTE = OFF    // Power-up Timer enbable.
#pragma config MCLRE = ON     // MCLR/VPP pin function is MCLR.
#pragma config CP = OFF       // Program memory code protection disabled.
#pragma config BOREN = ON     // Brown-out Reset enabled.
#pragma config CLKOUTEN = OFF // CLKOUT function is disabled; I/O or oscillator function on the CLKOUT pin.
#pragma config WRT = OFF      // Flash Memory Write protection off.
#pragma config STVREN = ON    // Stack Overflow or Underflow will cause a Reset.
#pragma config BORV = LO      // Brown-out Reset Voltage (Vbor), low trip point selected.
#pragma config LVP = OFF      // High-voltage on MCLR/VPP must be used for programming.

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern uint8_t proper_compare(void);

/* Make most of the important things global. Saves program space not having to
 * push/pop values for function calls.
 * In some situations, all global could be worse due to bank switching code.
 * However this code has been tested as globals vs function arguments, and it
 * was found that globals can save >60 words compared to function args.
 */
volatile uint8_t uart_byte;
volatile uint8_t adc_byte;
volatile bit adc_flag;
volatile bit uart_flag;
volatile bit blink_flag;
volatile bit newline_flag;
bit did_something = 0;  //Global so it is optimized with other bits
unsigned char cmdstring[9] = {0};

uint8_t pincnt;
uint8_t stage;
uint8_t seed;

/* These are put in common RAM. This helps reduce programming strain for the
 * stage 3 section with assembly. No worrying about banking for these since
 * 0x70-0x7F are available in any bank.
 * Probably not the most efficient, but, makes the ASM for the compare easier.
 */

volatile uint8_t pin_input[4] __at(0x70);
volatile uint8_t pin[4] __at(0x74);
volatile uint8_t loop __at(0x78);
volatile uint8_t cmp __at(0x79);

#define LED_G 4
#define LED_R 5

extern const char intro_text[];
extern const char stage_0_text[];
extern const char stage_1_text[];
extern const char stage_2_text[];
extern const char stage_3_text[];
extern const char completed_text[];

/* For some reason, this array cannot have its members defined at compile time
 * without a call to "puts(stage_text[<value>])" SOMEWHERE in the source. The
 * compromise
 */
const char *stage_text[5];

void init_pic() {
  OSCCON = 0b01011010; // 1 Mhz internal oscillator. 250khz instruction time.
  ANSELA = 0b00000100; // Analog enable on RA2 for button input
  TRISAbits.TRISA2 = 1;
  
  /* LEDs as outputs, active low */
  PORTAbits.RA4 = 1;
  PORTAbits.RA5 = 1;
  TRISAbits.TRISA4 = 0;
  TRISAbits.TRISA5 = 0;
}

void init_uart() {
  TRISAbits.TRISA1 = 1; // UART RX pin an input.
  TRISAbits.TRISA0 = 0; // UART TX pin an output.

  // 9600 bps, 1 MHz oscillator.
  SPBRGH = 0x00;
  SPBRGL = 0x19;

  // 16-bit Baud Rate Generator
  BAUDCONbits.BRG16 = 1;

  TXSTAbits.TXEN = 1; // Transmit enabled.
  TXSTAbits.SYNC = 0; // Enable asynchronous mode.
  TXSTAbits.BRGH = 1; // High speed.

  RCSTAbits.SPEN = 1; // Enable serial port.

}

void init_adc(void) {
    /* ANSEL/TRIS already set by init_pic() */
    
    /* Set channel selection, AN2 */
    ADCON0bits.CHS = 0b00010;
    
    /* Set VREF to VDD */
    ADCON1bits.ADPREF = 0;
    
    /* Set conversion clock, Fosc/64, ~4 us conversion time */
    ADCON1bits.ADCS = 0b100;
    
    /* 10-bit left-justified */
    ADCON1bits.ADFM = 0;
    
    /* Enable ADC */
    ADCON0bits.ADON = 1;
    
    /* Enable ADC conversion interrupt */
    PIR1bits.ADIF = 0;
    PIE1bits.ADIE = 1;
    
    /* Enable timed conversion, ~10ms, from Timer0 */
    ADCON2bits.TRIGSEL = 0b0011;
    
    adc_flag = 0;
}

/* Set up Timer0 with 1:8 prescaler
 * At 1 MHz, this is roughly 8.2 ms, a close enough timing for ADC
 */
void init_timer0(void) 
{
    TMR0 = 0;
    OPTION_REGbits.PSA = 0;
    OPTION_REGbits.PS = 0b010;
    OPTION_REGbits.TMR0CS = 0;
}

void putch(unsigned char byte) {
  // Wait until no char is being held for transmission in the TXREG.
  while (!PIR1bits.TXIF) {
    continue;
  }
  // Write the byte to the transmission register.
  TXREG = byte;
}

/* Main interrupt handler. Only interrupts we have are from UART RX and ADC.
 * ADC acquisition is set up on a timer. Bitflags are used to keep track of
 * states. The ISR only ever sets these flags, the rest of the code will only
 * clear them.
 */
void interrupt interrupt_handler() {
  if (PIR1bits.RCIF && PIE1bits.RCIE) {
    // EUSART receiver enabled and unread char in receive FIFO.
    if (RCSTAbits.OERR) {
      // Overrun error.
      RCSTAbits.CREN = 0;          // Clear the OERR flag.
      uart_byte = RCREG; // Clear any framing error.
      RCSTAbits.CREN = 1;
    } else {
      uart_byte = RCREG;
      if(uart_byte == '\r') newline_flag = 1;
      else uart_flag = 1;
    }
  }
  
  /* ADC, PIR1bits.ADIF must be manually cleared */
  if (PIR1bits.ADIF && PIE1bits.ADIE) {
      PIR1bits.ADIF = 0;
      adc_byte = ADRESH;
      adc_flag = 1;
  }
}

void led_toggle(uint8_t led) {
    LATA ^= (uint8_t)(1 << led);
}

void led_blink(uint8_t led) {
    led_toggle(led);
    __delay_ms(50);
    led_toggle(led);
}

void led_flicker(uint8_t led)
{
    uint8_t i;
    
    for (i = 10; i != 0; i--) {
        led_blink(led);
        __delay_ms(50);
    }
}

/* This function is a "bottom half" of the ADC. In it, the value of the last
 * ADC conversion is parsed. The buttons on the device are set up with a
 * resistor ladder; each button press returning roughly half the value of the
 * next one up. The ADC will be at max voltage when no button press, and ground
 * when the leftmost button is pressed. This is parsed to a sane value, 0 - 3,
 * matching each button value. A value of 255 means that no buttons are being
 * pushed.
 * 
 * This function requires two consecutive reads to match in terms of value.
 * Once two matching readings are taken, the function ignores all other input
 * until all buttons are released for a sample. At which time the return value
 * of this function is 1 to indicate a valid read. All other returns of this
 * function is 0.
 */
uint8_t adc_parse(void) {
    static bit repeated;
    static bit valid_press;
    uint8_t ret = 0;
    uint8_t str_val;
    
    /* This can waste time by setting str_val multiple times, but it saves some
     * code space vs ranged if statements or else if's. This is outside of the
     * critical time section also. */
    str_val = 0;
    if (adc_byte > 0x3F) str_val = 1;
    if (adc_byte > 0x94) str_val = 2;
    if (adc_byte > 0xB3) str_val = 3;
    if (adc_byte > 0xE4) str_val = 255; //Released button
    
    if (repeated && (pin_input[pincnt] == str_val)) {
        repeated = 0;
        valid_press = 1;
        led_toggle(LED_G);        
    } 
    
    if (!valid_press && (str_val != 255)) {
        pin_input[pincnt] = str_val;
        repeated = 1;
    }
    
    if (valid_press && str_val == 255) {
        valid_press = 0;
        ret = 1;
        led_toggle(LED_G);
    }
    
    return ret;
}

/* This function is used to compare the input PIN to the expected PIN. The
 * pin_input variable is the last input complete pin, while the pin variable
 * is the expected PIN.
 * 
 * Each stage is represented, and each stage performs a different compare
 * function. See the switch statement below for more information
 * 
 * Returns a 1 if the strings match, a 0 otherwise.
 */
uint8_t kp_compare()
{
    uint8_t i;
    uint8_t ret = 1;
    
    switch(stage) {
        /* Stage 0 is odd because each button input is tested as it comes in.
         * This never happens in the real world and is stupid. But it is meant
         * to exaggerate and show how the next stage is just as naive.
         */
        case 0:
            if (pin_input[pincnt-1] != pin[pincnt-1]) ret = 0;
            break;

        /* Stage 1 is a common naive password compare. Starting at one end, it
         * compares each place of the PIN with the expected value. At the first
         * incorrect value, the function returns.
         *
         * This allows one to identify the pin, place by place, in less than
         * 4+4+4+4 guesses.
         */
        case 1:
            for (i = 0; i < pincnt; i++) {
                if (pin_input[i] != pin[i]) return 0;
            }
            break;

        /* Stage 2 is meant to be a naive fix to the above problem. A value is
         * set at the start of the loop. In this loop, every digit of the PIN
         * is compared against what is expected. If the digit is incorrect, then
         * the value is cleared and the PIN is incorrect.
         *
         * While this does compare each digit, unlike above, the time is still
         * different for a matching and non-matching digit. This time difference
         * allows one to narrow in on how many correct and incorrect
         * digits there are.
         *
         * Note that this architecture makes this specific scenario actually
         * kind of difficult. Compilers optimized for speed with direct compare
         * and jump instructions could opt to compare and jump if false, or
         * compare, execute the next instruction 'ret = 0', and then jump. The
         * idea here is that the paths of the if() statement are different
         * lengths.
         *
         * A nop instruction was added below in order to ensure this function
         * is broken in the intended way. The 'ret = 0' instruction is basically
         * free with the way the PIC tests bit set/clr and skips instructions.
         * Which causes the true and false paths to be equal in length.
         * This behavior can be seen in the proper_compare() function.
         */
        case 2:
            for (i = 0; i < pincnt; i++) {
                if (pin_input[i] != pin[i]) {
                    ret = 0;
                    asm("nop");
                }
            }
            break;

        /* Stage 3 is a correct way to do PIN compare. It is hand tuned ASM
         * (see the function in compare.s) that checks every place, and whether
         * the digit matches or not, takes the same amount of time.
         *
         * The assembly will add 1 to a variable for every non-matching place.
         * At the end, a single compare against 0 is used. If that value was 0,
         * it means all four places were correct. If it is nonzero, it means
         * not all four places were correct.
         *
         * Using this method and carefully understanding the critical time
         * sections, it is possible to create a routine that will leak no info
         * about how many digits were correct or incorrect.
         *
         * Stage 3, while implemented correctly, is readily defeated due to the
         * poor PRNG that drives the PIN generation.
         */
        case 3:
            if (proper_compare() != 0) ret = 0;
            break;
                        
        default:
            break;
    }
    
    return ret;
}

/* This function is essentially a Galois LFSR for pseudorandom number generation
 * This is like any other basic PRNG, in that once the seed or any value that
 * comes from it is known, as well as how the PRNG steps happen, the entire
 * stream can then be predicted.
 * 
 * The seed is generated at the start based on a timer and human interaction.
 * This delay allows for a somewhat random seed, however it is only 8-bits.
 * 
 * The PIN of each stage is derived directly from the current PRNG value. The
 * 8-bit value is broken in to 4x 2-bit quantities, the PIN digit value is the
 * value of each 2-bit quantity. Hence, a direct mapping of 0-3.
 * 
 * This function directly writes the expected PIN value to its array each call.
 */
void generate_pin(void)
{
    uint8_t seed_copy, i;
    seed = seed >> 1;
    if (CARRY) seed ^= 0xB8;
    seed_copy = seed;
    for (i = 3; i != 0; i--) {
        pin[i] = seed_copy & 0x3;
        seed_copy >>= 2;
    }
    pin[0] = seed_copy & 0x3;
}

/* This function reads const char arrays from program flash. The program flash
 * of this PIC12F1572 uses 14-bit words. Two 7-bit ASCII characters can fit in
 * each word.
 * 
 * The high byte is the first character, the low byte is the second. It is
 * guaranteed that at the end of each array, both high and low bytes will be
 * NULL, or low byte will be NULL.
 * 
 * Due to the limitation of this specific PIC, the indirect registers cannot be
 * used to read all 14-bits of each program word. It can only read the lowest
 * 8-bits. We need to use the built-in flash access routines to get access to
 * all 14-bits in each word.
 * 
 * Due to the limitation of PIC xc8, these packed arrays cannot be represented
 * directly in C. An ASM include sets up all of these words. This include can
 * be generated with the tools/pack_strings.c program.
 */
void print_packed(const char *ptr)
{
    unsigned char buf;
    
    /* The CFGS bit should already be cleared from hardware */
    PMCON1 &= ~(_PMCON1_CFGS_MASK);
    PMADR = ptr;

    do {
        PMCON1bits.RD = 1;
        /* Two dummy operations as required by the flash program read process.
         * RD is not clearable by software, using a NOP can cause the compiler
         * to pop out extra instructions to guard inline ASM commands. Therefore
         * clearing the RD command does nothing but waste time, but not too much
         * time.
         */
        PMCON1bits.RD = 0;
        PMCON1bits.RD = 0;
        
        /* Read high byte first! */
        buf = ((PMDAT >> 7) & 0x7F);
        putch(buf);
        buf = (PMDAT & 0x7F);
        putch(buf);

        /* Increment flash pointer */
        PMADR++;
    } while (buf);
}

/* Clear the terminal
 * Uses VT100 commands to scroll the screen and put the cursor @ home.
 * This helps to hide the "Seed:" output in the terminal and works on at least
 * PuTTY and picocom.
 */
void clr_term(void) {
    putch(0x1B);
    putch('[');
    putch('2');
    putch('J');
    putch(0x1B);
    putch('[');
    putch('H');
}

/* Cleans the terminal, prints the intro text for the next stage, and generates
 * the PIN for the next stage.
 */
void print_stage(uint8_t stage)
{
    clr_term();
    print_packed(stage_text[stage]);
    generate_pin();
}

int main() {
    uint8_t recvcnt;
    uint8_t i;
    volatile uint8_t seed_volatile;

    stage = 0;
    pincnt = 0;
    recvcnt = 0;
    
    init_pic();
    init_uart();
    init_adc();
    init_timer0();

    stage_text[0] = stage_0_text;
    stage_text[1] = stage_1_text;
    stage_text[2] = stage_2_text;
    stage_text[3] = stage_3_text;
    stage_text[4] = completed_text;
    
    // Enable serial RX and general interrupts
    RCSTAbits.CREN = 1;
    INTCONbits.GIE = 1;
    INTCONbits.PEIE = 1;

    /* Enable the receive interrupt, expect spurious data, immediately clear
     * UART flags. If there are multiple characters received that are valid,
     * then the ISR should keep firing until the RX FIFO is clear, and then
     * return. At that point, clear both of the associated flags, and then
     * start our main loop.
     */
    PIE1bits.RCIE = 1;
    uart_flag = 0;
    newline_flag = 0;
    
    /* Clear the terminal, print the intro text, and wait for a newline.
     * This newline is our delay for setting the PRNG seed. Once a newline is
     * caught, the seed is set from TMR0 which is running at ~8.2 ms and is an
     * 8-bit quantity.
     * 
     * A seed of 0x00 is bad as it means all mutations after it will also be
     * 0x00. Add 1 in this case. This does mean that there are 255 seed values,
     * two of them are technically repeated, and there will only ever be 255
     * PIN values as a PIN of 0000 will never happen.
     */
    clr_term();
    print_packed(intro_text);
    while(!newline_flag);
    newline_flag = 0;
    seed = TMR0;
    if (!seed) seed++;
    
    /* Doing this and printing the seed in binary vs using printf to formate
     * saves about 220 words overall.
     */
    seed_volatile = seed;
    putch('S');
    putch('e');
    putch('e');
    putch('d');
    putch(':');
    putch(' ');
    for (i = 0; i < 8; i++) {
        seed_volatile = seed_volatile << 1;
        putch(CARRY + 0x30);
    }

    print_stage(0);
    
    while (1) {
#if UART
        if (uart_flag) {
#ifdef ECHO_UART
            putch(uart_byte);
#endif
            uart_flag = 0;
            if(recvcnt != 8) cmdstring[recvcnt++] = uart_byte;
        }
#endif
        
        if (adc_flag) {
            adc_flag = 0;

            /* Check to see if a new valid button press was received. 
             * Since stage 0 is awkward and really naive, we need to compare
             * at every change. No matter the stage, the kpcnt would only ever
             * increase by 1 if a new byte came in. This new byte has already
             * been added to the buffer.
             * The only thing thats really different, is stage 0 when
             * kpcnt is less than 4.
             * Once kpcnt == 4, then stage 0 behaves exactly the same as
             * stage 1 in terms of timing. However its still only comparing a
             * single digit at a time as it comes in.
             */
            
            /* if a valid input was detected from adc_parse, increment the
             * current pin counter. The valid digit has already been placed in
             * the pin_input[] array thanks to adc_parse().
             * 
             * NOTE: This section needs to be nested like this!
             * This allows stage 0 to call kp_compare ONLY when a new PIN digit
             * was read in!
             */
            if (adc_parse()) {
                pincnt++;
                /* ADC/UART interrupts do not _need_ to be disabled here. This
                 * stage can be visually watched to determine the correct PIN.
                 */
                if (stage == 0 && pincnt < 4) {
                    if (!kp_compare()) {
                        led_flicker(LED_R);
                        pincnt = 0;
                    }
                }
            }

            if (pincnt == 4) {
                /* Disable UART and ADC interrupts during the compare routines
                 * to not allow them to throw off the timing.
                 */
                PIE1bits.ADIE = 0;
                PIE1bits.RCIE = 0;
                
                if (kp_compare()) {
                    led_toggle(LED_G);
                    __delay_ms(2000);
                    led_toggle(LED_G);
                    stage++;
                    print_stage(stage);
                } else {
                    led_flicker(LED_R);
                }
                
                /* Clear our the pin_input buffer and re-enable ADC/UART RX */
                while (--pincnt) pin_input[pincnt] = 0;
                
                PIR1bits.RCIF = 0;
                PIE1bits.RCIE = 1;
                
                PIR1bits.ADIF = 0;
                PIE1bits.ADIE = 1;
            }
        }
#if UART
        if (newline_flag) {
            /* Clean up buffer and reset for next command */
            newline_flag = 0;
            while(recvcnt) cmdstring[--recvcnt] = 0;
        }
#endif
    }
    return (EXIT_SUCCESS);
}
