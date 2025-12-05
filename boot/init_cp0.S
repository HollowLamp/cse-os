/*
 * initcp0.S
 *
 *  Created on: Jan 12, 2011
 *  Author: MIPS TECHNOLOGIES, INC
 *  Common CP0 register initializations
*/
/*
Copyright (c) 2014, Imagination Technologies LLC and Imagination Technologies
Limited.

All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions in binary form must be built to execute on machines
   implementing the MIPS32(R), MIPS64 and/or microMIPS instruction set
   architectures.

2. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

3. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

4. Neither the name of Imagination Technologies LLC, Imagination Technologies Limited
   nor the names of its contributors may be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL IMAGINATION TECHNOLOGIES LLC OR IMAGINATION
TECHNOLOGIES LIMITED BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/

#define _BOOTCODE 1

#include "boot.h"
#include <mips/regdef.h>
#include <mips/asm.h>
#include <mips/m32c0.h>

	.set	noreorder           // Don't allow the assembler to reorder instructions.
	.set	noat                // Don't allow the assembler to use r1(at) for synthetic instr.
/**************************************************************************************
* init_cp0 - 初始化 CP0 协处理器寄存器
*
* CP0 (Coprocessor 0) 是 MIPS 架构的系统控制协处理器
* 功能：MMU、异常处理、中断控制、计数器等
*
* 初始化策略：大部分寄存器置零，Status 寄存器设置特定值
**************************************************************************************/
LEAF(init_cp0)

    // Initialize Status - 初始化 Status 寄存器
    // Status 寄存器控制中断、异常级别、处理器模式等
	li		v1, 0x00400404		// (M_StatusERL | M_StatusIPL1 | M_StatusBEV)
	                            // ERL=1: 错误级别，BEV=1: 使用启动异常向量
	mtc0	v1, C0_STATUS		// write C0_Status - 写入 Status 寄存器

	// Initialize Watch registers if implemented. - 初始化 Watch 寄存器（如果实现了）
	// Watch 寄存器用于硬件断点和数据监视
	mfc0	v0, C0_CONFIG1		// read C0_Config1 - 读取 Config1 寄存器
	ext		v1, v0, 3, 1		// extract bit 3 WR (Watch registers implemented)
	                            // 提取 WR 位，判断是否实现了 Watch 寄存器
	beq 	v1, zero, done_wr   // 如果未实现 Watch，跳过初始化
	li    	v1, 0x7				// (M_WatchHiI | M_WatchHiR | M_WatchHiW)
	                            // 设置所有 Watch 条件位

	// Clear all possible Watch Status bits and disable watch exceptions
	// 清除所有可能的 Watch 状态位并禁用 Watch 异常
	// MIPS 最多支持 8 组 Watch 寄存器（WatchHi/Lo 0-7）
	mtc0	v1, C0_WATCHHI		// write C0_WatchHi0
	mtc0	zero, C0_WATCHLO	// write C0_WatchLo0

	mtc0	v1, C0_WATCHHI, 1	// write C0_WatchHi1
	mtc0	zero, C0_WATCHLO, 1	// write C0_WatchLo1

	mtc0	v1, C0_WATCHHI, 2	// write C0_WatchHi2
	mtc0	zero, C0_WATCHLO, 2	// write C0_WatchLo2

	mtc0	v1, C0_WATCHHI, 3	// write C0_WatchHi3
	mtc0	zero, C0_WATCHLO, 3	// write C0_WatchLo3

	mtc0	v1, C0_WATCHHI, 4	// write C0_WatchHi4
	mtc0	zero, C0_WATCHLO, 4	// write C0_WatchLo4

	mtc0	v1, C0_WATCHHI, 5	// write C0_WatchHi5
	mtc0	zero, C0_WATCHLO, 5	// write C0_WatchLo5

	mtc0	v1, C0_WATCHHI, 6	// write C0_WatchHi6
	mtc0	zero, C0_WATCHLO, 6	// write C0_WatchLo6

	mtc0	v1, C0_WATCHHI, 7	// write C0_WatchHi7
	mtc0	zero, C0_WATCHLO, 7	// write C0_WatchLo7

done_wr:

	// Clear WP bit to avoid watch exception upon user code entry, IV, and software interrupts.
	// 清除 WP 位，避免在进入用户代码、中断向量和软件中断时触发 Watch 异常
	mtc0	zero, C0_CAUSE		// write C0_Cause: Init AFTER init of CP0 WatchHi/Lo registers.
	                            // 写入 Cause 寄存器：必须在 Watch 寄存器初始化后执行

	// Clear timer interrupt. (Count was cleared at the reset vector to allow timing boot.)
	// 清除定时器中断（Count 在复位向量处已清零，用于测量启动时间）
	mtc0	zero, C0_COMPARE	// write C0_Compare - 写入 Compare 寄存器

        jr      ra
	nop
END(init_cp0)
