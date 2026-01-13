/*
 * Copyright (C) 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
.set nomips16
#include <asm/regdef.h>
#include <asm/cp0regdef.h>
#include <asm/asm.h>
/**
 * 异常向量表 - 运行时异常处理入口（BEV=0 时使用）
 * 位于 0x80000000，包含 TLB refill、Cache error、General exception 等入口
 */
.section .text.exc_vec3
# NESTED(except_vec3, 0, sp)
# 		.set	noat
# 		.set	push
# 		#.set	noreorder

/**
 * TLB 重填异常入口 (offset 0x00)
 * 当 TLB 查找未命中时触发，是最频繁的异常
 */
__tlb_refill:
		nop                         # 占位
		j _mips_general_exception   # 跳转到通用异常处理
		nop                         # 延迟槽

		.org 0x80
/**
 * XTLB 重填异常入口 (offset 0x80)
 * 用于 64 位地址空间的 TLB 重填（本系统为 32 位）
 */
__xtlb_refill_loop:
		nop                         # 占位
		j _mips_general_exception   # 跳转到通用异常处理
		nop                         # 延迟槽

		.org 0x100
/**
 * Cache 错误异常入口 (offset 0x100)
 * Cache 一致性错误或 ECC 错误时触发
 */
__cache_error_loop:
		nop                         # 占位
		j _mips_general_exception   # 跳转到通用异常处理
		nop                         # 延迟槽

		.org 0x180
/**
 * 通用异常处理入口 (offset 0x180)
 * 处理所有异常：中断、系统调用、地址错误等
 *
 * 功能：
 * 1. 保存内核栈指针
 * 2. 读取异常原因 (Cause 寄存器)
 * 3. 根据异常号跳转到相应处理函数
 */
_mips_general_exception:  # 中断处理地址
		/*
		 * Register saving is delayed as long as we dont know
		 * which registers really need to be saved.
		 * 寄存器保存延迟到我们知道具体需要保存哪些寄存器时再进行
		 */
#  1:	//j	1b
# 	nop

		sw		sp, kernel_sp       # 保存用户态栈指针到内存
		mfc0	k1, CP0_CAUSE       # 读取 Cause 寄存器到 k1，获取异常信息

		la		k0, exception_handlers  # 加载异常处理函数数组地址


		/*
		 * Next lines assumes that the used CPU type has max.
		 * 32 different types of exceptions. We might use this
		 * to implement software exceptions in the future.
		 * 接下来的代码假设 CPU 最多有 32 种不同的异常类型
		 * Cause 寄存器的 ExcCode 字段（bit 2-6）指示异常类型
		 */

		# 取出中断号（异常码）
		andi	k1, 0x7c

		# 把内存中 k0 地址处的值赋给 k0
		addu	k0, k0, k1          # k0 = exception_handlers + (ExcCode * 4)
		lw		k0, 0(k0)           # 从 exception_handlers[ExcCode] 读取处理函数地址

		nop                         # 避免 load-use 冒险

		# 跳到对应中断处理地址
		jr		k0                  # 跳转到异常处理函数
		nop                         # 延迟槽

		# END(except_vec3)
		# .set	at
		# .set	pop


/**
 * 数据段 - 存放全局变量
 */
.data
			.globl kernel_sp		# save the sp register when exception occur
kernel_sp:                      # 内核栈指针，异常发生时保存用户态 sp
			.word 0

			.globl mCONTEXT        # 上下文指针（用于任务切换）
mCONTEXT:
			.word 0

			.globl exception_handlers
exception_handlers:             # 异常处理函数指针数组（32个）
			.word 0:31              # 初始化为 0，由 trap_init() 设置

			.globl delay
delay:                          # 延时变量
			.word 0

			.globl temp
temp:                           # 临时变量
			.word 16

            .globl curtf
curtf:                          # 当前 Trapframe 指针
			.word 0





/**
 * 代码段 - 内核启动代码
 */
.text
/**
 * 内核入口函数 _start
 * 从 bootloader 跳转到这里开始执行内核初始化
 *
 * 功能：
 * 1. 禁用中断
 * 2. 禁用 Watch 异常
 * 3. 清零 BSS 段
 * 4. 设置内核栈
 * 5. 清除 BEV 位（切换到正常异常向量）
 * 6. 跳转到 main 函数
 */
LEAF(_start)

	.set	reorder             # 允许汇编器重排指令以优化
	.set	at                  # 允许使用 $at 寄存器

	/* Disable interrupts - 禁用中断 */
	mtc0	zero, CP0_STATUS    # 将 Status 寄存器清零，禁用所有中断

    /* Disable watch exception. - 禁用 Watch 异常 */
    mtc0    zero, CP0_WATCHLO   # 清除 WatchLo 寄存器
    mtc0    zero, CP0_WATCHHI   # 清除 WatchHi 寄存器


	/* Zero the BSS - 清零 BSS 段（未初始化的全局变量） */
	# test code - 测试代码（可能用于硬件调试）
	li v0, 0x80000000           # 加载测试地址
	li v1, 0x08000060           # 加载测试数据
	sw v1, 0(v0)                # 写入测试数据
	addi v0, 4                  # 地址加 4

	li v1, 0x00000000           # 加载 0
	sw v1, 0(v0)                # 写入 0
	addi v0, 4                  # 地址加 4





	# BSS 段清零循环
    la v0, _fbss                # 加载 BSS 段起始地址（由链接脚本定义）
	la v1, _end                 # 加载 BSS 段结束地址
	beq v0, v1, 2f              # 如果 BSS 为空，跳过清零
1:	                            # 清零循环开始
	addiu v0, v0, 4             # 地址加 4（每次处理 4 字节）
	nop
	sw zero, -4(v0)             # 将当前地址写入 0
	nop
	bne v0, v1, 1b              # 如果未到结束地址，继续循环
2:	                            # 清零完成

	/* disable kernel mode cache - 禁用内核模式缓存（可选）*/
	# 注意：通常情况下内核模式需要开启 Cache 以提高性能
	# 这里预留代码位置，实际可能不需要禁用
	# mfc0  t0, CP0_CONFIG
	# ori   t0, 0x3
	# xori  t0, 0x3
	# mtc0  t0, CP0_CONFIG

	/* set up stack - 设置内核栈 */
	# KERNEL 的栈顶位置，大小为 16KB
	la sp, 0x80400000           # 设置栈指针到 0x80400000（内核栈顶）
	                            # 栈向下生长，从高地址向低地址


	# Clear boot interrupt vector bit in Coprocessor 0 Status register
	# 清除 CP0 Status 寄存器中的 BEV 位
	# 从启动异常向量（0xBFC00000）切换到正常异常向量（0x80000000）
	mfc0	t0, CP0_STATUS      # 读取 Status 寄存器
	li		t1, ~(1 << 22)      # 构造掩码：BEV 位为 bit 22
	and		t0, t0, t1          # 清除 BEV 位
	mtc0	t0, CP0_STATUS      # 写回 Status 寄存器


	/* jump to main - 跳转到 C 语言 main 函数 */

	# UM=0; CP0=0; IE=0, 内核态，开中断
	mfc0	t0, CP0_STATUS
	ori	t0, t0, 0x00000001
	mtc0	t0, CP0_STATUS

	jal	main

	nop                         # 占位


loop:
	j	loop                    # 无限循环，如果 main 返回则在这里死循环
	nop
END(_start)

