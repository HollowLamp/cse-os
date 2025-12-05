/*
 * init_caches.S
 *
 *  Created on: Jan 12, 2011
 *  Author: MIPS TECHNOLOGIES, INC
 *  Common Cache initialization
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
 * init_icache - 初始化指令 Cache (Instruction Cache)
 *
 * 功能：
 * 1. 读取 I-Cache 参数（大小、行数、关联度）
 * 2. 遍历所有 Cache 行，将 Tag 置为 Invalid
 * 3. 确保启动时 Cache 处于一致状态
 *
 * Cache 初始化 ≈ 置零：将所有 Tag 标记为无效
 */
LEAF(init_icache)

	# Can be skipped if using magic simulation cache flush

	# Determine how big the I$ is - 确定 I-Cache 的大小
	mfc0	v0, C0_CONFIG1          # read C0_Config1 - 读取 Config1 寄存器

	# Isolate I$ Line Size - 获取 I-Cache 行大小
	ext		v1, v0, 19, 3           # extract IL - 提取 IL 字段 (bits 21:19)

	# Skip ahead if No I$ - 如果没有 I-Cache，跳过
	beqz	v1, done_icache         # 如果 IL==0，表示没有 I-Cache
	nop

	# Now have true I$ line size in bytes - 计算实际行大小（字节）
	li		a2, 2                   # 基础行大小为 2 字节
	sllv	a2, a2, v1              # a2 = 2 << IL，得到实际行大小

	ext		a1, v0, 22, 3           # extract IS - 提取 IS 字段 (bits 24:22)
	li		a3, 64                  # I$ Sets per way - 每路 64 组
	sllv	a1, a3, a1              # a1 = 64 << IS，得到每路的组数

	# Config1IA == I$ Assoc - 1 - I-Cache 关联度
	ext		a3, v0, 16, 3           # extract IA - 提取 IA 字段 (bits 18:16)
	add		a3, a3, 1               # a3 = IA + 1，得到实际关联度（路数）
	mul		a1, a1, a3              # Total number of sets - 总组数 = 每路组数 × 路数

	lui		a0, 0x8000              # Get a KSeg0 address for cacheops
	                                # 获取 KSEG0 地址用于 Cache 操作（0x80000000）

	# Clear TagLo/TagHi registers - 清零 TagLo/TagHi 寄存器
	mtc0	zero, C0_TAGLO          # write C0_ITagLo - 将 ITagLo 清零
	mtc0	zero, C0_TAGHI          # write C0_ITagHi - 将 ITagHi 清零
	ehb                             # 清除流水线冒险

next_icache_tag:
	# Index Store Tag Cache Op - 索引存储 Tag Cache 操作
	# Will invalidate the tag entry, clear the lock bit, and clear the LRF bit
	# 将使 Tag 无效，清除锁定位和 LRF 位
	cache	0x08, 0(a0)             # ICIndexStTag - I-Cache 索引存储 Tag
	add		a1, a1, -1              # Decrement set counter - 递减计数器
	bne		a1, zero, next_icache_tag  # 如果还有 entry，继续循环
	add		a0, a0, a2              # Get next line address - 地址增加一个行大小

done_icache:

    jr      ra                      # 返回
    nop
END(init_icache)

/**************************************************************************************
* init_dcache invalidates all data cache entries
* 初始化数据 Cache - 使所有数据 Cache 项无效
**************************************************************************************/

LEAF(init_dcache)

	mfc0	v0, C0_CONFIG1          # read C0_Config1 - 读取 Config1 寄存器

	# Isolate D$ Line Size - 获取 D-Cache 行大小
	ext		v1, v0, 10, 3           # extract DL - 提取 DL 字段 (bits 12:10)

	# Skip ahead if No D$ - 如果没有 D-Cache，跳过
	beqz	v1, done_dcache         # 如果 DL==0，表示没有 D-Cache
	nop

	# Now have true D$ line size in bytes - 计算实际行大小（字节）
	li		a2, 2                   # 基础行大小为 2 字节
	sllv	a2, a2, v1              # a2 = 2 << DL，得到实际行大小

	ext		a1, v0, 13, 3           # extract DS - 提取 DS 字段 (bits 15:13)
	li		a3, 64                  # D$ Sets per way - 每路 64 组
	sllv	a1, a3, a1              # a1 = 64 << DS，得到每路的组数

	# Config1DA == D$ Assoc - 1 - D-Cache 关联度
	ext		a3, v0, 7, 3            # extract DA - 提取 DA 字段 (bits 9:7)
	add		a3, a3, 1               # a3 = DA + 1，得到实际关联度（路数）
	mul		a1, a1, a3              # Get total number of sets - 总组数 = 每路组数 × 路数

	lui		a0, 0x8000              # Get a KSeg0 address for cacheops
	                                # 获取 KSEG0 地址用于 Cache 操作（0x80000000）

	# Clear TagLo/TagHi registers - 清零 TagLo/TagHi 寄存器
	mtc0	zero, C0_TAGLO          # write C0_TagLo - 将 TagLo 清零
	mtc0	zero, C0_TAGHI          # write C0_TagHi - 将 TagHi 清零
	mtc0	zero, C0_DTAGLO         # write C0_DTagLo - 将 DTagLo 清零
	mtc0	zero, C0_DTAGHI         # write C0_DTagHi - 将 DTagHi 清零
	ehb                             # 清除流水线冒险

	move	a3, a0                  # 保存起始地址

next_dcache_tag:
	# Index Store Tag Cache Op - 索引存储 Tag Cache 操作
	# Will invalidate the tag entry, clear the lock bit, and clear the LRF bit
	# 将使 Tag 无效，清除锁定位和 LRF 位
	cache	0x09, 0(a0)             # DCIndexStTag - D-Cache 索引存储 Tag
	add		a1, a1, -1              # Decrement set counter - 递减计数器
	bne		a1, zero, next_dcache_tag  # 如果还有 entry，继续循环
	add		a0, a0, a2              # Get next line address - 地址增加一个行大小

done_dcache:
    jr      ra                      # 返回
    nop
END(init_dcache)

/**
 * change_k0_cca - 修改 KSEG0 的 Cache 一致性属性
 *
 * 注意：此代码必须在 KSEG1（非缓存区）执行，不能在 KSEG0 执行
 * 因为我们正在修改 Cache 配置本身
 *
 * 功能：设置 KSEG0 为可缓存（cacheable）
 */
LEAF(change_k0_cca)
	# NOTE! This code must be executed in KSEG1 (not KSGE0 uncached)
    # Set CCA for kseg0 to cacheable
    # 设置 KSEG0 的 CCA（Cache Coherency Attribute）为可缓存

	mfc0	v0, C0_CONFIG           # read C0_Config - 读取 Config 寄存器
	li		v1, 0x03                # CCA for single-core processors
	                                # CCA = 3 (Cacheable, noncoherent, write-back, write allocate)
	ins		v0, v1, 0, 3            # insert K0 - 将 CCA 值插入 Config[2:0]
	mtc0	v0, C0_CONFIG           # write C0_Config - 写回 Config 寄存器
	jr.hb   ra                      # 返回，.hb 表示 hazard barrier（冒险屏障）
    	nop                         # 延迟槽

END(change_k0_cca)
