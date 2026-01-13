#include <mips/m32tlb.h>
#include <mips/cpu.h>
#include <env.h>
#include <mmu.h>
#include <types.h>
#include <pmap.h>
#include <error.h>
#include <tlbop.h>
#include <hash.h>

/* These variables are set by set_physic_mm() */
u_long maxpa;   /* Maximum physical address */
u_long npage;   /* Amount of memory(in pages) */
u_long basemem; /* Amount of base memory(in bytes) */
u_long extmem;  /* Amount of extended memory(in bytes) */
u_long tlbCount=0;
Pde *boot_pgdir;
#define maxTLB 16
struct Page *pages;
static u_long freemem;

/* 变量 page_free_list 来以链表的形式表示所有的空闲物理内存 */
static struct FreePageList page_free_list; /* Free list of physical pages */
struct HashTable ht;

/* 获取下一个可用的TLB条目索引，循环使用TLB */
u_long getNextTlb()//要写的下一个tlb
{
    printf("nexttlb:%d\n",tlbCount);
    // 返回当前TLB索引，并将索引递增，使用取模实现循环
    u_long result = tlbCount;
    tlbCount = (tlbCount + 1) % maxTLB;
    return result;
}




/********************* Private Functions *********************/
// transfer page to page number

//Page page0 0x80; 第80个物理页、256M 0x0, 4K * 80

//pages 0x00
/* 将页控制块指针转换为页号（物理页帧号） */
u_long
page2ppn(struct Page *pp)
{
    // 页号 = 当前页控制块地址 - pages数组基地址
    return pp - pages;
}

// transfer page to physical address
/* 将页控制块指针转换为物理地址 */
u_long
page2pa(struct Page *pp)
{
    // 物理地址 = 页号 * 页大小（4KB）
    return page2ppn(pp) << PGSHIFT;
}

// transfer physical address to page
/* 将物理地址转换为对应的页控制块指针 */
struct Page *
pa2page(u_long pa)
{
    if (PPN(pa) >= npage)
        panic("pa2page called with invalid pa: %x\n", pa);
    // 根据物理地址计算页号，返回对应的页控制块
    return &pages[PPN(pa)];
}

// transfer page to kernel virtual address
/* 将页控制块指针转换为内核虚拟地址 */
u_long
page2kva(struct Page *pp)
{
    // 先转换为物理地址，再通过KADDR转换为内核虚拟地址
    return (u_long)KADDR(page2pa(pp));
}

// transfer virtual address to physical address
/* 通过查页表将虚拟地址转换为物理地址，有则返回物理地址，无则返回全1 */
u_long
va2pa(Pde *pgdir, u_long va)//查页表，有则返回，无则返回全1
{
    Pte *p;

    pgdir = &pgdir[PDX(va)];
    if (!(*pgdir & PTE_V))//一级页表没找到
    {
        return ~0;  // 一级页表项无效，返回全1
    }
    p = (Pte *)KADDR(PTE_ADDR(*pgdir));         //二级页表没找到
    if (!(p[PTX(va)] & PTE_V))
    {
        return ~0;  // 二级页表项无效，返回全1
    }
    return PTE_ADDR(p[PTX(va)]) | (va & 0xFFF);  // 返回物理地址（页帧地址 + 页内偏移）
}

//缺页异常时用来输出的
u_long
va2pa_print(Pde *pgdir, u_long va)//查页表，有则返回，无则返回全1
{
    Pte *p;
    printf("\n@@@ tlb-va2pa: 0x%x   epc：0x%x\n",va,get_epc());

    pgdir = &pgdir[PDX(va)];
    if (!(*pgdir & PTE_V))   //一级页表没找到
    {
        return ~0;
    }
    p = (Pte *)KADDR(PTE_ADDR(*pgdir));//二级页表没找到
    if (!(p[PTX(va)] & PTE_V))
    {
        return ~0;
    }
    printf("tlb_va_found!\n", va);
    return PTE_ADDR(p[PTX(va)]) | (va & 0xFFF);
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
 * alloc 函数能够按照参数 align 进行对齐，然后分配 n 字节大小的物理内存，并根据
 * 参数 clear 的设定决定是否将新分配的内存全部清零，并最终返回新分配的内存的首地址。
 * Overview:
 *      Allocate `n` bytes physical memory with alignment `align`, if `clear` is set, clear the allocated memory.
 *      This allocator is used only while setting up virtual memory system.
 * Post-Condition:
 *      If we're out of memory, should panic, else return this address of memory we have allocated.
 */
static void *alloc(u_int n, u_int align, int clear)
{
    extern char end[];//scse0_3.lds中定义，end地址在0x80400000
    u_long alloced_mem;

    /**
     * Initialize `freemem` if this is the first time. The first virtual address that the
     * linker did *not* assign to any kernel code or global variables.
     */
    if (freemem == 0)
    {
        freemem = (u_long)end;
    }

    /* Step 1: Round up `freemem` up to be aligned properly. */
    // 将freemem向上对齐到align的倍数
    freemem = ROUND(freemem, align);

    /* Step 2: Save current value of `freemem` as allocated chunk. */
    // 保存当前freemem作为分配的内存起始地址
    alloced_mem = freemem;

    /* Step 3: Increase `freemem` to record allocation. */
    // 增加freemem以记录分配
    freemem = freemem + n;

    /* Step 4: Clear allocated chunk if parameter `clear` is set. */
    if (clear)
    {
        // 如果clear参数设置，将分配的内存清零
        bzero((void *)alloced_mem, n);
    }

    // We're out of memory, PANIC !!
    if (PADDR((void *)freemem) >= maxpa)
    {
        panic("out of memorty\n");
        return (void *)-E_NO_MEM;
    }

    /* Step 5: return allocated chunk. */
    return (void *)alloced_mem;
}

/**
 *
 * 返回一个va对应的页表项物理地址的指针，有create则创建二级页表
 *
 *
 * Overview:
 *      Get the page table entry for virtual address `va` in the given page directory `pgdir`.
 *      If the page table is not exist and the parameter `create` is set to 1, then create it.
 */
static Pte *boot_pgdir_walk(Pde *pgdir, u_long va, int create)
{
    Pde *pgdir_entryp;  // 二级页表物理地址指针
    Pte *pgtable;       // 二级页表虚拟地址（上面那个指针的内容的虚拟地址）
    Pte *pgtable_entry; // 页表入口地址（指针）

    /**
     * Step 1: Get the corresponding page directory entry and page table.
     * Use KADDR and PTE_ADDR to get the page table from page directory entry value.
     */
    // 获取一级页表项的地址，PDX(va)获取va的一级页表索引
    pgdir_entryp = &pgdir[PDX(va)];                  // pgdir在vm_init()中分配，对应地址是虚拟地址
    // 获取二级页表的虚拟地址，PTE_ADDR获取物理地址，KADDR转换为虚拟地址
    pgtable = (Pte *)KADDR(PTE_ADDR(*pgdir_entryp)); // 存的是物理地址，转为虚拟地址
    /**
     * Step 2: If the corresponding page table is not exist and parameter `create`
     * is set, create one. And set the correct permission bits for this new page table.
     */
    if ((*pgdir_entryp & PTE_V) == 0x0)     //求V位的值，如果是无效
    {
        if (create)
        {
            // 分配一个页大小的内存作为二级页表，并清零
            pgtable = alloc(BY2PG, BY2PG, 1); //不是页分配
            // 设置一级页表项，存储二级页表的物理地址和权限位
            *pgdir_entryp = PADDR(pgtable) | PTE_V | PTE_R; // 由于页大小为4KB，所以物理地址的低12为本全为0，正好可以用于设置符号位.
        }
        else
        {
            return 0;//无效且不create
        }
    }
    /* Step 3: Get the page table entry for `va`, and return it. */
    // 获取二级页表项的地址，PTX(va)获取va的二级页表索引
    pgtable_entry = &pgtable[PTX(va)];
    return pgtable_entry;//返回页表项的物理地址的指针
}

/**
 * 实现将制定的物理内存与虚拟内存建立起映射的功能，perm 实际上是 PTE_R 修改位
 * Overview:
 *      Map [va, va+size) of virtual address space to physical [pa, pa+size) in the page table rooted at pgdir.
 *      Use permission bits `perm|PTE_V` for the entries.
 *      Use permission bits `perm` for the entries.
 * Pre-Condition:
 *      Size is a multiple of BY2PG.
 */
void boot_map_segment(Pde *pgdir, u_long va, u_long size, u_long pa, int perm)
{
    int i, va_temp, pa_temp;
    Pte *pgtable_entry;

    /* Step 1: Check if `size` is a multiple of BY2PG. */
    // 检查size是否是页大小的整数倍
    if (size % BY2PG != 0)
    {
        panic("pmap.c :134:size not aligned");
    }

    /* Step 2: Map virtual address space to physical address. */
    /* Hint: Use `boot_pgdir_walk` to get the page table entry of virtual address `va`. */
    va_temp = va;
    pa_temp = pa;
    for (i = 0; i < size / BY2PG; i++)
    {
        // 获取虚拟地址对应的页表项
        pgtable_entry = boot_pgdir_walk(pgdir, va_temp, 1);
        // 设置页表项，建立虚拟地址到物理地址的映射
        *pgtable_entry = pa_temp | perm | PTE_V;
        // 移动到下一页
        va_temp += BY2PG;
        pa_temp += BY2PG;
    }
}

/**
 * 给操作系统内核必须的数据结构 – 页表（pgdir）、内存控制块数组（pages）和
 * 进程控制块数组（envs）分配所需的物理内存.
 * Overview:
 *     Set up two-level page table.
 *
 *     You can get more details about `UPAGES` and `UENVS` in inc/mmu.h.
 */
void vm_init()
{
    extern char end[];
    extern int mCONTEXT;
    extern struct Env *envs;

    Pde *pgdir;
    u_int n;

    /* Step 1: Allocate a page for page directory(first level page table). */
    // 分配一页内存作为一级页表（页目录），并清零
    pgdir = alloc(BY2PG, BY2PG, 1);// 内核的一级页表！！！！
    printf("to memory %x for struct page directory. \n", pgdir);
    mCONTEXT = (int)pgdir;
    boot_pgdir = pgdir;
    /**
     * Step 2: Allocate proper size of physical memory for global array `pages`,
     * for physical memory management. Then, map virtual address `UPAGES` to
     * physical address `pages` allocated before. For consideration of alignment,
     * you should round up the memory size before map.
     */
    // 为pages数组分配内存，每个物理页对应一个Page结构体
    pages = (struct Page *)alloc(npage * sizeof(struct Page), BY2PG, 1); // 给每个 物理 页的管理信息创建内存
    printf("to memory %x for struct Pages.\n", pages);
    // 计算pages数组的大小，向上对齐到页大小
    n = ROUND(npage * sizeof(struct Page), BY2PG);
    // 将UPAGES虚拟地址映射到pages物理地址
    boot_map_segment(pgdir, UPAGES, n, PADDR(pages), PTE_R);

    /**
     * Step 3, Allocate proper size of physical memory for global array `envs`,
     * for process management. Then map the physical address to `UENVS`.
     */
    // 为envs数组分配内存，每个进程对应一个Env结构体
    envs = (struct Env *)alloc(NENV * sizeof(struct Env), BY2PG, 1);
    printf("to memory %x for struct Pages.\n", envs);
    // 计算envs数组的大小，向上对齐到页大小
    n = ROUND(NENV * sizeof(struct Env), BY2PG);
    // 将UENVS虚拟地址映射到envs物理地址
    boot_map_segment(pgdir, UENVS, n, PADDR(envs), PTE_R);
    printf("mips_vm_init:boot_pgdir is %x\n", boot_pgdir);
    printf("pmap.c:\t mips vm init success\n");
}

/**
 * page_init 函数，使用 inc/queue.h 中定义的宏函数将未分配
 * 的物理页加入到空闲链表 page_free_list 中去
 * Overview:
 *     Initialize page structure and memory free list.
 *     The `pages` array has one `struct Page` entry per physical page. Pages
 *     are reference counted, and free pages are kept on a linked list.
 *
 *     Use `LIST_INSERT_HEAD` to insert something to list.
 */
void page_init(void)
{
    int i = 0;
    /* Step 1: Initialize page_free_list. */
    /* Hint: Use macro `LIST_INIT` defined in include/queue.h. */
    // 初始化空闲页链表为空
    LIST_INIT(&page_free_list);

    /* Step 2: Align `freemem` up to multiple of BY2PG. */
    // 将freemem向上对齐到页边界
    freemem = ROUND(freemem, BY2PG);

    /* Step 3: Mark all memory blow `freemem` as used(set `pp_ref`
     * filed to 1) */
    // 将freemem以下的内存标记为已使用（被内核占用）
    for (i = 0; i < PPN(PADDR((void *)freemem)); i++)
    {
        pages[i].pp_ref = 1;  // 引用计数设为1，表示已被使用
    }

    /* Step 4: Mark the other memory as free. */
    // 将freemem以上的内存标记为空闲，加入空闲链表
    for (; i < npage; i++)
    {
        pages[i].pp_ref = 0;  // 引用计数设为0，表示空闲
        LIST_INSERT_HEAD(&page_free_list, &pages[i], pp_link);
    }
}

/**
 * page_alloc 函数用来从空闲链表中分配一页物理内存
 * Overview:
 *     Allocates a physical page from free memory, and clear this page.
 * Post-Condition:
 *     If failed to allocate a new page(out of memory(there's no free page)), return -E_NO_MEM.
 *     Else, set the address of allocated page to *pp, and returned 0.
 * Note:
 *     Does NOT increment the reference count of the page - the caller must do
 *     these if necessary (either explicitly or via page_insert).
 *
 *     Use LIST_FIRST and LIST_REMOVE defined in include/queue.h .
 */
int page_alloc(struct Page **pp)
{
    struct Page *ppage_temp;
    /* Step 1: Get a page from free memory. If fails, return the error code.*/
    // 检查空闲链表是否为空
    if (LIST_EMPTY(&page_free_list))
    {
        return -E_NO_MEM;  // 没有空闲页，返回内存不足错误
    }

    /**
     * Step 2: Initialize this page.
     *  use `bzero`.
     */
    *pp = LIST_FIRST(&page_free_list);                 //get and turn for the *pp
    bzero((void *)page2kva(*pp), BY2PG);               // bzero *pp 清零分配的页
    LIST_REMOVE(*pp, pp_link);                         //remove allocted page from free_list.
    return 0;
}


int page_alloc_share(struct Page **pp)
{
    struct Page *ppage_temp;
    /* Step 1: Get a page from free memory. If fails, return the error code.*/
    // 检查空闲链表是否为空
    if (LIST_EMPTY(&page_free_list))
    {
        return -E_NO_MEM;  // 没有空闲页，返回内存不足错误
    }

    /**
     * Step 2: Initialize this page.
     *  use `bzero`.
     */
    *pp = LIST_FIRST(&page_free_list);                 //get and turn for the *pp
    bzero((void *)page2kva(*pp), BY2PG);               // bzero *pp 清零分配的页
    LIST_REMOVE(*pp, pp_link);                         //remove allocted page from free_list.
    return page2kva(*pp);
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
        // 分配一个共享内存页
        if ((r = page_alloc(&p)) < 0)
        {
            panic("create_share_vm: page_alloc failed\n");
            return NULL;
        }
        // 设置页的引用计数
        p->pp_ref = 1;

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
    perm = PTE_V | PTE_R;
    /*Step 2: Use appropriate perm to set initial stack for new Env. */
    /*Hint: The user-stack should be writable? */
    // 将共享页插入到进程的地址空间中
    u_int user_va = e->heap_pc;  // 保存映射的用户空间虚拟地址
    r = page_insert(e->env_pgdir, p, user_va, perm);
    if (r < 0)
    {
        printf("error,load_icode:page_insert failed\n");
        return NULL;
    }
    // 更新进程的堆指针，移动到下一个页
    e->heap_pc = e->heap_pc + BY2PG;
    // 返回用户空间的虚拟地址（而不是内核虚拟地址）
    return (void *)user_va;
}

/**
 * page_free 函数用于将一页之前分配的内存重新加入到空闲链表中
 * Overview:
 *     Release a page, mark it as free if it's `pp_ref` reaches 0.
 *
 *     When to free a page, just insert it to the page_free_list.
 */
void page_free(struct Page *pp)
{
    /* Step 1: If there's still virtual address refers to this page, do nothing. */
    // 如果引用计数大于0，说明还有虚拟地址引用此页，不释放
    if (pp->pp_ref > 0)
    {
        return;
    }

    /* Step 2: If the `pp_ref` reaches to 0, mark this page as free and return. */
    // 如果引用计数等于0，将此页加入空闲链表
    else if (pp->pp_ref == 0)
    {
        // 将页插入到空闲链表头部
        LIST_INSERT_HEAD(&page_free_list, pp, pp_link);
        return;
    }
    else
        /* If the value of `pp_ref` less than 0, some error must occurred before, so PANIC !!! */
        panic("cgh:pp->pp_ref is less than zero\n");
    return;
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
    Pde *pgdir_entryp;
    Pte *pgtable;
    struct Page *ppage; // a temp point,help for ppte

    /* Step 1: Get the corresponding page directory entry and page table. */
    // 获取一级页表项的地址
    pgdir_entryp = &pgdir[PDX(va)];//指向二级页表物理地址的指针
    // 获取二级页表的虚拟地址
    pgtable = (Pte *)KADDR(PTE_ADDR(*pgdir_entryp));//二级页表虚拟地址

    /* by the va, get the pa; again, by the pa, get the va of the two-level page table array */
    // use PTE_ADDR is because the low-12bit of pa is not all 0, need change.

    /**
     * Step 2: If the corresponding page table is not exist(valid) and parameter `create`
     * is set, create one. And set the correct permission bits for this new page table.
     * When creating new page table, maybe out of memory.
     */
    // 检查一级页表项是否有效
    if ((*pgdir_entryp & PTE_V) == 0)  //没有二级页表
    {
        if (create == 0)
        {
            // 不创建，设置ppte为0并返回
            *ppte = 0;
            return 0;
        }
        else
        {     //alloc a page for page table.
            // 分配一页作为二级页表
            if (page_alloc(&ppage) < 0)
            { //cannot alloc a page for page table
                // 内存分配失败，返回错误
                *ppte = 0;
                return -E_NO_MEM;
            }
            // 获取新分配页的虚拟地址作为二级页表
            pgtable = (Pte *)page2kva(ppage);
            // 设置一级页表项，存储二级页表的物理地址和权限位
            *pgdir_entryp = page2pa(ppage) | PTE_V | PTE_R;//存的是物理地址
            // 增加页的引用计数
            ppage->pp_ref++;
        }
    }

    /* Step 3: Set the page table entry to `*ppte` as return value. */
    if (ppte)
    {
        // 返回二级页表项的地址
        *ppte = &pgtable[PTX(va)];
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
int page_insert(Pde *pgdir, struct Page *pp, u_long va, u_int perm)
{
    u_int PERM;
    Pte *pgtable_entry;
    PERM = perm | PTE_V;

    pgdir_walk(pgdir, va, 1,&pgtable_entry);

	if (!pgtable_entry) {

		return -E_NO_MEM;
	}

	if (*pgtable_entry & PTE_V) {          //当前页表项有映射
		// 检查是否是同一个物理页
		if (pa2page(PTE_ADDR(*pgtable_entry)) == pp) {
			// 插入的是同一个页面,后面固定加，这里先减
			pp->pp_ref--;
		}
		else {
			page_remove(pgdir, va);
		}
	}
    *pgtable_entry = (page2pa(pp) | PERM);
    pp->pp_ref++;
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
    // 获取虚拟地址对应的页表项
    pgdir_walk(pgdir, va, 0, &pte);

      /* Check if the page table entry doesn't exist or is not valid. */
    if (pte == 0)
    {
        return 0;
    }
    if ((*pte & PTE_V) == 0)
    {
        return 0; //the page is not in memory.
    }

    /* Step 2: Get the corresponding Page struct. */
    // 如果ppte不为空，存储页表项的地址
    if (ppte)
    {
        *ppte = pte;
    }
               /* Use function `pa2page`, defined in include/pmap.h . */
    // 根据页表项中的物理地址获取对应的Page结构体
    ppage = pa2page(PTE_ADDR(*pte));
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
    // 查找va对应的页和页表项
    ppage = page_lookup(pgdir, va, &pagetable_entry);//查出va对应页表项

    if (ppage == 0)
    {
        return;
    }
    printf("page_remove:va 0x%x  pa 0x%x\n",va,*pagetable_entry);

    // 减少页的引用计数，如果为0则释放页
    page_decref(ppage);               //减引用
    // 清空页表项
    *pagetable_entry = 0;             //tlb删除
    // 使TLB中对应的条目无效
    tlb_invalidate(pgdir, va);
    return;
}

/**
 * 从tlb中删去e的va目标项
 * Overview:
 *      Update TLB.
 */
void tlb_invalidate(Pde *pgdir, u_long va)//
{

    if (curenv)
    {
        // 如果有当前进程，使用进程的ASID来使TLB条目无效
        tlb_out(PTE_ADDR(va) | GET_ENV_ASID(curenv->env_id));   // 假如不是释放当前进程，会有问题！
    }
    else
    {
        // 没有当前进程，直接使用虚拟地址
        tlb_out(PTE_ADDR(va));
        printf(" PTE_ADDR(va) : %x \n", PTE_ADDR(va));
    }

}


uint32_t pageout(uint32_t va, uint32_t context)
{
    u_long r;
    struct Page *p = NULL;

    if (context < 0x80000000) //todo
    {
        panic("tlb refill and alloc error!");
    }

    if ((va > 0x7f400000) && (va < 0x7f800000)) //todo 虚拟内存里这块是ENVS，只有内核可以访问，为什么要单独判断这个
    {
        panic(">>>>>>>>>>>>>>>>>>>>>>it's env's zone");
    }

    if ((r = page_alloc(&p)) < 0)
    {
        panic("page alloc error!");
    }

    page_insert((Pde *)context, p, VA2PFN(va), PTE_R);
    printf("pageout: @ 0x%x @  ->pa 0x%x\n", va,page2pa(p));
    printf("CP0HI: 0x%x status:0x%x \n",get_asid(),get_status());

    return va2pa((Pde *)context, va);
}
