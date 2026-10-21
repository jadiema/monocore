| Players 3 and 4 drawn in a band the Neo Geo does not show (step 3b of DESIGN_4P.md), for the
| MONOCORE layer to place the four fields.
|
| Every task adds its sprites to the sprite list of the frame ($1001CE, write pointer -29066(a5);
| 106 routines do it), and the vertical-blank routine ($D36) sends the list to the video RAM. The
| kernel runs the tasks one after the other: the entries a task adds lie between the pointer when
| the previous task gave the hand and the pointer when it gives it. The scheduler ($2A54, where
| every system call ends) notes them for tasks 7 and 8; before the list is sent, their entries
| are moved:
|   player 3 (task 7): 256 lines down    (bit 15 of the vertical position word, +6 of an entry)
|   player 4 (task 8): 256 lines down and 256 pixels right (bit 15 of +6 and of +8)
| The second list (big objects: banners, characters; $100DFE, -29062(a5)) is noted and moved the
| same way. Players 3 and 4 both draw on the side of player 2 for now; in the band, their x ranges do not
| overlap, which tells them apart. An entry (12 bytes): +0 sprite tiles, +4 attributes,
| +6 ((0xFFF0 - y) << 7) | height, +8 x << 7, +10 shrink.

	.equ	LAST, 0x10F640			| pointer when the last task gave the hand
	.equ	S7, 0x10F644			| entries of task 7: [S7, E7), of task 8: [S8, E8)
	.equ	E7, 0x10F648
	.equ	S8, 0x10F64C
	.equ	E8, 0x10F650
	.equ	SAVE_D1, 0x10F654		| registers kept here, not on the stack of the task
	.equ	SAVE_A0, 0x10F658		| (the RAM of the game stays as without the patch)
	.equ	FIELD7, 0x10F65C		| 1: task 7 is the field of player 3 (set as the field starts,
	.equ	FIELD8, 0x10F65D		| patches/field.s; cleared here when the task ends)
	.equ	LAST2, 0x10F670		| the same for the second list ($100DFE, -29062(a5)): the
	.equ	S7B, 0x10F674			| big objects of several tiles (the WON and LOST banners,
	.equ	E7B, 0x10F678			| the characters)
	.equ	S8B, 0x10F67C
	.equ	E8B, 0x10F680
	.equ	LIST, 0x1001CE
	.equ	LIST2, 0x100DFE

| expect 002A54 6 crc32 0x88fbfcb6	(lea -19260(a5),a4 / move.w (a4),d0)
	.section .p_002A54,"ax"
	jmp	sched_hook

| expect 000D3A 6 crc32 0x490337c0	(lea $1001CE,a0, start of the sending of the list)
	.section .p_000D3A,"ax"
	jsr	band34

	.section .p_0F0400,"ax"
sched_hook:
	move.l	%d1, SAVE_D1
	move.l	%a0, SAVE_A0
	move.l	-19258(%a5), %d1		| tasks alive: a task 7 or 8 that ended is no field any more
	btst	#7, %d1
	bne.s	5f
	clr.b	FIELD7
5:	btst	#8, %d1
	bne.s	6f
	clr.b	FIELD8
6:	move.w	-19260(%a5), %d1		| the task that gave the hand
	movea.l	-29066(%a5), %a0		| the list as it ends now
	cmpi.w	#7, %d1
	bne.s	1f
	tst.b	FIELD7
	beq.s	1f
	move.l	LAST, S7
	move.l	%a0, E7
	move.l	LAST2, S7B
	move.l	-29062(%a5), E7B
1:	cmpi.w	#8, %d1
	bne.s	2f
	tst.b	FIELD8
	beq.s	2f
	move.l	LAST, S8
	move.l	%a0, E8
	move.l	LAST2, S8B
	move.l	-29062(%a5), E8B
2:	move.l	%a0, LAST
	move.l	-29062(%a5), LAST2
	movea.l	SAVE_A0, %a0
	move.l	SAVE_D1, %d1
	lea	-19260(%a5), %a4		| what the two replaced instructions did
	move.w	(%a4), %d0
	jmp	0x2A5A

| in the vertical-blank routine, which has saved every register
band34:
	movea.l	S7, %a0
	movea.l	E7, %a1
	bra.s	2f
1:	eori.w	#0x8000, 6(%a0)			| player 3: 256 lines down
	lea	12(%a0), %a0
2:	cmpa.l	%a1, %a0
	bcs.s	1b
	movea.l	S8, %a0
	movea.l	E8, %a1
	bra.s	4f
3:	eori.w	#0x8000, 6(%a0)			| player 4: 256 lines down, 256 pixels right
	eori.w	#0x8000, 8(%a0)
	lea	12(%a0), %a0
4:	cmpa.l	%a1, %a0
	bcs.s	3b
	movea.l	S7B, %a0			| the second list: player 3
	movea.l	E7B, %a1
	bra.s	6f
5:	eori.w	#0x8000, 6(%a0)
	lea	12(%a0), %a0
6:	cmpa.l	%a1, %a0
	bcs.s	5b
	movea.l	S8B, %a0			| player 4
	movea.l	E8B, %a1
	bra.s	8f
7:	eori.w	#0x8000, 6(%a0)
	eori.w	#0x8000, 8(%a0)
	lea	12(%a0), %a0
8:	cmpa.l	%a1, %a0
	bcs.s	7b
	clr.l	S7				| the lists start again after this frame
	clr.l	E7
	clr.l	S8
	clr.l	E8
	clr.l	S7B
	clr.l	E7B
	clr.l	S8B
	clr.l	E8B
	move.l	#LIST, LAST
	move.l	#LIST2, LAST2
	lea	LIST, %a0			| what the replaced instruction did
	rts

