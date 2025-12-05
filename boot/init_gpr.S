/*
 * init_gpr.S
 *
 *  Created on: Jan 12, 2011
 *      Author: MIPS TECHNOLOGIES, INC
 *  Start of boot code for 24K Family of Cores
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
* init_gpr - 初始化通用寄存器 (General Purpose Registers)
*
* 注意：GPR 初始化不是必须的，但在以下场景有用：
* - 启动调试：方便识别未初始化的寄存器
* - 仿真环境：某些寄存器初始化方式可能不工作
*
* Shadow Register Sets：
* MIPS 支持影子寄存器组，用于快速中断响应
* 每个影子组都有独立的 GPR 副本
**************************************************************************************/
LEAF(init_gpr)

	// Initialize the general purpose registers and any shadow register sets.
	// 初始化通用寄存器和所有影子寄存器组
	// Although not necessary, register initialization may be useful during boot,
    // debug, and simulation when certain ways of initializing registers may not work
    // (xor rN, rN, rN for example.)
    // 虽然不是必须的，但在启动、调试和仿真时很有用

	// Initialize register sets - 初始化寄存器组
    li      $1, 0xdeadbeef      // (0xdeadbeef stands out, kseg2 mapped, odd.)
                                // 使用 0xdeadbeef 便于识别（明显特征，KSEG2 映射，奇数地址）

	// Determine how many shadow sets are implemented (in addition to the base register set.)
	// 确定实现了多少个影子寄存器组（除了基础寄存器组之外）
	// the first time thru the loop it will initialize using $1 set above.
	// 第一次循环将使用上面设置的 $1 值进行初始化
	// At the bottom og the loop, 1 is  subtract from $30
	// 在循环底部，从 $30 减 1
	// and loop back to next_shadow_set to start the next loop and the next lowest set number.
	// 然后循环回 next_shadow_set，开始下一个循环和下一个更低的组号
	mfc0	$29, C0_SRSCTL		// read C0_SRSCtl - 读取影子寄存器控制寄存器
	ext	    $30, $29, 26, 4		// extract HSS - 提取 HSS (Highest Shadow Set) 字段

next_shadow_set:
	// set PSS to shadow set to be initialized
	// 设置 PSS (Previous Shadow Set) 为要初始化的影子组
	ins		$29, $30, 6, 4		// insert PSS - 将影子组号插入 PSS 字段
	mtc0	$29, C0_SRSCTL		// write C0_SRSCtl - 写入影子寄存器控制寄存器

	wrpgpr	$1, $1
	wrpgpr	$2, $1
	wrpgpr	$3, $1
	wrpgpr	$4, $1
	wrpgpr	$5, $1
	wrpgpr	$6, $1
	wrpgpr	$7, $1
	wrpgpr	$8, $1
	wrpgpr	$9, $1
	wrpgpr	$10, $1
	wrpgpr	$11, $1
	wrpgpr	$12, $1
	wrpgpr	$13, $1
	wrpgpr	$14, $1
	wrpgpr	$15, $1
	wrpgpr	$16, $1
	wrpgpr	$17, $1
	wrpgpr	$18, $1
	wrpgpr	$19, $1
	wrpgpr	$20, $1
	wrpgpr	$21, $1
	wrpgpr	$22, $1
	wrpgpr	$23, $1
	wrpgpr	$24, $1
	wrpgpr	$25, $1
	wrpgpr	$26, $1
	wrpgpr	$27, $1
	wrpgpr	$28, $1
	wrpgpr	$29, $1
	beqz    $30, done_init_gpr  // early exit when we get to set 0 so we don't clobber return in $31
	                            // 当到达组 0 时提前退出，避免破坏 $31 中的返回地址
	nop
	wrpgpr	$30, $1
	wrpgpr	$31, $1             // 初始化 $31（返回地址寄存器）
	b		next_shadow_set     // 循环到下一个影子组
	add		$30, -1             // Since the code started with the highest set number this decrements to the next lower number
	                            // 递减组号，从最高组号向 0 递减


done_init_gpr:
    jr      ra                  // 返回
    nop                         // 延迟槽
END(init_gpr)

