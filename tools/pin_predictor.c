/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2019 KBEmbedded and DEF CON Hardware Hacking Village */

/* A simple tool that prints out every possible seed value for the HHV SCA Demo
 * followed by the 4 PIN values for this seed.
 * Using this for any purpose other than verification is cheating :D
 */

#include <stdio.h>
#include <stdint.h>

void main(void)
{

	uint8_t val;
	uint16_t seed;
	uint8_t loop;
	int i;

	for (seed = 0; seed < 256; seed++) {
		printf("0x%02X: ", seed);
		if (seed & 0x1) {
			val = (seed >> 1);
			val ^= 0xB8;
		} else {
			val = (seed >> 1);
		}
		for (loop = 0; loop < 4; loop++) {
			
			printf("0x%02X ", val);
			printf("%c%c%c%c, ", (((val >> 6) & 0x3) + 0x30),
					    (((val >> 4) & 0x3) + 0x30),
					    (((val >> 2) & 0x3) + 0x30),
					    (((val) & 0x3) + 0x30));

			if (val & 0x1) {
				val = (val >> 1);
				val ^= 0xB8;
			} else {
				val = (val >> 1);
			}
		}
		printf("\n");
	}
}
