/*
 * boot.S for microAptiv_UP
 *
 *  Created on: April 12, 2014
 *  Author: MIPS TECHNOLOGIES, INC
 *  Start of boot code
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

#define _RESETCODE 1

#include <mips/regdef.h>
#include <mips/asm.h>
#include "boot.h"
#include "mfp_io.h"
#include <mips/m32c0.h>

.set    noreorder               # Don't allow the assembler to reorder instructions.

    # The first function, __reset_vector, just loads the address of the __cpu_init function
    # and jumps to it. This does 2 things; First it will jump to a KSEG1 address which is a
    # mirror of the BEV�s KSEG0 address but cacheable. Second it will do a mode switch to
    # enable micromips mode.

/**************************************************************************************
    R E S E T   E X C E P T I O N   H A N D L E R
**************************************************************************************/

/**
 * 复位异常处理入口 - 系统上电或复位后的第一条指令
 * 地址：0xBFC00000 (Boot Exception Vector)
 */
LEAF(__reset_vector)
    la a2,__cpu_init        # 加载 __cpu_init 函数地址到 a2
    jr a2                   # 跳转到 CPU 初始化函数
                            # Clear cp0 Count (Used to measure boot time.)
    nop                     # 延迟槽 (delay slot)

END(__reset_vector)

/**
 * CPU 初始化函数 - 初始化硬件环境
 * 功能：
 * 1. 检查是否为 NMI (Non-Maskable Interrupt)
 * 2. 初始化 CP0 寄存器
 * 3. 初始化 TLB
 * 4. 初始化 Cache
 * 5. 准备跳转到内核入口 _start
 */
LEAF(__cpu_init)

    # Verify the code is here due to a reset and not NMI. If this is an NMI then trigger
    # a debugger breakpoint using a sdbp instruction.
    # 验证这是由复位引起的，而不是 NMI（不可屏蔽中断）

    mfc0    v0, C0_STATUS           # Read CP0 Status - 读取 CP0 Status 寄存器
    ext     v1, v0, 19, 1           # extract NMI - 提取 NMI 位 (bit 19)
    beqz    v1, init_resources      # Branch if this is NOT an NMI exception. - 如果不是 NMI，跳转到初始化
    nop
    sdbbp                           # Failed assertion: NMI. - 是 NMI，触发调试器断点



init_resources:                     # initializes resources for "cpu". - 初始化 CPU 资源

    # Initialize CP0 registers - 初始化 CP0 协处理器寄存器
    # CP0 控制 CPU 行为，包括 MMU、异常处理、中断等

    bal     init_cp0                # Init CP0 Status, Count, Compare, Watch*, and Cause.
                                    # 调用 CP0 初始化函数（在 init_cp0.S 中定义）
    nop                             # 延迟槽

    # Initialize the TLB - 初始化 TLB (Translation Lookaside Buffer)
    # TLB 用于虚拟地址到物理地址的快速转换
    # 必须给每个 entry 赋予不同的 VA，避免 TLB shutdown

    bal     init_tlb                # Generate unique EntryHi contents per entry pair.
                                    # 调用 TLB 初始化函数（在 init_tlb.S 中定义）
    nop                             # 延迟槽


    # The changing of Kernel mode cacheability must be done from KSEG1
    # Since the code is executing from KSEG0 It needs to do a jump to KSEG1 change K0 and jump back to KSEG0
    # Cache 配置必须在 KSEG1（非缓存区）执行，因为正在配置 Cache 本身

    la      t0, change_k0_cca       # 加载 change_k0_cca 函数地址
    or      t0, t0, 0x20000000      # changed to KSEG1 address by setting bit 29
                                    # 设置 bit 29，将地址转换为 KSEG1（非缓存）
    jalr    t0                      # 跳转到 KSEG1 执行 Cache 配置
    nop                             # 延迟槽


    # Prepare for eret to main. - 准备通过 eret 指令跳转到 _start
    # eret 用于从异常返回，这里利用它来进入内核

    la      ra, all_done            # If main returns then go to all_done:.
                                    # 设置返回地址，如果 _start 返回则跳转到 all_done
    move    a0, zero                # Indicate that there are no arguments available.
                                    # argc = 0，表示没有命令行参数
    la      a1, _start              # load the address of the CRT entry point _start.
                                    # 加载内核入口地址 _start
    mtc0    a1, C0_ERRPC, 0         # Write ErrorEPC with the address of main
                                    # 将 _start 地址写入 ErrorEPC 寄存器 (CP0 reg 30, sel 0)
    ehb                             # clear hazards (makes sure write to ErrorPC has completed)
                                    # 清除流水线冒险，确保写入完成

    # Return from exception will now execute code in main
    eret                            # Exit reset exception handler and start execution of _start.
                                    # 从异常返回，PC 跳转到 ErrorEPC（即 _start）


/**************************************************************************************/
/**
 * 内核返回后的处理
 * 如果 _start 函数返回，系统进入死循环
 */
all_done:
    # If main returns it will return to this point.  Just spin here.
    # 如果内核返回到这里，进入无限循环
    b       all_done            # 无限循环
    nop                         # 延迟槽

END(__cpu_init)

# Inline the code: fill the rest of space between here and the next exception vector address.

#include "init_caches.S"

/**************************************************************************************
    B O O T   E X C E P T I O N   H A N D L E R S (CP0 Status[BEV] = 1)
    启动阶段的异常处理程序（当 BEV 位为 1 时有效）
**************************************************************************************/
/* NOTE: the linker script must insure that this code starts at start + 0x200 so the exception */
/* vectors will be addressed properly. All .org assume this! */
/* 注意：链接脚本必须确保这些代码位于正确的偏移位置 */

/**
 * TLB 重填异常向量 - 32位任务 (offset 0x200)
 * 当 TLB 查找失败时触发此异常
 */
.org 0x200                      # TLB refill, 32 bit task.
    sdbbp                       # This has the effect of starting the debugger
                                # 触发调试器断点（启动阶段不应该发生 TLB 异常）
    nop

/**
 * XTLB 重填异常向量 - 64位任务 (offset 0x280)
 * 用于 64 位地址空间的 TLB 重填
 */
.org 0x280                      # XTLB refill, 64 bit task. start + 0x280
    sdbbp                       # This has the effect of starting the debugger
                                # 64位 TLB 重填异常（本系统为 32 位，不应触发）
    nop

/**
 * 包含 CP0 初始化代码
 * init_cp0 函数位于此处，被 boot.S 调用
 */
#include "init_cp0.S"

/**
 * Cache 错误异常向量 (offset 0x300)
 * Cache 一致性错误或 ECC 错误时触发
 */
.org 0x300                      # Cache error exception. start + 0x300
    sdbbp                       # This has the effect of starting the debugger
                                # Cache 错误异常（启动阶段不应该发生）
    nop

/**
 * 包含 TLB 初始化代码
 * init_tlb 函数位于此处，被 boot.S 调用
 */
#include "init_tlb.S"


/**
 * 通用异常向量 (offset 0x380)
 * 所有其他异常（中断、系统调用等）的入口
 */
.org 0x380    /* General exception. */
    # display alternating one-zeros on board LEDs
    # 在启动阶段，通用异常处理只是简单返回
    # 实际的异常处理在 start.S 中的 _mips_general_exception
    .set    at                  # 临时允许使用 $at
    li      v0, 0x55555555      # Board LEDs display
                                # 加载交替的 0/1 模式到 v0
    li      v1, LEDS_ADDR       # 加载 LED 地址到寄存器
    sw      v0, 0(v1)           # alternating one-zeros
                                # 将模式写入 LED 地址（用于调试）
    .set    noat                # 恢复 noat 设置
    eret                        # Write pattern to LEDs
                                # 从异常返回
    nop

# If you want the above code to fit into 1k flash you will need to leave out the
# code below. This is the code that covers the debug exception which you normally will not get.
# 如果需要将代码压缩到 1KB Flash 内，可以省略下面的 EJTAG 调试异常处理

/**
 * EJTAG 调试异常向量 (offset 0x480)
 * JTAG 调试器触发的异常
 */
.org 0x480 /* EJTAG debug exception (EJTAG Control Register[ProbTrap] == 0.) */
    li      r24_malta_word, LEDS_ADDR     # If post boot then $24 is clobbered.
                                          # 加载 LED 地址（启动后 $24 可能被覆盖）
    mtc0    a0, C0_DESAVE       # DeSave a0 - 保存 a0 到调试保存寄存器
    mfc0    a0, C0_DEPC         # Read DEPC - 读取调试异常 PC
    sw      a0, 0(r24_malta_word)    # Display lower 16 bits of DEPC if there is not an EJTAG probe.
                                     # 在 LED 上显示异常 PC（如果没有 EJTAG 探针）
    mfc0    a0, C0_DESAVE       # Restore a0 - 恢复 a0
1:  b       1b  /* Stay here */  # 无限循环，等待调试器介入
    nop

