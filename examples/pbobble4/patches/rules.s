| The rounds with four players (step 5 of DESIGN_4P.md): the last player left wins.
|
| In the game, the player whose bubbles reach the line (state 12, $62048-$620AC) leads the end of
| the round: it freezes its opponent (108(opponent) = 6, state 0), plays its LOST (states 13,
| 14), then gives the opponent the state 10 (WON: 10, 11) and itself 0; the winner, at the end of
| its WON, takes the number of the opponent for the result and ends the round ($22160). The
| opponent comes from two 2-entry tables indexed by the player number ($61E68, $62252), which
| players 3 and 4 read past. Here: a player who loses is out; the freezing and the WON go to the
| last player left, only when one is left; while more are left, the others go on. With two
| players this is the game as it was.

	.equ	P1S, 0x10D5D2
	.equ	P2S, 0x10D668
	.equ	P3S, 0x10F400
	.equ	P4S, 0x10F500
	.equ	FIELD7, 0x10F65C		| players 3 and 4 have a field (patches/band.s)
	.equ	FIELD8, 0x10F65D
	.equ	OUT, 0x10F660			| 4 bytes: the player is out of this round
	.equ	LASTOUT, 0x10F664		| structure of the last player out
	.equ	WINNER, 0x10F668		| structure of the winner of the round (patches/band.s)
	.equ	WINS, 0x10F6B0			| rounds won by each player (4 bytes, main.c shows them)
	.equ	FOURROUND, 0x10F616		| the round has a field for player 3 or 4 (patches/four.s)

| expect 061E2E 34 crc32 0xd679758b	(the player has just lost: freeze the opponent)
	.section .p_061E2E,"ax"
	jsr	lost_now
	bra.s	.+0x22-6			| to the end of the site (. is the bra, 6 bytes in)
	.fill	13,2,0x4e71
| expect 061E76 28 crc32 0x763ed612	(during LOST: keep the opponent frozen)
	.section .p_061E76,"ax"
	jsr	keep_frozen
	bra.s	.+0x1C-6
	.fill	10,2,0x4e71
| expect 061ECA 28 crc32 0xfa1e69dd
	.section .p_061ECA,"ax"
	jsr	keep_frozen
	bra.s	.+0x1C-6
	.fill	10,2,0x4e71
| expect 061F0C 28 crc32 0x9c974384	(end of LOST: the opponent gets the WON)
	.section .p_061F0C,"ax"
	jsr	give_won
	bra.s	.+0x1C-6
	.fill	10,2,0x4e71
| expect 032570 10 crc32 0x7b97dc0c	(bsr $32682 / bsr $327AE / rts: the WON and LOST banners)
	.section .p_032570,"ax"
	jmp	banners4
	nop
	nop
| expect 0221C2 40 crc32 0xbbf53349	(the rounds won by the winner d0: -10244(a5), -10252(a5);
	.section .p_0221C2,"ax"		|  up to the movem that gives d0 back, at $221E6)
	jsr	rounds_won
	bra.s	.+0x28-6
	.fill	16,2,0x4e71
| expect 0620CA 6 crc32 0xf90ba3b8	(the attack d0 sent to the opponent: $620CA-$620FA)
	.section .p_0620CA,"ax"
	jmp	0xC00E10			| shared among all the opponents (neort.c, native_attack)
| expect 0621D6 6 crc32 0x09f0a25a	(end of WON: the opponent, for the result)
	.section .p_0621D6,"ax"
	jsr	the_loser
| expect 062204 6 crc32 0xc127f3da
	.section .p_062204,"ax"
	jsr	the_loser
| expect 062224 6 crc32 0x8c959282
	.section .p_062224,"ax"
	jsr	the_loser

	.section .p_0F0800,"ax"
| d1 = number of players still in the round, a1 = the structure of the last one of them
| (players 1 and 2 always have a field; 3 and 4 when their task is a field)
in_round:
	moveq	#0, %d1
	suba.l	%a1, %a1
	tst.b	OUT
	bne.s	1f
	addq.w	#1, %d1
	lea	P1S, %a1
1:	tst.b	OUT+1
	bne.s	2f
	addq.w	#1, %d1
	lea	P2S, %a1
2:	tst.b	FIELD7
	beq.s	3f
	tst.b	OUT+2
	bne.s	3f
	addq.w	#1, %d1
	lea	P3S, %a1
3:	tst.b	FIELD8
	beq.s	4f
	tst.b	OUT+3
	bne.s	4f
	addq.w	#1, %d1
	lea	P4S, %a1
4:	rts

| the player of a4 has just lost: out; if one player is left, freeze it (as the game does)
lost_now:
	movem.l	%d0-%d1/%a1, -(%sp)
	move.w	2(%a4), %d0
	lea	OUT, %a1
	st	0(%a1,%d0.w)
	move.l	%a4, LASTOUT
	bsr.w	in_round
	cmpi.w	#1, %d1
	bne.s	1f
	move.w	#6, 108(%a1)
	move.w	#0, 4(%a1)
1:	movem.l	(%sp)+, %d0-%d1/%a1
	rts

keep_frozen:					| during LOST, while one player is left
	movem.l	%d1/%a1, -(%sp)
	bsr.w	in_round
	cmpi.w	#1, %d1
	bne.s	1f
	move.w	#6, 108(%a1)
1:	movem.l	(%sp)+, %d1/%a1
	rts

give_won:					| end of LOST: the last player left gets the WON
	movem.l	%d1/%a1, -(%sp)
	bsr.w	in_round
	cmpi.w	#1, %d1
	bne.s	1f
	move.w	#10, 4(%a1)
	tst.l	WINNER				| one more round won, once per round (two players may lose
	bne.s	2f				| at the same frame)
	move.l	%a1, WINNER
	move.w	2(%a1), %d1
	andi.w	#3, %d1
	lea	WINS, %a1
	cmpi.b	#99, 0(%a1,%d1.w)
	bcc.s	1f
	addq.b	#1, 0(%a1,%d1.w)
	bra.s	1f
2:	move.l	%a1, WINNER
1:	movem.l	(%sp)+, %d1/%a1
	rts

| a0 = the opponent of the winner, for the result (replaces asl.w #2,d0 / movea.l $62252(pc,d0.w),a0).
| The result of the game knows two sides only, and counts the rounds won in 2-entry tables
| (-10236(a5)...): with four players it gets a valid 2-player result, the other side of the
| winner (player 2 when player 1 wins, player 1 otherwise); its banners are not drawn
| (banners4 below), the screen of main.c says WINNER and OUT for each player.
the_loser:
	tst.b	FOURROUND			| (the fields of players 3 and 4 may have ended already)
	beq.s	1f
	lea	P1S, %a0
	movea.l	WINNER, %a1
	cmpa.w	#0, %a1
	beq.s	2f
	tst.w	2(%a1)
	bne.s	2f
	lea	P2S, %a0			| player 1 won
	bra.s	2f
1:	asl.w	#2, %d0
	movea.l	opponents(%pc,%d0.w), %a0
2:	rts
opponents:
	.long	P2S, P1S, P4S, P3S

| The rounds won count only with two players: the match of the game is player 1 against player 2
| (2-entry counters, a PLAYER n WON at the end); with four players the rounds follow one another
| and the panels of main.c say who wins each one. Otherwise what $221C2-$221E9 did: +1 to the
| rounds won (at most 999) and to the total of the winner d0 (d0 kept), a0 left on the total.
rounds_won:
	tst.b	FOURROUND
	bne.s	2f
	move.l	%d0, -(%sp)
	lea	-10244(%a5), %a0
	asl.w	#1, %d0
	addq.w	#1, 0(%a0,%d0.w)
	cmpi.w	#999, 0(%a0,%d0.w)
	ble.s	1f
	move.w	#999, 0(%a0,%d0.w)
1:	lea	-10252(%a5), %a0
	addq.w	#1, 0(%a0,%d0.w)
	move.l	(%sp)+, %d0
	rts
2:	lea	-10252(%a5), %a0
	rts

banners4:					| the banners of the result, with two players only (the fields
	tst.b	FOURROUND			| have ended when the result draws them: FIELD7 and FIELD8
	bne.s	1f				| are already cleared)
	jsr	0x32682				| WON
	jsr	0x327AE				| LOST
1:	rts
