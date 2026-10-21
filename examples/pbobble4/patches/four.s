| Four fields (step 3a of DESIGN_4P.md): tasks 7 and 8 run the field routine for players 3 and 4,
| with their own structures, buffers and pads; the tables indexed by the player number get four
| entries. Players 3 and 4 play against each other for now (opponent tables), like players 1
| and 2: two duels in one game; the attacks shared among all opponents come next (site 620D4).
|
| RAM of players 3 and 4. $10598C-$108F98 looked free but is the game's object table (320 entries
| of 44 bytes from -10088(a5), mostly empty), and $10B140-$10D4D4 the end of a 16 KB array at
| $1094D4: the RAM chosen is not referenced by the code and never changes in game sessions
| (DESIGN_4P.md): the work RAM of the original BIOS, which the port replaces ($10F300-$10FBFF,
| the game's stack grows down from $10F300), and $1041D6-$105476 for the two 2 KB buffers.
	.equ	P3S, 0x10F400			| structures (150 bytes, as $10D5D2 / $10D668)
	.equ	P4S, 0x10F500
	.equ	PAD3, 0x10F600			| pad blocks (4 bytes, as $100FF4 / $100FF8)
	.equ	PAD4, 0x10F604
	.equ	RAW3, 0x10F610			| pads 3 and 4 as the platform gives them (1 = pressed)
	.equ	RAW4, 0x10F611
	.equ	SCORE3, 0x10F620		| per-player long (as $1057DC / $1057E0)
	.equ	SCORE4, 0x10F624
	.equ	COUNT3, 0x10F628		| per-player word (as $10900A / $10900C)
	.equ	COUNT4, 0x10F62A
	.equ	BUF3, 0x104200			| field buffers (2 KB, as $10D6FE / $10DEFE)
	.equ	BUF4, 0x104A00

| --- pads: the field reads its pad through three 4-entry tables; entries 3 and 4 pointed to RAM
| the game uses for other things
| expect 06249E 8 crc32 0x288afd0f
| expect 0624AE 8 crc32 0x62ede429
| expect 0624BE 8 crc32 0xf623d665
	.section .p_06249E,"a"
	.long	PAD3, PAD4			| state
	.section .p_0624AE,"a"
	.long	PAD3+1, PAD4+1			| new presses
	.section .p_0624BE,"a"
	.long	PAD3+3, PAD4+3

| the pad blocks of players 1 and 2 are filled by $246C, then $24BE computes their presses:
| fill those of players 3 and 4 as well
| expect 0024B8 6 crc32 0x0a228521	(jsr $24BE.w / rts)
	.section .p_0024B8,"ax"
	jmp	pads34

| --- structures 3 and 4 initialised with 1 and 2; tasks 7 and 8 created when the field of player 2
| starts, in two-player mode (the game creates its fields in several places, one per mode: $22B48,
| $22B94, $22BD4, $22C68 ...; every field task starts with this initialisation)
| expect 074D60 8 crc32 0x321d20a9	(jsr $74D68(pc) / nop / rts)
	.section .p_074D60,"ax"
	jmp	init34
	nop

| expect 074EE8 22 crc32 0x1d29f431	(d0 into 98(a4) of both structures)
	.section .p_074EE8,"ax"
	jmp	set98

| --- tables indexed by the player number: lea T(pc),a0 / movea.l 0(a0,dN.w),a0 -> jsr + nop
| expect 043380 8 crc32 0xa837c1af
	.section .p_043380,"ax"
	jsr	t_score
	nop
| expect 044D3E 8 crc32 0xa46587a0
	.section .p_044D3E,"ax"
	jsr	t_count
	nop
| expect 0459DC 8 crc32 0x15f90765
	.section .p_0459DC,"ax"
	jsr	t_opp14
	nop
| expect 0620D4 8 crc32 0xb09b7716	(index in d1: the structure of the opponent, attack sent)
	.section .p_0620D4,"ax"
	jsr	t_opp
	nop
| expect 062B2E 8 crc32 0x0fd9c387
	.section .p_062B2E,"ax"
	jsr	t_pad1
	nop
| expect 074E1E 6 crc32 0xcd545cca	(lsl.w #2,d0 / movea.l $74E90(pc,d0.w),a0)
	.section .p_074E1E,"ax"
	jsr	t_pad

| ---------------------------------------------------------------- the routines
	.section .p_0F0100,"ax"
| pads 3 and 4: the same as $246C does for 1 and 2 (state inverted, previous state), then the
| presses as $24BE does. Only d0-d2 may change, as in $24BE: a0 is kept.
pads34:
	move.l	%a0, -(%sp)
	move.b	PAD3, PAD3+2
	move.b	RAW3, %d0
	andi.b	#0x7F, %d0
	eori.b	#0x7F, %d0
	move.b	%d0, PAD3
	move.b	PAD4, PAD4+2
	move.b	RAW4, %d0
	andi.b	#0x7F, %d0
	eori.b	#0x7F, %d0
	move.b	%d0, PAD4
	jsr	0x24BE				| players 1 and 2, as before
	lea	PAD3, %a0
	bsr.s	presses
	lea	PAD4, %a0
	bsr.s	presses
	movea.l	(%sp)+, %a0
	rts
presses:					| as $24BE: +1 = new presses, +3 = previous or not now
	move.b	2(%a0), %d1
	move.b	(%a0), %d0
	move.b	%d1, %d2
	not.b	%d0
	and.b	%d0, %d2
	move.b	%d2, 1(%a0)
	or.b	%d0, %d1
	move.b	%d1, 3(%a0)
	rts

init34:
	jsr	0x74D68				| end of the initialisation of player 2, as before
	clr.l	0x10F660			| a new round: nobody is out (patches/rules.s)
	clr.l	0x10F664
	lea	P3S, %a4
	move.w	#0, (%a4)			| played by a person (1: by the computer)
	move.w	#2, 2(%a4)			| player number
	move.l	#BUF3, 126(%a4)
	jsr	0x74D68
	lea	P4S, %a4
	move.w	#0, (%a4)
	move.w	#3, 2(%a4)
	move.l	#BUF4, 126(%a4)
	jsr	0x74D68
	cmpi.w	#6, -19260(%a5)			| the field of player 2 is starting
	bne.s	1f
	tst.w	21966(%a5)			| players 1 and 2 both played by people (the flags the
	bne.s	1f				| game sets before creating the fields: 0 = a person,
	tst.w	21968(%a5)			| 1 = the computer): for now, two people means 4 players
	bne.s	1f
	move.l	#7, -(%sp)			| task 7: player 3
	pea	0x74F16
	trap	#1
	addq.l	#8, %sp
	move.l	#8, -(%sp)			| task 8: player 4
	pea	0x74F16
	trap	#1
	addq.l	#8, %sp
1:	lea	0x10D668, %a4			| a4 on the structure of player 2, as the original leaves it
	rts

set98:
	move.l	%d0, 0x10D5D2+98
	move.l	%d0, 0x10D668+98
	move.l	%d0, P3S+98
	move.l	%d0, P4S+98
	rts

t_score:
	lea	score_t(%pc), %a0
	movea.l	0(%a0,%d0.w), %a0
	rts
t_count:
	lea	count_t(%pc), %a0
	movea.l	0(%a0,%d0.w), %a0
	rts
t_opp14:
	lea	opp14_t(%pc), %a0
	movea.l	0(%a0,%d0.w), %a0
	rts
t_opp:
	lea	opp_t(%pc), %a0
	movea.l	0(%a0,%d1.w), %a0
	rts
t_pad1:
	lea	pad1_t(%pc), %a0
	movea.l	0(%a0,%d0.w), %a0
	rts
t_pad:
	lsl.w	#2, %d0
	movea.l	pad_t(%pc,%d0.w), %a0
	rts

	.even
pad_t:	.long	0x100FF4, 0x100FF8, PAD3, PAD4			| $74E90 with 4 entries
pad1_t:	.long	0x100FF5, 0x100FF9, PAD3+1, PAD4+1		| $62B52
score_t: .long	0x1057DC, 0x1057E0, SCORE3, SCORE4		| $434C8
count_t: .long	0x10900A, 0x10900C, COUNT3, COUNT4		| $44E72
opp14_t: .long	0x10D668+0x14, 0x10D5D2+0x14, P4S+0x14, P3S+0x14	| $45A0E: the opponent's +$14
opp_t:	.long	0x10D668, 0x10D5D2, P4S, P3S			| $620FC: the opponent (attacks)
