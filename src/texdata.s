/* the pre-scaled texture columns (tools/textures.py) */
	.section .rodata
	.align 2
	.global _tex_bin
_tex_bin:
	.incbin "build/tex.bin"
