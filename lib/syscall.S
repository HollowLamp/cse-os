#include <asm/regdef.h>
#include <asm/cp0regdef.h>
#include <asm/asm.h>
#include <stackframe.h>
#include <unistd.h>


NESTED(handle_sys,TF_SIZE, sp)
    # 保存所有寄存器到栈上
    SAVE_ALL
    nop
    .set at

    # 从 a0 寄存器获取系统调用号（用户通过 msyscall 的第一个参数传入）
    # a0 对应 TF_REG4，系统调用号格式为 __SYSCALL_BASE + 偏移
    lw      a0, TF_REG4(sp)         # 从栈中恢复 a0（系统调用号）

    # 减去基地址，得到系统调用表中的索引
    subu    a0, a0, __SYSCALL_BASE  # a0 = a0 - 9527，得到实际索引 (0, 1, 2, ...)

    # 检查系统调用号是否在合法范围内
    # 如果 a0 < __NR_SYSCALLS + 1，则 t0 = 1，否则 t0 = 0
    sltiu   t0, a0, __NR_SYSCALLS + 1

    # 设置 EPC 为下一条指令（系统调用返回后从 syscall 的下一条指令继续执行）
    lw      t1, TF_EPC(sp)          # 从栈中读取 EPC
    addiu   t1, t1, 4               # EPC + 4
    sw      t1, TF_EPC(sp)          # 保存回栈中

    # 如果系统调用号非法，跳转到错误处理
    beq     t0, zero, illegal_syscall
    nop

    # 计算系统调用函数在表中的地址
    sll     t0, a0, 2               # t0 = 索引 * 4（每个函数指针占 4 字节）
    la      t1, sys_call_table      # t1 = 系统调用表基地址
    addu    t1, t1, t0              # t1 = 表基址 + 偏移
    lw      t2, 0(t1)               # t2 = 函数指针

    # 检查函数指针是否为空
    beq     t2, zero, illegal_syscall
    nop

    # 设置返回地址，执行系统调用
    la      ra, handle_return       # 系统调用返回后跳转到 handle_return
    jalr    t2                      # 跳转执行系统调用函数
    nop

handle_return:
    # 系统调用返回值在 v0 中，需要保存到栈上的 TF_REG2 位置
    # 这样异常返回时会恢复到 v0 寄存器，用户程序就能拿到返回值
    sw      v0, TF_REG2(sp)

    j       ret_from_exception
    nop

illegal_syscall:
    # 打印错误信息
    move    a0, t0
    jal     print_illegal
    nop
    j       ret_from_exception
    nop

END(handle_sys)

    .extern sys_putchar
    .extern sys_getenvid
    .extern sys_get_shm
    .extern sys_env_create
    .extern sys_set_pgfault_handler
    .extern sys_mem_alloc
    .extern sys_mem_map
    .extern sys_mem_unmap
    .extern sys_pthread_create
    .extern sys_set_env_status
    .extern sys_set_trapframe
    .extern sys_panic
    .extern sys_ipc_can_send
    .extern sys_ipc_recv
    .extern sys_free_myself
    .extern sys_write_dev
    .extern sys_read_dev
    .extern sys_printf
    .extern sys_set_leds
    .extern sys_get_switchs
    .extern sys_readline
    .extern sys_env_create_1
    .extern sys_mkdir
    .extern sys_cd
    .extern sys_fcraete
    .extern sys_fread
    .extern sys_fwrite
    .extern sys_ls
    .extern sys_rm
    .extern sys_rt_write_byte
    .extern sys_rt_require_device
    .extern sys_rt_release_device
    .extern sys_rt_claim_device
    .extern sys_rt_write_by_num
    # //Overview:
    # //syscalltable stores all the syscall function s entrypoints

.macro syscalltable
    .word sys_putchar
    .word sys_getenvid
    .word sys_get_shm
    .word sys_env_create
    .word sys_set_pgfault_handler
    .word sys_mem_alloc
    .word sys_mem_map
    .word sys_mem_unmap
    .word sys_pthread_create
    .word sys_set_env_status
    .word sys_set_trapframe
    .word sys_panic
    .word sys_ipc_can_send
    .word sys_ipc_recv
    .word sys_free_myself
    .word sys_write_dev
    .word sys_read_dev
    .word sys_printf
    .word sys_set_leds
    .word sys_get_switchs
    .word sys_readline
    .word sys_env_create_1
    .word sys_mkdir
    .word sys_cd
    .word sys_fcraete
    .word sys_fread
    .word sys_fwrite
    .word sys_ls
    .word sys_rm
    .word sys_rt_write_byte
    .word sys_rt_require_device
    .word sys_rt_release_device
    .word sys_rt_claim_device
    .word sys_rt_write_by_num
    .word sys_rt_exit
.endm
EXPORT(sys_call_table)

    syscalltable
.size sys_call_table, . - sys_call_table
