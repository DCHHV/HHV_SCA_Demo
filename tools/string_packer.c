/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2019 KBEmbedded and DEF CON Hardware Hacking Village */

/* This is a very simple and somewhat fragile tool. Its basically a script but
 * is hopefully more portable in this format.
 *
 * The point of this is to receive a block of text through stdin, and then
 * output a PIC MPASM compatible packed data stream. This output is formatted
 * specifically for PIC12F1572, but would be compatible with any 14-bit PIC.
 *
 * This series of PIC, being only 14-bit, is only able to put string const's
 * in to the lowest 8-bits of each word. Using packing, 2x 7bit ASCII chars
 * can be put in a single PIC flash word.
 *
 * The high byte is the first character, the low byte is the second.
 *
 * The input format is expected to be:

<name_of_array>
Text to put in.\r\n
Be sure to use escape sequences as needed.

 * This will output:

PSECT strings,class=CODE,local,delta=2
GLOBAL _<name_of_array>
_<name_of_array>:
DW	0x... ; "XX"
...

 * The final output can be redirected to a file that can be included in the PIC
 * project. C can directly reference <name_of_array>. See the PIC sources for
 * more information on the packing and unpacking process.
 *
 * Usage: ./string_packer < textfile.txt > strings.s
 */

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Modifies the data in the passed pointer.
 * When dealing with an escape sequence, the '\' is not encoded in to the
 * binary, but the character after it needs to have the binary value of the
 * whole escape sequence.
 */
void parse_escaped(char *buf)
{
	switch (*buf) {
	  case 'n': /* Newline */
		*buf = '\n';
		break;
	  case 'r': /* Carriage return */
		*buf = '\r';
		break;
	  case '\\': /* Literal '\' */
		*buf = '\\';
		break;
	  case 't': /* Tab */
		*buf = '\t';
		break;
	  default:
		/* Poor error handling to just assert, but, print the value of
		 * the data we tripped up at.
		 */
		printf("0x%X\n", *buf);
		assert(0);
		break;
	}

}

void main(void)
{
	char *buf;
	char even_byte[3];
	char odd_byte[3];
	ssize_t sz = 128;
	/* NOTE: read is used as return from getline. read does NOT include
	 * the NULL termination at the end of line in its count!
	 */
	ssize_t read;
	uint32_t cnt;
	uint8_t byte_num = 0;
	uint16_t dw;

	/* Allocate a sting buffer, 128 chars, arbitrary, most of this
	 * input will be under 80 chars as it is meant for serial termial
	 * Use of getline() will resize this if necessary.
	 * Use assert here, no sense in trying to recover from failure
	 * of malloc()
	 */
	buf = (unsigned char *)malloc(sz);
	assert(buf);

	/* Get initial information about the array we're going to get */
	read = getline(&buf, &sz, stdin);
	assert(read != -1); /* No stream input? */
	
	/* Print some asm comments here */
	printf("PSECT strings,class=CODE,local,delta=2\n");
	printf("GLOBAL _%s\n", buf);
	printf("_%s\n", buf);

	read = getline(&buf, &sz, stdin);
	while (read != -1) {
		/* On each line, take every character that is printable, pack
		 * it in to a 14-bit word. Every two characters are packed
		 * together, with the relevant PIC mnemonic being printed out.
		 * Packing puts the first character in to the upper 7-bits of
		 * the word, and the following character in to the lower 7-bits
		 * of the PIC word. Repeat for all characters in the text. The
		 * mnemonic is followed by a comment of the two ASCII chars
		 * that word represents.
		 *
		 * Escape characters can be used in the source text, compatible
		 * escape characters are handled by packing in the binary
		 * representation of them.
		 *
		 * If the last character in a file is a high byte, a \0 is
		 * set as the low byte. If the the last character is a low byte
		 * then a word is added that contains two \0.
		 */
		for (cnt = 0; cnt <= read; cnt++) {
			if (!isprint(buf[cnt])) continue;
			if (byte_num == 0) {
				/* If we encounter an escape sequence, we need
				 * to print the full sequence to the ASM
				 * comment. The escaped char is then analyzed
				 * and replaced with the correct binary value.
				 */
				if (buf[cnt] == '\\') {
					cnt++;
					even_byte[0] = '\\';
					even_byte[1] = buf[cnt];
					even_byte[2] = '\0';
					/* This modifies buf[cnt]! */
					parse_escaped(&buf[cnt]);
					dw = (buf[cnt] << 7);
				} else {
					even_byte[0] = buf[cnt];
					even_byte[1] = '\0';
					dw = (buf[cnt] << 7);
				}

				byte_num++;
			} else {
				/* If we encounter an escape sequence, we need
				 * to print the full sequence to the ASM
				 * comment. The escaped char is then analyzed
				 * and replaced with the correct binary value.
				 */
				if (buf[cnt] == '\\') {
					cnt++;
					odd_byte[0] = '\\';
					odd_byte[1] = buf[cnt];
					odd_byte[2] = '\0';
					/* This modifies buf[cnt]! */
					parse_escaped(&buf[cnt]);
					dw |= ((buf[cnt] & 0x7F));
				} else {
					dw |= ((buf[cnt] & 0x7F));
					odd_byte[0] = buf[cnt];
					odd_byte[1] = '\0';
				}

				byte_num = 0;
				printf("DW\t0x%04X ; \"%s%s\"\n",
				  (dw & 0x3FFF), even_byte, odd_byte);
			}
		}
		read = getline(&buf, &sz, stdin);
	}

	/* When we leave the above loop, read is -1, what is done next depends
	 * on the byte_num value. If byte_num is 0, then the next word needs
	 * to be both NULL. If byte_num is 1, then the high byte of dw was set
	 * up, we need to add a NULL to the last byte and print the whole dw
	 * string.
	 */
	if (byte_num == 0) printf("DW\t0x0000 ; \"\\0\\0\"\n\n\n");
	else printf("DW\t0x%04X ; \"%s\\0\"\n\n\n", (dw & 0x3FFF), even_byte);
	
}
