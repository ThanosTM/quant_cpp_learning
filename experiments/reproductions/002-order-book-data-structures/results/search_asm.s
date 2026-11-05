	.section	__TEXT,__text,regular,pure_instructions
	.build_version macos, 26, 0	sdk_version 26, 1
	.globl	_search_branchy                 ; -- Begin function search_branchy
	.p2align	2
_search_branchy:                        ; @search_branchy
	.cfi_startproc
; %bb.0:
	mov	x8, #0                          ; =0x0
	cbz	x1, LBB0_2
LBB0_1:                                 ; =>This Inner Loop Header: Depth=1
	lsr	x9, x1, #1
	add	x10, x0, x8, lsl #3
	lsl	x11, x9, #3
	ldr	w10, [x10, x11]
	sub	x11, x1, x9
	cmp	w10, w2
	csel	x10, x11, xzr, lt
	add	x8, x10, x8
	cmp	x1, #1
	mov	x1, x9
	b.hi	LBB0_1
LBB0_2:
	mov	x0, x8
	ret
	.cfi_endproc
                                        ; -- End function
	.globl	_search_branchless              ; -- Begin function search_branchless
	.p2align	2
_search_branchless:                     ; @search_branchless
	.cfi_startproc
; %bb.0:
	mov	x8, #0                          ; =0x0
	cbz	x1, LBB1_2
LBB1_1:                                 ; =>This Inner Loop Header: Depth=1
	lsr	x9, x1, #1
	add	x10, x0, x8, lsl #3
	lsl	x11, x9, #3
	ldr	w10, [x10, x11]
	sub	x11, x1, x9
	cmp	w10, w2
	csel	x10, x11, xzr, lt
	add	x8, x10, x8
	cmp	x1, #1
	mov	x1, x9
	b.hi	LBB1_1
LBB1_2:
	mov	x0, x8
	ret
	.cfi_endproc
                                        ; -- End function
.subsections_via_symbols
