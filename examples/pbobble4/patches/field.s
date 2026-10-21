| Player fields: the structure of a player is computed from the number of its task instead of
| "task 5 -> player 1, else player 2", so that tasks 7 and 8 can be the fields of players 3 and 4.
| Assembled by tools/patch68k.py (GNU as, m68k, --register-prefix-optional is not used: %a4...).
|
| expect 074F1E 22 crc32 0x56ba7d81     (cmpi.w #5,task / bne / lea P1,a4 / bra / lea P2,a4)

	.section .p_074F1E,"ax"
	jsr	field_struct			| a4 = structure of the running task's player
	bra.s	.+16				| to 074F34, where the original code goes on
	.fill	7,2,0x4e71			| nop (never run)

	.section .p_0F0000,"ax"
| a4 = structure of the player whose field is the running task (task 5 = player 1 ... 8 = player 4).
| No data register and no stack: a4 itself is the index (4 bytes per entry of the table).
field_struct:
	movea.w	-19260(%a5),%a4			| number of the running task
	adda.w	%a4,%a4
	adda.w	%a4,%a4				| x 4
	movea.l	field_table-20(%pc,%a4.w),%a4	| entry for tasks 5-8 (the table starts at task 5)
	rts
field_table:
	.long	0x10D5D2			| player 1 (task 5)
	.long	0x10D668			| player 2 (task 6)
	.long	0x10F400			| player 3 (task 7, patches/four.s)
	.long	0x10F500			| player 4 (task 8)
