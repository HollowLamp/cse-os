#include <asm/regdef.h>
#include <asm/cp0regdef.h>
#include <asm/asm.h>
#include <stackframe.h>

# 以上不能删
.macro	__build_clear_sti
	STI
.endm

.macro	__build_clear_cli
	CLI
.endm


NESTED(handle_tlb, TF_SIZE, sp)
	nop
	SAVE_ALL # 把32个寄存器以及cp0以及几个特殊寄存器的内容按指定顺序存到栈中
	
    # 获取引发中断的虚拟地址
    mfc0    a0, CP0_BADVADDR     # CP0_BADVADDR 保存了引发异常的虚拟地址
    mfc0    a1, CP0_CONTEXT      # CP0_CONTEXT 保存了与虚拟地址相关的上下文信息

    # 调用 va2pa 函数，查找虚拟地址对应的物理地址
    jal     va2pa                # va2pa(a0, a1)
	                         # k0寄存器=0x1800，第11位和12位为1
                             # MANUALP143 ，表示页大小为4K

	                         # 引发中断的虚拟地址
							 # a0,a1是后面va2pa()的两个参数
	                         
							 # 这个函数会查页表，看有没有虚拟地址对应的物理页
	nop

    # 检查 va2pa 的返回值
    move    v0, v0               # va2pa 的返回值存储在 v0 中
    li      t0, 0xFFFFFFFF       # 全1表示没有找到对应的物理地址
    beq     v0, t0, page_fault   # 如果没有找到物理地址，跳转到 page_fault
    nop

	                         # V0是va2pa返回值，全1代表没有这个页，否则代表物理地址，相等表示需要新建页
	nop	
	# TLB 重填逻辑
    # 如果找到物理地址，填充 TLB 表项
    move    k1, v0               # v0 是物理地址
    srl     k1, k1, 12           # 逻辑右移12位，提取页号
    sll     k1, k1, 6            # 逻辑左移6位，清除低6位
    ori     k1, k1, 0x6          # 设置 D 位和 V 位为 1（可写、有效）
    mtc0    k1, CP0_ENTRYLO0     # 将物理地址写入 CP0_ENTRYLO0
    mtc0    zero, CP0_ENTRYLO1   # 清空 CP0_ENTRYLO1
    ehb                          # 确保前面的操作完成
    # 更新 TLB
    tlbwi                       # 写入 TLB 表项
    j       tlb_refill_done     # 跳转到完成处理
	                         # 不用新建页，只需要把映射关系填到tlb里
	nop

page_fault:  	# page fault occur，新建页
	jal     pageout
	               # 引发中断的虚拟地址
	               # 传参

	               # 创建页
	nop
	# 这里pageout()返回了物理地址在v0，直接过就行
    # pageout 返回物理地址在 v0 中
    move    k1, v0              # v0 是分配的新物理地址
    srl     k1, k1, 12          # 逻辑右移12位，提取页号
    sll     k1, k1, 6           # 逻辑左移6位，清除低6位
    ori     k1, k1, 0x6         # 设置 D 位和 V 位为 1（可写、有效）
    mtc0    k1, CP0_ENTRYLO0    # 将物理地址写入 CP0_ENTRYLO0
    mtc0    zero, CP0_ENTRYLO1  # 清空 CP0_ENTRYLO1
    ehb                         # 确保前面的操作完成

    # 更新 TLB
    tlbwi                       # 写入 TLB 表项
	
b1:   
	                # v0 is pa
	                # 逻辑右移12
	                # 逻辑左移6，最后6位清零，最前面6位清零
	                # D位和V位置为1

	
	                # 暂存，下面用来转成另一个地址
	                # 提取第12位，判断奇数还是偶数  
                    # 不为0，为奇
	nop
SET_ENTRYLO0:
	mtc0 	k1, CP0_ENTRYLO0  
	mtc0    zero,CP0_ENTRYLO1
	ehb
	and		a1, 0xFFFFEFFF 
	lw		a0, mCONTEXT 

	jal 	va2pa_print   
	nop
	li		t0, 0xFFFFFFFF  

	beq		v0, t0, tlb_refill_done
	nop
	move 	k1, v0		   # v0 is pa
	srl		k1, 12         # 逻辑右移12
	sll 	k1, 6          # 逻辑左移6，最后6位清零，最前面6位清零
	or		k1, 0x6        # D位和V位置为1
	mtc0    k1,CP0_ENTRYLO1
	j		tlb_refill_done
	nop	
SET_ENTRYLO1:
	
	mtc0    k1, CP0_ENTRYLO1
	mtc0    zero,CP0_ENTRYLO0
	ehb
	and		a1, 0xFFFFEFFF 
	lw		a0, mCONTEXT 
	jal 	va2pa_print  
	nop
	li		t0, 0xffffffff  
	beq		v0, t0, tlb_refill_done
	nop

	move 	k1, v0		   # v0 is pa
	srl		k1, 12         # 逻辑右移12
	sll 	k1, 6          # 逻辑左移6，最后6位清零，最前面6位清零
	or		k1, 0x6        # D位和V位置为1
	mtc0    k1,CP0_ENTRYLO0	

tlb_refill_done:
	# todo TLB重填
	RESTORE_ALL
	eret
	nop
	mtc0    v0, CP0_INDEX

oldTlb:	
	ehb # 确保前面所有操作已完成
	tlbwi
	nop
	j	ret_from_exception
	nop
	
END(handle_tlb)


NESTED(handle_mod, TF_SIZE, sp)
	nop
	# CLI
	.set at # 开启at寄存器警告
	move	a0, sp
	                 # 把函数入口传给v0
	
	nop
	j	ret_from_exception
	nop
END(handle_mod)

NESTED(handle_reserved, TF_SIZE, sp)   # 从中断中恢复中断返回用户态
	nop           # 空转指令确保安全，类似使得cpu指令按字对齐后取下一个未知的，使得CPU的PIPELINE不会错误的执行一个预取得指令
	# CLI         # 关中断
	.set at       # 允许汇编器使用$at寄存器

	nop
	j	simple_return
	nop
END(handle_reserved)

FEXPORT(ret_from_exception)
	.set noreorder

	RESTORE_ALL 
	nop
	# 判断是否嵌套
	
    mfc0    t0, CP0_STATUS        # 读取 CP0_STATUS 寄存器
    andi    t0, t0, 0x2           # 检查 EXL 位（位 1）
    bnez    t0, core_ret  # 如果 EXL 位为 1，跳转到嵌套异常处理
    nop                           # 延迟槽
	
	eret
	nop

nor_ret:             
	# STI
	eret
	nop
core_ret:
	j k1
	nop
	# 若是用户态 --> 内核态，则 k0 = sp, sp = *kernelsp - PT_SIZE，store k0, PT_R29(sp)，保存其它寄存器
	# 若是内核态 --> 内核态，直接 k0 = sp, sp = sp - PT_SIZE，store k0, PT_R29(sp)，然后保存其它寄存器

END(ret_from_exception)	

FEXPORT(simple_return)#没有restoreall
	.set noreorder

	nop
	# 判断是否嵌套
	mfc0	t1,CP0_EPC,2   
	li	t0,-0x80000000
	addu t0,t1
	ehb
	bgez t0,core_ret_1     #epc >= 0x80000000，嵌套
	nop            
	# STI
	eret
	nop
END(simple_return)

core_ret_1:
	j t1
	nop


NESTED(handle_addr, TF_SIZE, sp)
	nop
	li		sp,0x80400000#切到内核SP  
	# CLI
	.set at # 开启at寄存器警告
	mfc0 t0, CP0_CAUSE     # 取出上一次exception的cause
	mfc0 t2, CP0_STATUS    # 取出Processor status
	and t0, t2 
	andi t1, t0, STATUSF_IP0 # t0和立即数0X400相与，取出t0第10位，结果存t1，
	j print_addr_error
	
	nop
END(handle_addr)

NESTED(handle_mult_tlb, TF_SIZE, sp)  # machie check异常，多个tlb项相同，理论上不会触发
	nop
	# CLI
	.set at # 开启at寄存器警告
	nop

	mfc0 t1,CP0_STATUS
	nop
	                       # 清掉tlb多个重复的异常
	and		t1,t0
	mtc0	t1,CP0_STATUS
	ehb
	j	simple_return
	nop
	nop
END(handle_mult_tlb)


.set noreorder
# .align	5
NESTED(handle_int, TF_SIZE, sp)
.set at
nop

SAVE_TF
.set	noat # 关闭关于at寄存器的警告

# CLI
mfc0	t0, CP0_CAUSE  # 取出上一次exception的cause
mfc0	t2, CP0_STATUS # 取出Processor status

and	t0, t2  

andi	t1, t0, STATUSF_IP0   # t0和立即数0X400相与，取出t0第10位，结果存t1，

j timer_irq

nop
END(handle_int)

	.extern delay

button_deal:
	# 处理键盘



	nop


timer_irq:
	nop
	# 处理定时器



	nop




