/*
 * init_tlb.S
 *
 *  Created on: Jan 12, 2011
 *  Author: MIPS TECHNOLOGIES, INC
 *  Common TLB initialization for MIPS cores
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
**************************************************************************************/
/**
 * init_tlb - 初始化 TLB (Translation Lookaside Buffer)
 *
 * 功能：
 * 1. 检查是否有 TLB
 * 2. 读取 TLB 大小
 * 3. 为每个 TLB entry 设置唯一的虚拟地址，避免 TLB shutdown
 *
 * TLB shutdown 问题：
 * 如果多个 TLB entry 有相同的 VA 和 ASID，硬件会关闭 TLB 防止损坏
 * 因此初始化时必须给每个 entry 赋予不同的 VA（以 8KB 为间隔）
 */
LEAF(init_tlb)

check_for_tlb:
	# Determine if we have a TLB - 检查是否有 TLB
	mfc0	v0, C0_CONFIG           # read C0_Config - 读取 Config 寄存器
	ext		v1, v0, 7, 3            # extract MT field - 提取 MT 字段 (bits 9:7)
	li		a3, 0x1                 # load a 1 to check against - 加载 1 用于比较
	beq		v1, a3, check_for_tlb_done  # 如果 MT==1，表示有 TLB
	mfc0	v0, C0_CONFIG1          # read C0_Config1 - 读取 Config1 寄存器
	nop

start_init_tlb:
	# Config1MMUSize == Number of TLB entries - 1
	# Config1 的 MMUSize 字段表示 TLB 项数减 1
	ext		v1, v0, 25, 6           # extract MMU Size - 提取 MMUSize (bits 30:25)
	mtc0	zero, C0_ENTRYLO0       # write C0_EntryLo0 - 将 EntryLo0 清零
	mtc0	zero, C0_ENTRYLO1       # write C0_EntryLo1 - 将 EntryLo1 清零
	mtc0	zero, C0_PAGEMASK       # write C0_PageMask - 页面掩码设为 0 (4KB 页面)
	mtc0	zero, C0_WIRED          # write C0_Wired - Wired 寄存器清零（所有 entry 可替换）

	# 初始化虚拟地址，从 0x00000000 开始，每次增加 8KB (0x2000)
	li		a0, 0x00000000          # 起始虚拟地址

next_tlb_entry_pair:
	# 遍历所有 TLB entry，为每个设置唯一的 VA
	mtc0	v1, C0_INDEX            # write C0_Index - 设置要写入的 TLB entry 索引
	mtc0	a0, C0_ENTRYHI          # write C0_EntryHi - 设置虚拟地址（VPN）
	ehb                             # 清除流水线冒险
	tlbwi                           # TLB Write Indexed - 将 CP0 寄存器写入 TLB[Index]

	addi	v1, v1, -1              # 递减索引计数器
	bne		v1, zero, next_tlb_entry_pair  # 如果还有 entry，继续循环
	addi	a0, a0, 0x2000          # Add 8K to the address to avoid TLB conflict with previous entry
	                                # VA 地址增加 8KB，确保每个 entry 的 VA 不同


check_for_tlb_done:
done_init_tlb:
    jr      ra                      # 返回
    nop
END(init_tlb)
