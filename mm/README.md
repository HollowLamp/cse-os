@panzhiyao
@2025.12.13
这是一个markdown文档，记录memory相关的内容

mm/                 # 内存管理模块
├── pmap.c          # 核心物理内存和虚拟内存管理（主文件）
├── pmap.all        # 可能是汇编文件，是通过pmap.c生成的一个汇编文件，暂时不需要管
├── Makefile        # 内存管理模块的Makefile
├── m32tlb_ops.S    # MIPS32 TLB操作汇编代码
├── tlb_asm.S       # TLB相关汇编代码
└── tlbop.c         # TLB操作C封装

inc/                # 头文件目录
├── pmap.h          # 内存管理相关声明（重要！）
├── mmu.h           # MMU相关定义（重要！）
├── tlbop.h         # TLB操作声明
├── m32c0.h         # MIPS CP0寄存器定义
├── trap.h          # 异常处理定义
├── types.h         # 基本类型定义
└── ...             # 其他头文件


# 内存管理模块函数状态表
| 函数名 | 功能描述

| `set_physic_mm()` | 探测物理内存大小 | 完成（硬编码） | ✅ |
| `alloc()` | 早期内存分配器 | 完成 | ✅ |
| `boot_pgdir_walk()` | 启动时页表遍历 | 完成 | ✅ |
| `boot_map_segment()` | 启动时内存映射 | 完成 | ✅ |
| `vm_init()` | 虚拟内存初始化 | 基本完成 | ✅ |
| `page_init()` | 物理页管理初始化 | 完成 | ✅ |
| `page_alloc()` | 分配物理页 | 完成 | ✅ |
| `page_free()` | 释放物理页 | 完成 | ✅ |
| `pgdir_walk()` | 通用页表遍历 | 完成 | ✅ |
| `page_insert()` | 插入页映射 | 完成 | ✅ |
| `page_lookup()` | 查找页映射 | 完成 | ✅ |
| `page_remove()` | 移除页映射 | 完成 | ✅ |
| `page_decref()` | 减少页引用 | 完成 | ✅ |
| `tlb_invalidate()` | 使TLB项失效 | 完成 | ✅ |
| `pageout()` | 缺页处理 | 完成 | ✅ |
| `va2pa()` | 地址转换 | 完成 | ✅ |
| `va2pa_print()` | 调试用地址转换 | 完成 | ✅ |


# 内存管理模块syscall部分代码-yjb
| 函数名 | 功能描述

| `sys_get_shm()` | 共享内存分配 | 完成 | ✅ |
| `sys_mem_alloc()` | 内存分配 | 完成 | ✅ |
| `sys_mem_map()` | 内存映射 | 完成 | ✅ |
| `sys_mem_unmap()` | 内存取消映射 | 完成 | ✅ |
