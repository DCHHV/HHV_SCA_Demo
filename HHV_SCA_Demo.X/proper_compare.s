/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2019 KBEmbedded and DEF CON Hardware Hacking Village */

#include <xc.inc>

/* Can't figure out how to make loop and cmp "local" variables without the
 * linker bitching that they overlap even though they should be relocated.
 * So, just make them global in C, in common RAM, I guess.
  */

/* Returns 0 when strings match, greater than 0 when differing */
global	_pin
global	_pin_input
global	_loop
global	_cmp

psect   comparefunc,class=CODE,delta=2

/* Confession: It has been a while since I wrote assembly, so this
 * is not the most optimized. However, it is exactly the same amount
 * of cycles for each digit in the PIN, correct or incorrect.
 *
 * The critical section marked below, branches to one of two paths
 * that meet back up later to the same place. The branch is
 * determined by a comparison of one byte from the input PIN and
 * the same byte position of the expected PIN. For ease of code it
 * actually goes backwards, starting at the last byte and working to
 * the first. What is important is that each path of these branches
 * take exactly the same amount of cycles. Doing so eliminates the
 * timing side channel attack (there may still be other avenues,
 * however).
 *
 * Both the input PIN and the expected PIN, as well as the loop
 * counter are put in specific places of common RAM. This might not
 * actually be necessary, however it has little impact overall.
 * It prevents this assembly section and the compiler from having to worry
 * about bank switching to access these variables. This may not be a
 * concern because:
 * 1) RAM use is small enough for the whole project that all of the RAM fits
 *      in a single bank.
 * 2) The C compiler _should_ be smart enough to see what variables are being
 *      touched inside of this block and put them all in the same bank to
 *      begin with.
 */

global _proper_compare

_proper_compare:

    CLRF    _cmp
    MOVLW   3;
    MOVWF   _loop;
stage_3_loop:
    /* Load the address of pin_input+3 in to FSR0 */
    MOVLW   low _pin_input
    ADDWF   _loop, W
    MOVWF   FSR0L
    CLRF    FSR0H

    /* Load the address of pin+3 in to FSR1 */
    MOVLW   low _pin
    ADDWF   _loop, W
    MOVWF   FSR1L
    CLRF    FSR1H

    /* Compare the two values via XOR. Z flag is set when they are equal */
    MOVF    INDF0, W;
    XORWF   INDF1, W;

    /* Start critical section */
    BTFSS   STATUS, 2;		// 2 cycles if Zero is set, 1 if not. Bit 2 is Z
    INCF    _cmp, F;		// 1 cycle.
    /* End critical section */

check_loop:			//From BTFSS, both paths take 2 cycles to here.
    CLRW;
    XORWF   _loop, W;
    BTFSC   STATUS, 2;		//2 cycles if true, 1 if false. Bit 2 is Z
    GOTO    end_loop;

    DECF    _loop, F;
    GOTO    stage_3_loop;
end_loop:

    MOVF    _cmp, W
    RETURN
