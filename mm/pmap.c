#include <tlbop.h>
#include <env.h>
#include <mmu.h>
#include <types.h>
#include <pmap.h>
#include <error.h>
#include <hash.h>

/* These variables are set by set_physic_mm() */
unsigned long maxpa;   /* Maximum physical address */
unsigned long npage;   /* Amount of memory(in pages) */
unsigned long basemem; /* Amount of base memory(in bytes) */
unsigned long extmem;  /* Amount of extended memory(in bytes) */
unsigned long tlbCount=0;
Pde *boot_pgdir;   /* 内核启动页目录指针，用于早期虚拟内存管理 */
#define maxTLB 16
struct Page *pages;
static unsigned long freemem;

/* 变量 page_free_list 来以链表的形式表示所有的空闲物理内存 */
static struct Page* page_free_list; /* Free list of physical pages */
struct HashTable ht;

unsigned long getNextTlb()//要写的下一个tlb
{
    printf("nexttlb:%lu\n",tlbCount);
    return tlbCount++;
}
/********************* Private Functions *********************/
/**
 * 将Page结构体指针转换为物理页号
 * @param pp Page结构体指针
 * @return 物理页号
 * 
 * 原理：pages数组是所有Page结构体的起始地址，通过指针减法计算偏移量得到页号
 * 例如：pages是0x00，pp是0x80，则返回0x80 / sizeof(struct Page)
 */
u_long
page2ppn(struct Page *pp)
{
    return pp - pages;
}

/**
 * 将Page结构体指针转换为物理地址
 * @param pp Page结构体指针
 * @return 物理地址
 * 
 * 原理：先转换为页号，再左移PGSHIFT位（通常是12位）得到物理地址
 * PGSHIFT表示页内偏移的位数，4K页对应12位
 */
u_long
page2pa(struct Page *pp)
{
    return page2ppn(pp) << PGSHIFT;
}

/**
 * 将物理地址转换为Page结构体指针
 * @param pa 物理地址
 * @return Page结构体指针
 * 
 * 原理：通过PPN宏获取页号，然后在pages数组中找到对应的Page结构体
 * 如果页号超出总页数npage，则调用panic函数终止程序
 */
struct Page *
pa2page(u_long pa)
{
    if (PPN(pa) >= npage)
        panic("pa2page called with invalid pa: %x\n", pa);
    return &pages[PPN(pa)];
}

/**
 * 将Page结构体指针转换为内核虚拟地址
 * @param pp Page结构体指针
 * @return 内核虚拟地址
 * 
 * 原理：先转换为物理地址，再通过KADDR宏转换为内核虚拟地址
 * KADDR宏用于将物理地址映射到内核虚拟地址空间
 */
u_long
page2kva(struct Page *pp)
{
    return KADDR(page2pa(pp));
}

/**
 * 将虚拟地址转换为物理地址
 * @param pgdir 页目录指针
 * @param va 虚拟地址
 * @return 物理地址，如果映射不存在则返回全1
 * 
 * 步骤：
 * 1. 使用PDX宏获取一级页表索引，找到对应的一级页表项
 * 2. 检查一级页表项是否有效（PTE_V位是否设置）
 * 3. 如果有效，通过KADDR和PTE_ADDR获取二级页表的虚拟地址
 * 4. 使用PTX宏获取二级页表索引，找到对应的二级页表项
 * 5. 检查二级页表项是否有效
 * 6. 如果有效，返回PTE_ADDR提取的物理地址
 * 
 * 注意：此函数用于内核态查询虚拟地址的物理映射
 */
u_long va2pa(Pde* pgdir, u_long va)
{
    Pte* p;
    Pde* pd_entry;

    /* Step 1: 获取一级页表项 */
    pd_entry = &pgdir[PDX(va)];

    /* Step 2: 检查一级页表项是否有效 */
    if (!(*pd_entry & PTE_V)) {
        return ~0; // 无效则返回全1
    }

    /* Step 3: 获取二级页表的虚拟地址 */
    // 必须先提取物理地址，再转换
    u_long pgtable_pa = PTE_ADDR(*pd_entry);
    p = (Pte*)KADDR(pgtable_pa);

    /* Step 4: 检查二级页表项是否有效 */
    if (!(p[PTX(va)] & PTE_V)) {
        return ~0;
    }

    /* Step 5: 返回物理地址 */
    return PTE_ADDR(p[PTX(va)]);
}
/**
 * 调试用的虚拟地址到物理地址转换（带打印）
 * @param pgdir 页目录指针
 * @param va 虚拟地址
 * @return 物理地址，如果映射不存在则返回全1
 *
 * 注意：此函数仅用于调试，不应在生产代码中使用
 */
u_long va2pa_print(Pde* pgdir, u_long va)
{
    Pte* p;
    Pde* pd_entry;

    printf("\n@@@ tlb-va2pa: 0x%08x   epc：0x%08x\n", va, get_epc());

    pd_entry = &pgdir[PDX(va)];

    if (!(*pd_entry & PTE_V)) {
        printf("  [一级页表无效] PDX=%d, pde=0x%08x\n", PDX(va), *pd_entry);
        return ~0;
    }

    /* 修复：先提取物理地址，再转换 */
    u_long pgtable_pa = PTE_ADDR(*pd_entry);
    p = (Pte*)KADDR(pgtable_pa);

    if (!(p[PTX(va)] & PTE_V)) {
        printf("  [二级页表无效] PTX=%d, pte=0x%08x\n", PTX(va), p[PTX(va)]);
        return ~0;
    }

    u_long pa = PTE_ADDR(p[PTX(va)]);
    printf("  [找到映射] va=0x%08x -> pa=0x%08x\n", va, pa);

    return pa;
}

void print_illegal(int num)
{
    printf("illegal syscall num :%d ,epc: 0x%x\n",num/4,get_epc());
    while(1);
}

// env释放
void print_addr_error()
{
    printf("\n### addr exception (see manual p120)###\n");
    printf("### epc：0x%x  badaddr: 0x%x status: 0x%x\n",get_epc(),get_badaddr(),get_status());
    // while(1);
    env_free(curenv);

}


/**
 * 确定内核可用的物理内存的大小和范围
 * Overview:
 *      Initialize basemem and npage.
 *      Set basemem to be 64MB, and calculate corresponding npage value.
 */
void set_physic_mm()
{
    /**
     * Step 1: Initialize basemem.
     * (When use real computer, CMOS tells us how many kilobytes there are).
     */
    maxpa = 250 * 1024 * 1024; // Set maximum physical address  250M=0X 0FA0 0000
    basemem = maxpa;          // Amount of base memory equals 
    /* Step 2: Calculate corresponding npage value. */
    npage = maxpa / BY2PG; // Amount of pages equal to maximum physical address divided by page size (4KB)
    extmem = 0;            // In our platform there is no extended memory
    printf("Physical memory: %dK available, ", (int)(maxpa / 1024));
    printf("base = %dK, extended = %dK\n", (int)(basemem / 1024), (int)(extmem / 1024));
}

/**
 * 分配指定字节的物理内存
 * @param n 需要分配的字节数
 * @param align 对齐要求
 * @param clear 是否将分配的内存清零
 * @return 分配的内存的虚拟地址
 * 
 * 特性：
 * - 能够按照参数align进行对齐
 * - 根据参数clear的设定决定是否将新分配的内存全部清零
 * - 仅在设置虚拟内存系统时使用
 * 
 * 步骤：
 * 1. 如果是第一次调用，初始化freemem为内核代码和全局变量之后的第一个虚拟地址
 *    (end是在scse0_3.lds中定义的符号，指向内核代码段结束后的地址)
 * 2. 将freemem向上对齐到指定的align值
 * 3. 保存当前的freemem作为分配的内存块
 * 4. 将freemem增加ROUNDUP(n, align)，记录已分配的内存
 * 5. 如果clear为true，使用memset将分配的内存块清零
 * 6. 检查是否超出物理内存的最大地址maxpa，如果是则panic
 * 7. 返回分配的内存块的虚拟地址
 */
static void *alloc(u_int n, u_int align, int clear)
{
    extern char end[]; // scse0_3.lds中定义，end地址在0x80400000
    u_long alloced_mem;

    // 初始化freemem，如果是第一次调用
    // freemem指向内核代码和全局变量之后的第一个虚拟地址
    if (freemem == 0)
    {
        freemem = (u_long)end;
    }

    // Step 1: 将freemem向上对齐到指定的align值
    freemem = ROUNDUP(freemem, align);

    // Step 2: 保存当前的freemem作为分配的内存块
    alloced_mem = freemem;

    // Step 3: 增加freemem，记录已分配的内存
    freemem += ROUNDUP(n, align);

    // Step 4: 如果clear为true，将分配的内存块清零
    if (clear)
    {
        memset((void*)alloced_mem, 0, ROUNDUP(n, align));
    }

    // 检查是否超出物理内存的最大地址
    if (PADDR(freemem) >= maxpa)
    {
        panic("out of memory\n");
    }

    // Step 5: 返回分配的内存块的虚拟地址
    return (void*)alloced_mem;
}

/**
 * 获取虚拟地址va对应的页表项物理地址的指针
 * @param pgdir 页目录指针（一级页表）
 * @param va 虚拟地址
 * @param create 如果为1，则在二级页表不存在时创建
 * @return 页表项的指针
 * 
 * 功能：
 * - 查找va对应的页表项
 * - 如果二级页表不存在且create为1，则创建二级页表
 * - 仅在启动时设置虚拟内存系统使用
 * 
 * 参数说明：
 * - pgdir_entryp: 一级页表项指针，指向va对应的一级页表项
 * - pgtable: 二级页表的虚拟地址
 * - pgtable_entry: 最终要返回的页表项指针
 * 
 * 步骤：
 * 1. 获取va对应的一级页表项
 * 2. 检查一级页表项是否有效
 * 3. 如果无效且create为1，则分配一个新的二级页表
 * 4. 获取va在二级页表中对应的页表项
 * 5. 返回页表项的指针
 */
static Pte* boot_pgdir_walk(Pde* pgdir, u_long va, int create)
{
    Pde* pgdir_entryp;
    Pte* pgtable;
    Pte* pgtable_entry;

    /* Step 1: 获取va对应的一级页表项 */
    pgdir_entryp = &pgdir[PDX(va)];

    /* Step 2: 检查一级页表项是否有效 */
    if ((*pgdir_entryp & PTE_V) == 0x0) {
        /* 一级页表项无效 */
        if (create) {
            // 分配一个新的二级页表
            pgtable = alloc(BY2PG, BY2PG, 1);

            // 设置一级页表项
            *pgdir_entryp = PADDR(pgtable) | PTE_V;

            // 注意：此时pgtable已经是虚拟地址，不需要KADDR转换
        }
        else {
            return 0; // 不允许创建则返回NULL
        }
    }
    else {
        /* 一级页表项有效，获取二级页表的虚拟地址 */
        // 这里需要先用PTE_ADDR提取物理地址，再用KADDR转换
        u_long pgtable_pa = PTE_ADDR(*pgdir_entryp);
        pgtable = (Pte*)KADDR(pgtable_pa);
    }

    /* Step 3: 获取va对应的页表项 */
    pgtable_entry = &pgtable[PTX(va)];

    return pgtable_entry;
}

/**
 * 将虚拟地址范围映射到物理地址范围
 * @param pgdir 页目录指针（一级页表）
 * @param va 起始虚拟地址
 * @param size 映射大小（字节数）
 * @param pa 起始物理地址
 * @param perm 页表项权限位（如PTE_R, PTE_W等）
 * 
 * 功能：
 * - 在以pgdir为根的页表中，将虚拟地址范围[va, va+size)映射到物理地址范围[pa, pa+size)
 * - 每个页表项的权限位设置为perm | PTE_V（有效位必须设置）
 * - 用于启动时建立内核空间的内存映射（如内核代码、数据段、设备内存等）
 * 
 * 前置条件：
 * - size必须是页大小BY2PG（4KB）的整数倍
 * - va和pa必须是页对齐的（即低12位为0）
 * 
 * 参数说明：
 * - i: 循环计数器，用于遍历所有需要映射的页
 * - va_temp: 当前处理的虚拟地址
 * - pa_temp: 当前处理的物理地址
 * - pgtable_entry: 指向当前虚拟地址对应的页表项（二级页表中的条目）
 * 
 * 步骤：
 * 1. 检查size是否为页大小的整数倍，若不是则panic
 * 2. 初始化临时虚拟地址和物理地址为起始地址
 * 3. 遍历每一页需要映射的内存：
 *    a. 通过boot_pgdir_walk找到当前虚拟地址对应的页表项
 *    b. 设置页表项的值为物理地址|权限位|有效位
 *    c. 更新临时虚拟地址和物理地址，指向下一页
 */
void boot_map_segment(Pde *pgdir, u_long va, u_long size, u_long pa, int perm)
{
    int i;              // 循环计数器，遍历所有待映射的页
    u_long va_temp;     // 当前处理的虚拟地址（修正为u_long以支持完整32位地址范围）
    u_long pa_temp;     // 当前处理的物理地址（修正为u_long以支持完整32位地址范围）
    Pte *pgtable_entry; // 指向当前虚拟地址对应的页表项

    /* Step 1: 检查size是否为页大小BY2PG的整数倍 */
    if (size % BY2PG != 0)
    {
        panic("pmap.c: size not aligned to page boundary");
    }

    /* Step 2: 遍历虚拟地址范围，建立映射关系 */
    va_temp = va;
    pa_temp = pa;
    for (i = 0; i < size / BY2PG; i++)
    {
        /* 找到当前虚拟地址对应的页表项，若不存在则创建 */
        pgtable_entry = boot_pgdir_walk(pgdir, va_temp, 1);
        
        /* 设置页表项：物理地址 | 权限位 | 有效位 */
        *pgtable_entry = pa_temp | perm | PTE_V;
        
        /* 更新虚拟地址和物理地址，处理下一页 */
        va_temp += BY2PG;
        pa_temp += BY2PG;
    }
}

/**
 * 初始化虚拟内存系统，建立二级页表，并为内核关键数据结构分配内存和建立映射
 * 
 * 功能：
 * - 分配并初始化内核页目录（一级页表）
 * - 映射内核代码和数据段到虚拟地址空间
 * - 映射硬件设备内存（控制台、MMIO等）
 * - 为物理页管理数组(pages)分配内存并建立映射
 * - 为进程控制块数组(envs)分配内存并建立映射
 * - 是操作系统虚拟内存系统的核心初始化函数
 * 
 * 参数说明：
 * - pgdir: 内核页目录指针（一级页表）
 * - n: 用于计算对齐后的内存大小
 * 
 * 步骤：
 * 1. 分配一页内存作为内核页目录，并将其设置为全局boot_pgdir
 * 2. 建立内核代码和数据段的映射
 * 3. 建立控制台内存的映射，用于系统输出
 * 4. 建立MMIO设备内存的映射，用于设备访问
 * 5. 为pages数组分配内存，用于物理页管理
 * 6. 将pages数组映射到虚拟地址UPAGES
 * 7. 为envs数组分配内存，用于进程管理
 * 8. 将envs数组映射到虚拟地址UENVS
 * 9. 输出初始化成功信息
 */
void vm_init()
{
    extern char end[];           // 内核代码段结束后的地址（在scse0_3.lds中定义）
    extern int mCONTEXT;         // 存储页目录指针的全局变量
    extern struct Env *envs;     // 进程控制块数组指针

    Pde *pgdir;                  // 内核页目录指针
    u_int n;                     // 临时变量，用于计算对齐后的内存大小

    /* Step 1: 分配一页内存作为内核页目录（一级页表） */
    pgdir = alloc(BY2PG, BY2PG, 1); // 分配4KB页对齐的内存，清零
    printf("to memory %x for struct page directory. \n", pgdir);
    mCONTEXT = (int)pgdir;       // 将页目录指针保存到全局变量mCONTEXT
    boot_pgdir = pgdir;          // 设置全局boot_pgdir指针指向内核页目录
    
    /* Step 2: 映射内核代码和数据段 */
    // 虚拟地址范围：[KERNBASE, end)
    // 物理地址范围：[PADDR(KERNBASE), PADDR(end))
    // 权限：可写（PTE_W）
    boot_map_segment(pgdir, KERNBASE, (u_long)end - KERNBASE, PADDR(KERNBASE), PTE_W);
    
    /* Step 3: 映射控制台内存 */
    // 虚拟地址范围：[0x10000000, 0x10000200)
    // 物理地址范围：[0x10000000, 0x10000200)
    // 权限：可写（PTE_W）
    boot_map_segment(pgdir, 0x10000000, 0x200, 0x10000000, PTE_W);
    
    /* Step 4: 映射MMIO设备内存 */
    // 虚拟地址范围：[DEVSPACE, DEVSPACE + 0x100000)
    // 物理地址范围：[0x100000, 0x200000)
    // 权限：可写（PTE_W）
    boot_map_segment(pgdir, DEVSPACE, 0x100000, 0x100000, PTE_W);
    
    /* Step 5: 为物理页管理数组pages分配内存并建立映射 */
    // pages数组用于管理所有物理页，每个物理页对应一个struct Page结构
    pages = alloc(npage * sizeof(struct Page), BY2PG, 1);
    printf("to memory %x for struct Pages.\n", pages);
    
    // 计算对齐后的内存大小（确保是页大小的整数倍）
    n = ROUNDUP(npage * sizeof(struct Page), BY2PG);
    
    // 将pages数组映射到虚拟地址UPAGES
    // 虚拟地址：UPAGES
    // 物理地址：PADDR(pages)
    // 权限：可写（PTE_W）
    boot_map_segment(pgdir, UPAGES, n, PADDR(pages), PTE_W);
    
    /* Step 6: 为进程控制块数组envs分配内存并建立映射 */
    // envs数组用于管理所有进程，每个进程对应一个struct Env结构
    envs = alloc(NENV * sizeof(struct Env), BY2PG, 1);
    printf("to memory %x for struct Envs.\n", envs);
    
    // 计算对齐后的内存大小
    n = ROUNDUP(NENV * sizeof(struct Env), BY2PG);
    
    // 将envs数组映射到虚拟地址UENVS
    // 虚拟地址：UENVS
    // 物理地址：PADDR(envs)
    // 权限：可写（PTE_W）
    boot_map_segment(pgdir, UENVS, n, PADDR(envs), PTE_W);
    
    /* Step 7: 输出初始化成功信息 */
    printf("mips_vm_init:boot_pgdir is %x\n", boot_pgdir);
    printf("pmap.c:\t mips vm init success\n");
}

/**
 * 初始化物理页管理系统，建立物理页的空闲链表
 * 
 * 功能：
 * - 初始化物理页控制块数组pages
 * - 建立物理页的空闲链表page_free_list
 * - 标记已使用的物理页（内核代码、数据、页表等）
 * - 标记空闲的物理页并将其加入空闲链表
 * - 是物理内存管理的核心初始化函数
 * 
 * 工作原理：
 * - pages数组为每个物理页分配一个struct Page控制块，用于跟踪页的引用计数和空闲状态
 * - page_free_list是一个双向链表，用于管理所有空闲的物理页
 * - 物理页的引用计数pp_ref表示该页被映射的次数，当引用计数为0时，页可以被释放
 * 
 * 参数说明：
 * - i: 循环计数器，用于遍历所有物理页
 * - pa: 物理地址临时变量（未使用，但保留以保持代码兼容性）
 * 
 * 步骤：
 * 1. 初始化空闲链表page_free_list
 * 2. 将freemem向上对齐到页边界，确保只有完整的页被分配
 * 3. 标记freemem以下的物理页为已使用（引用计数为1）
 * 4. 标记freemem以上的物理页为空闲（引用计数为0），并将其插入到空闲链表
 */
void page_init(void)
{
    int i = 0;
    u_long pa;
    
    /* Step 1: Initialize page_free_list. */
    page_free_list = NULL;
    
    /* Step 2: Align `freemem` up to multiple of BY2PG. */
    freemem = ROUNDUP(freemem, BY2PG);
    
    /* Step 3: Mark all memory below `freemem` as used(set `pp_ref`
     * filed to 1) */
    for (i = 0; i < PPN(freemem); i++) {
        struct Page *pp = &pages[i];
        pp->pp_ref = 1;
    }
    
    /* Step 4: Mark the other memory as free. */
    for (; i < npage; i++) {
        struct Page *pp = &pages[i];
        pp->pp_ref = 0;
        // 使用头插法将页添加到空闲链表
        pp->pp_link = page_free_list;
        page_free_list = pp;
    }
    
    printf("page_init: free memory from page %d to %d\n", PPN(freemem), npage - 1);
}

/**
 * 从空闲链表中分配一页物理内存，并将其内容清零
 * 
 * @param pp 指向物理页指针的指针，用于返回分配的物理页控制块
 * @return 0 表示分配成功，-E_NO_MEM 表示内存不足
 * 
 * 功能：
 * - 从空闲链表page_free_list中分配一页物理内存
 * - 将分配的物理页内容清零，确保数据安全
 * - 更新物理页的引用计数和空闲状态
 * - 将分配的物理页控制块指针返回给调用者
 * 
 * 后置条件：
 * - 如果分配成功，*pp指向分配的物理页控制块，返回0
 * - 如果内存不足（空闲链表为空），*pp为NULL，返回-E_NO_MEM
 * 
 * 注意事项：
 * - 该函数会将物理页的引用计数设置为1，表示该页已被分配
 * - 调用者在使用该页进行映射时，需要根据情况适当增加引用计数
 * - 分配的物理页已经被清零，可以直接使用而不会包含旧数据
 * 
 * 工作原理：
 * - 空闲链表page_free_list维护了所有可用的物理页
 * - 从链表头部获取页可以保证分配的效率（O(1)时间复杂度）
 * - 使用bzero将页内容清零，防止信息泄露和数据错误
 * - 将页从空闲链表中移除，表示该页已被占用
 * 
 * 步骤：
 * 1. 检查空闲链表是否为空，如果为空则返回错误
 * 2. 从空闲链表头部获取第一个空闲页
 * 3. 将该页的虚拟地址内容清零
 * 4. 将该页从空闲链表中移除
 * 5. 设置该页的引用计数为1
 * 6. 将分配的物理页控制块指针通过pp参数返回给调用者
 * 7. 返回0表示分配成功
 */
int page_alloc(struct Page **pp)
{
    struct Page *ppage_temp; // 临时存储分配的物理页控制块
    
    /* Step 1: 检查空闲链表是否为空 */
    if (page_free_list == NULL) {
        *pp = NULL;          // 空闲链表为空，设置*pp为NULL
        return -E_NO_MEM;    // 返回内存不足错误
    }
    
    /* Step 2: 从空闲链表头部获取一个空闲页 */
    ppage_temp = page_free_list;
    
    /* Step 3: 将分配的物理页内容清零 */
    // page2kva将物理页控制块转换为虚拟地址
    // BY2PG是页大小（4KB）
    bzero(page2kva(ppage_temp), BY2PG);
    
    /* Step 4: 将该页从空闲链表中移除 */
    page_free_list = ppage_temp->pp_link;
    
    /* Step 5: 设置该页的引用计数为1 */
    ppage_temp->pp_ref = 1;
    
    /* Step 6: 将分配的物理页控制块指针返回给调用者 */
    *pp = ppage_temp;
    
    /* Step 7: 返回分配成功 */
    return 0;
}


int page_alloc_share(struct Page **pp)
{
    struct Page *ppage_temp;
    
    /* Step 1: 检查空闲链表是否为空 */
    if (page_free_list == NULL) {
        *pp = NULL;
        return -E_NO_MEM;
    }
    
    /* Step 2: 从空闲链表头部获取一个空闲页 */
    ppage_temp = page_free_list;
    
    /* Step 3: 将分配的物理页内容清零 */
    bzero(page2kva(ppage_temp), BY2PG);
    
    /* Step 4: 将该页从空闲链表中移除 */
    page_free_list = ppage_temp->pp_link;
    
    /* Step 5: 设置该页的引用计数为1 */
    ppage_temp->pp_ref = 1;
    
    /* Step 6: 将分配的物理页控制块指针返回给调用者 */
    *pp = ppage_temp;
    
    /* Step 7: 返回分配成功 */
    return 0;
}

// 共享内存
struct Page* create_share_vm(int key, size_t size)
{
    struct Page* value = NULL;
    value = tryHashTableFind(&ht, key, value);
    if( value != NULL)
    {
        printf(" ### find shared entry ### %x \n",value);
        return value;
    }
    else
    {
        printf(" ### create_share_vm ### \n");
        //默认申请size小于一个页先 todo
        struct Page *p = NULL;
        uint32_t entry_point;
        u_long r;
        u_long perm;
        /*Step 1: alloc a page. */
        if ((r = page_alloc(&p)) < 0) {
            panic("create_share_vm: page_alloc failed");
            return NULL;
        }
        
        // 将页面标记为已使用，引用计数加1
        p->pp_ref++;
        
        // 将页面添加到哈希表中
        tryHashTableInsert(&ht, key, p);
        printf(" ####### insert shared page entry####### %x \n",p);
        return p;
    }
}

//共享内存页加入当前虚拟地址中
void* insert_share_vm(struct Env *e, struct Page *p)
{
    u_long perm;
    u_long r;
    perm = PTE_V | PTE_R | PTE_W | PTE_U;
    
    // 在用户堆区域分配一个虚拟地址，向下移动两个页
    u_long va = e->heap_pc - 2 * BY2PG;
    
    /*Step 2: Use appropriate perm to set initial stack for new Env. */
    /*Hint: The user-stack should be writable? */
    r = page_insert(e->env_pgdir, p, va, perm);
    if (r < 0) {
        printf("error,load_icode:page_insert failed\n");
        return NULL;
    }
    int *result = (int *)va;
    return result;
}

/**
 * 释放物理页，如果引用计数为0则将其加入空闲链表
 * 
 * @param pp 指向要释放的物理页控制块的指针
 * 
 * 功能：
 * - 释放指定的物理页，根据引用计数决定是否将其加入空闲链表
 * - 维护物理页的引用计数，确保正确的内存释放
 * - 防止重复释放和释放未分配的页
 * 
 * 前置条件：
 * - pp必须指向一个已分配的物理页控制块
 * - 该页的引用计数必须大于0
 * 
 * 工作原理：
 * - 物理页的引用计数pp_ref表示该页被映射的次数
 * - 当引用计数大于1时，减少引用计数表示减少一次映射
 * - 当引用计数等于1时，减少引用计数后变为0，表示该页不再被任何进程映射
 * - 当引用计数为0时，将该页加入空闲链表，表示该页可以被重新分配
 * 
 * 注意事项：
 * - 不能释放引用计数为0的页（已空闲的页）
 * - 该函数不会立即回收物理页的内存，只有当引用计数为0时才会将其加入空闲链表
 * - 调用者需要确保在不再使用该页的映射时调用此函数
 * 
 * 步骤：
 * 1. 检查物理页的引用计数是否为0，如果是则panic（防止重复释放）
 * 2. 如果引用计数大于1，则将其减1并返回（减少一次映射）
 * 3. 如果引用计数等于1，则将其减1（变为0）
 * 4. 将该页加入空闲链表，表示该页现在可以被重新分配
 */
void page_free(struct Page *pp)
{
    /* Step 1: 检查物理页是否已经是空闲状态（引用计数为0） */
    if (pp->pp_ref == 0) {
        panic("page_free: page already free");
    }

    /* Step 2: 如果引用计数大于1，减少引用计数并返回 */
    if (pp->pp_ref > 1) {
        pp->pp_ref--;  // 减少一次映射
        return;
    }

    /* Step 3: 如果引用计数等于1，减少引用计数并将页加入空闲链表 */
    pp->pp_ref--;  // 引用计数变为0，表示该页不再被任何进程映射
    /* Insert the page to the free list */
    pp->pp_link = page_free_list;
    page_free_list = pp;
}

/**
 * 地址转换和页表创建（create 为 1）
 * 在空闲链表初始化之后发挥功能，直接使用 page_alloc 函数从空闲链表中以页为单位进行内存的申请
 * Overview:
 *      Given `pgdir`, a pointer to a page directory, pgdir_walk returns a pointer
 *      to the page table entry (with permission PTE_R|PTE_V) for virtual address 'va'.
 * Pre-Condition:
 *      The `pgdir` should be two-level page table structure.
 * Post-Condition:
 *      If we're out of memory, return -E_NO_MEM.
 *      Else, we get the page table entry successfully, store the value of page table
 *      entry to *ppte, and return 0, indicating success.
 * 
 *      We use a two-level pointer to store page table entry and return a state code to indicate
 *      whether this function execute successfully or not.
 *      This function have something in common with function `boot_pgdir_walk`.
 */
int pgdir_walk(Pde *pgdir, u_long va, int create, Pte **ppte)
{
    Pde *pde; //一级页表项
    Pte *pgtab; //二级页表
    struct Page *pp; //用于分配页表的物理页

    /* Step 1: Find corresponding page directory entry. */
    pde = &pgdir[PDX(va)];

    /* Step 2: If the page directory entry is valid, get the page table. */
    if (*pde & PTE_V) {
        pgtab = (Pte *)KADDR(PTE_ADDR(*pde));
    } else {
        /* Step 3: If the page directory entry is invalid, and `create` is set,
         * create a new page table. Otherwise, return failure. */
        if (!create) {
            if (ppte) {
                *ppte = NULL;
            }
            return -E_NO_MEM;
        }

        /* Step 3.1: Allocate a new page table. */
        if (page_alloc(&pp) < 0) {
            if (ppte) {
                *ppte = NULL;
            }
            return -E_NO_MEM;
        }
        pgtab = (Pte *)KADDR(page2pa(pp));
        memset(pgtab, 0, BY2PG);

        /* Step 3.2: Set the page directory entry. */
        *pde = page2pa(pp) | PTE_V;
        pp->pp_ref++;
    }

    /* Step 4: Return the corresponding page table entry. */
    if (ppte) {
        *ppte = &pgtab[PTX(va)];
    }
    return 0;
}

/**
 * 将 va 虚拟地址和其要对应的物理页 pp 的映射关系以 perm 的权限设置加入页目录.
 * 先判断 va 是否有对应的页表项: 如果页表项有效（或者叫 va 是否
 * 已经有了映射的物理地址）的话，则去判断这个物理地址是不是我们要插入的那个物理
 * 地址，如果不是，那么就把该物理地址移除掉；如果是的话，则修改权限，放到 tlb 中.
 * Overview:
 *      Map the physical page 'pp' at virtual address 'va'.
 *      The permissions (the low 12 bits) of the page table entry should be set to 'perm|PTE_V'.
 * Post-Condition:
 *      Return 0 on success
 *      Return -E_NO_MEM, if page table couldn't be allocated
 * 
 *      If there is already a page mapped at `va`, call page_remove() to release this mapping.
 *      The `pp_ref` should be incremented if the insertion succeeds.
 */
int page_insert(Pde* pgdir, struct Page* pp, u_long va, u_int perm)
{
    Pte* pte;
    int r;

    /* Step 1: Get the page table entry for virtual address `va`. */
    if ((r = pgdir_walk(pgdir, va, 1, &pte)) < 0) {
        printf("page_insert: pgdir_walk failed for va=0x%08x\n", va);
        return r;
    }

    /* Step 2: If the page table entry exists(valid), remove the old mapping. */
    if (*pte & PTE_V) {
        /* 检查是否是映射到同一个物理页 */
        if (PTE_ADDR(*pte) == page2pa(pp)) {
            /* 同一个页面，只需更新权限 */
            *pte = page2pa(pp) | perm | PTE_V;

            /* 如果权限变化，需要使TLB失效 */
            if ((*pte & (PTE_R | PTE_W | PTE_U)) != (perm & (PTE_R | PTE_W | PTE_U))) {
                tlb_invalidate(pgdir, va);
            }

            return 0;
        }

        /* 映射到不同页面，先移除旧的 */
        printf("page_insert: removing old mapping for va=0x%08x\n", va);
        page_remove(pgdir, va);
    }

    /* Step 3: Set the page table entry to point to physical page `pp`. */
    *pte = page2pa(pp) | perm | PTE_V;

    /* Step 4: Increase the reference count of page `pp`. */
    pp->pp_ref++;

    printf("page_insert: va=0x%08x -> pa=0x%08x, perm=0x%x, ref=%d\n",
        va, page2pa(pp), perm, pp->pp_ref);

    /* Step 5: 使旧TLB条目失效 */
    tlb_invalidate(pgdir, va);

    return 0;
}


/**
 * 找到虚拟地址 va 所在的页
 * Overview:
 *      Look up the Page that virtual address `va` map to.
 * Post-Condition:
 *      Return a pointer to corresponding Page, and store it's page table entry to *ppte.
 *      If `va` doesn't mapped to any Page, return NULL.
 */
struct Page *
page_lookup(Pde *pgdir, u_long va, Pte **ppte)
{
    struct Page *ppage;
    Pte *pte;

    /* Step 1: Get the page table entry. */
    if (pgdir_walk(pgdir, va, 0, &pte) < 0 || pte == NULL) {
        if (ppte) {
            *ppte = NULL;
        }
        return NULL;
    }

      /* Check if the page table entry doesn't exist or is not valid. */
    if (pte == 0)
    {
        if (ppte) {
            *ppte = NULL;
        }
        return 0;
    }
    if ((*pte & PTE_V) == 0)
    {
        if (ppte) {
            *ppte = NULL;
        }
        return 0; //the page is not in memory.
    }

    /* Step 2: Get the corresponding Page struct. */
    ppage = pa2page(PTE_ADDR(*pte)); /* Use function `pa2page`, defined in include/pmap.h . */
   
    if (ppte) {
        *ppte = pte;
    }
    return ppage;
}

/**
 * Overview:
 *      Decrease the `pp_ref` value of Page `*pp`, if `pp_ref` reaches to 0, free this page.
 */
void page_decref(struct Page *pp)
{
    if (--pp->pp_ref == 0)
    {
        page_free(pp);
    }
}

/**
 * Overview:
 *      Unmaps the physical page at virtual address `va`.
 */
void page_remove(Pde *pgdir, u_long va)
{
    Pte *pagetable_entry;
    struct Page *ppage;
    ppage = page_lookup(pgdir, va, &pagetable_entry);

    if (ppage == 0)
    {
        return;
    }
    printf("page_remove:va 0x%x  pa 0x%x\n",va,*pagetable_entry);

    /* Clear the page table entry */
    *pagetable_entry = 0;
    /* Decrease reference count */
    page_decref(ppage);
    /* Invalidate TLB entry */
    tlb_invalidate(pgdir, va);
    return;
}
#include <m32c0.h>  // 确保包含CP0寄存器定义头文件

/**
 * 使指定虚拟地址的TLB条目失效
 * @param pgdir 页目录指针（用于验证）
 * @param va 虚拟地址
 *
 * MIPS TLB失效原理：
 * 1. 设置EntryHi寄存器（VPN2 + ASID）
 * 2. 执行tlbp指令，探测TLB中是否有匹配的条目
 * 3. 如果找到，使用tlbwi指令写入无效条目使其失效
 *
 * EntryHi寄存器格式：
 *   [31:13] VPN2（虚拟页号/2）
 *   [12:8]  保留
 *   [7:0]   ASID（地址空间标识符）
 */
void tlb_invalidate(Pde* pgdir, u_long va)
{
    u_int entryhi;
    u_int asid = 0;

    /* Step 1: 参数验证 */
    if (!pgdir) {
        printf("tlb_invalidate: NULL pgdir\n");
        return;
    }

    if (va >= ULIM && !(va >= KERNBASE && va < (KERNBASE + 0x20000000))) {
        printf("tlb_invalidate: invalid va 0x%08x\n", va);
        return;
    }

    /* Step 2: 获取当前ASID */
    if (curenv && curenv->env_asid) {
        asid = curenv->env_asid & 0xFF;  // ASID是8位
    }
    else {
        // 内核模式或没有当前环境，使用ASID 0
        asid = 0;
    }

    /* Step 3: 构造EntryHi值
     * VPN2 = va[31:13] （MIPS TLB使用VPN/2，因为每个TLB条目对应两个页面）
     * ASID = asid[7:0]
     */
    entryhi = (va & 0xFFFFE000);  // 保留VPN2位，清除低13位

    /* 如果va是奇数页（VPN2的最低有效位为1），需要调整 */
    if (va & 0x1000) {
        // 对于奇数页，VPN2需要减1（因为TLB条目总是成对映射）
        entryhi -= 0x2000;
    }

    // 清除原有的ASID位（低8位）
    entryhi &= ~0xFF;
    // 设置新的ASID
    entryhi |= asid;

    printf("tlb_invalidate: va=0x%08x, entryhi=0x%08x, asid=%d\n",
        va, entryhi, asid);

    /* Step 4: 调用汇编函数使TLB条目失效 */
    /* tlb_out函数应该：
       1. 将entryhi写入CP0_EntryHi
       2. 执行tlbp指令寻找匹配的TLB条目
       3. 如果找到（Index寄存器>=0），执行tlbwi写入无效条目
     */
    tlb_out(entryhi);

    /* Step 5: 可选 - 验证TLB条目确实已失效 */
#ifdef DEBUG_TLB
    {
        u_int index;
        // 再次探测，确认TLB条目已失效
        asm volatile(
            "mtc0 %1, $10\n\t"   // 写入EntryHi
            "tlbp\n\t"           // 探测TLB
            "mfc0 %0, $0\n\t"    // 读取Index
            : "=r"(index)
            : "r"(entryhi)
            );

        if ((index & 0x80000000) == 0) {
            printf("  [警告] TLB条目可能未完全失效，index=%d\n", index & 0x3F);
        }
    }
#endif
}

extern int get_asid();

/**
 * 处理缺页异常（原pageout函数）
 * @param va 触发异常的虚拟地址
 * @param context 上下文（页目录物理地址）
 * @return 物理地址，如果失败返回0
 *
 * 功能：处理TLB缺失异常，分配物理页并建立映射
 */
u_long handle_page_fault(u_long va, u_long context)
{
    Pde* pgdir;
    struct Page* pp = NULL;
    Pte* pte;
    int r;
    u_int perm = 0;

    printf("\n=== 缺页异常处理开始 ===\n");
    printf("  va=0x%08x, context=0x%08x\n", va, context);
    printf("  epc=0x%08x, badvaddr=0x%08x\n", get_epc(), get_badvaddr());

    /* Step 1: 参数验证 */
    if (context < KERNBASE) {
        printf("  [错误] 无效的context: 0x%08x\n", context);
        panic("handle_page_fault: invalid context");
    }

    pgdir = (Pde*)KADDR(context);  // 将物理地址转换为内核虚拟地址

    /* Step 2: 检查虚拟地址范围 */
    if (va >= UTOP && va < ULIM) {
        // 这是内核预留区域（envs, pages等），用户不能直接访问
        if (va < ULIM && !(va >= KERNBASE)) {
            printf("  [错误] 用户访问内核预留区域: 0x%08x\n", va);
            return 0;
        }
    }

    /* Step 3: 确定访问类型和权限 */
    // 检查异常原因
    u_int cause = get_cause();
    u_int excode = (cause >> 2) & 0x1F;

    int is_write = 0;
    int is_user = (va < ULIM);  // 用户地址空间

    switch (excode) {
    case 2:  /* TLBL - TLB加载异常 */
        printf("  [TLB加载异常]\n");
        perm = PTE_V | PTE_R;
        break;
    case 3:  /* TLBS - TLB存储异常 */
        printf("  [TLB存储异常]\n");
        perm = PTE_V | PTE_R | PTE_W;
        is_write = 1;
        break;
    case 1:  /* Mod - TLB修改异常 */
        printf("  [TLB修改异常]\n");
        // 需要检查页面是否可写
        perm = PTE_V | PTE_R | PTE_W;
        is_write = 1;
        break;
    default:
        printf("  [未知异常] excode=%d\n", excode);
        return 0;
    }

    /* Step 4: 检查是否已存在映射（可能只是TLB缺失） */
    struct Page* existing = page_lookup(pgdir, va, &pte);

    if (existing) {
        printf("  [找到现有页面] pp=0x%08x, pa=0x%08x\n",
            existing, page2pa(existing));

        // 检查权限
        if (pte && *pte) {
            if (is_write && !(*pte & PTE_W)) {
                printf("  [错误] 写保护异常\n");
                return 0;
            }

            if (is_user && !(*pte & PTE_U)) {
                printf("  [错误] 用户模式访问内核页\n");
                return 0;
            }

            // 如果是修改异常，需要设置脏位
            if (is_write && excode == 1) {
                *pte |= PTE_D;
                printf("  [设置脏位]\n");
            }

            // 页面已存在，只需返回物理地址（TLB会重新加载）
            return page2pa(existing);
        }
    }

    /* Step 5: 分配新页面 */
    printf("  [分配新页面]\n");
    if ((r = page_alloc(&pp)) < 0) {
        printf("  [错误] 页面分配失败: %d\n", r);
        panic("handle_page_fault: page allocation failed");
    }

    /* Step 6: 设置完整的权限 */
    if (is_user) {
        perm |= PTE_U;  // 用户可访问
    }

    // 如果是写操作，设置脏位
    if (is_write) {
        perm |= PTE_D;
    }

    /* Step 7: 建立映射 */
    printf("  [建立映射] va=0x%08x -> pp=0x%08x, perm=0x%x\n",
        va, pp, perm);

    if ((r = page_insert(pgdir, pp, va, perm)) < 0) {
        printf("  [错误] 页面插入失败: %d\n", r);
        page_free(pp);
        panic("handle_page_fault: page insertion failed");
    }

    /* Step 8: 记录日志 */
    u_long pa = page2pa(pp);
    printf("  [映射成功] va=0x%08x -> pa=0x%08x\n", va, pa);
    printf("  [页面信息] pp_ref=%d\n", pp->pp_ref);

    printf("=== 缺页异常处理结束 ===\n\n");

    return pa;
}

/* 为了向后兼容，保留pageout作为包装函数 */
u_long pageout(u_long va, u_long context)
{
    printf("警告: pageout()已废弃，请使用handle_page_fault()\n");
    return handle_page_fault(va, context);
}
