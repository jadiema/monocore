| The sending of the sprite list to the video RAM ($D36-$E18), in C (neort.c, native_sprites):
| the original writes every word through the LSPC registers, 11 % of the time of the console with
| four fields. After the band of patches/band.s ($D3A), $D40 calls the native routine (an address
| the runtime handles, like the BIOS), which does the work of $D40-$E12, then $E14 restores the
| registers and returns.
|
| expect 000D40 12 crc32 0xccca313a	(move.l #$1D80,d3 / move.l #$8076,d4)
	.section .p_000D40,"ax"
	jsr	0xC00F00			| native_sprites
	bra.w	0xE14				| movem_load / rts
	nop
