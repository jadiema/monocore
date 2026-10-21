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
| Players 3 and 4 both draw on the side of player 2 for now; in the band, their x ranges do not
| overlap, which tells them apart. An entry (12 bytes): +0 sprite tiles, +4 attributes,
| +6 ((0xFFF0 - y) << 7) | height, +8 x << 7, +10 shrink.

	.equ	LAST, 0x10F640			| pointer when the last task gave the hand
	.equ	S7, 0x10F644			| entries of task 7: [S7, E7), of task 8: [S8, E8)
	.equ	E7, 0x10F648
	.equ	S8, 0x10F64C
	.equ	E8, 0x10F650
	.equ	SAVE_D1, 0x10F654		| registers kept here, not on the stack of the task
	.equ	SAVE_A0, 0x10F658		| (the RAM of the game stays as without the patch)
	.equ	LIST, 0x1001CE

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
	move.w	-19260(%a5), %d1		| the task that gave the hand
	movea.l	-29066(%a5), %a0		| the list as it ends now
	cmpi.w	#7, %d1
	bne.s	1f
	move.l	LAST, S7
	move.l	%a0, E7
1:	cmpi.w	#8, %d1
	bne.s	2f
	move.l	LAST, S8
	move.l	%a0, E8
2:	move.l	%a0, LAST
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
	clr.l	S7				| the list starts again after this frame
	clr.l	E7
	clr.l	S8
	clr.l	E8
	move.l	#LIST, LAST
	lea	LIST, %a0			| what the replaced instruction did
	rts
