/*
 env.c 文件是操作系统内核中负责进程管理的核心模块。它实现了进程的创建（env_alloc, load_icode, env_create_*）、
 初始化（env_init, env_setup_vm）、查找（envid2env）、ID生成（mkenvid）、销毁（env_free）以及调度运行（env_run）等功能。
 它与内存管理（pmap.h）、文件系统（ff.h）、ELF解析（elf.h）和底层硬件（通过汇编函数 lcontext, env_pop_tf, set_asid）紧密交互。
 pthread_create 和 copy_curenv 表明系统还尝试支持某种形式的线程或轻量级进程。
 */
#include <stdint.h>
#include <mmu.h>
#include <error.h>
#include <env.h>
#include <kerelf.h>
#include <sched.h>
#include <pmap.h>
#include <printf.h>
#include <../fs/ff.h>
#include <../fs/elf.h>
#include <../drivers/timer.h>

/*
定义和声明全局变量。
envs: 指向所有环境（进程）控制块（Env 结构体数组）的指针。这是存储所有进程信息的核心数据结构。
curenv: 指向当前正在运行的环境（进程）的指针。
mCONTEXT, curtf: 外部变量，可能用于保存当前的页表基址和上下文信息，供汇编代码使用。
env_free_list: 空闲环境控制块的链表头指针。未使用的 Env 结构体会链接在这里。
env_runnable_head, env_runnable_tail: 可运行环境（进程）的循环链表的头和尾指针。处于 ENV_RUNNABLE 状态的进程会被放入这个队列等待调度。
*/
struct Env *envs = NULL;   // All environments
struct Env *curenv = NULL; // the current env
extern int mCONTEXT;
extern int curtf;
struct Env *env_free_list = NULL; // Free list

struct Env *env_runnable_head = NULL; // Runnable ring head
struct Env *env_runnable_tail = NULL; // Runnable ring tail
extern Pde *boot_pgdir;				  // kernel page directory
extern char *KERNEL_SP;				  // top of kernel stack
extern int remaining_time;			  // remaining time for current env

/*
声明外部变量和函数。
boot_pgdir: 内核启动时使用的页目录。
KERNEL_SP: 内核栈指针。
remaining_time: 可能与进程时间片有关。
env_pop_tf: 汇编函数，用于恢复陷阱帧（Trapframe）并跳转到用户模式执行。
lcontext: 汇编函数，用于切换地址空间（通常是加载新的页表基址到MMU）。
set_asid, get_asid: 设置/获取当前活动的地址空间标识符（ASID）。这在支持TLB的处理器中很重要，用于区分不同进程的TLB条目。
set_epc: 设置CP0 EPC寄存器（异常程序计数器）。
create_share_vm, insert_share_vm: 创建和插入共享内存页面的函数。
get_status: 获取CPU状态寄存器。
copy_curenv: 复制当前环境以创建新线程的函数。
*/
extern Pde *boot_pgdir;
extern char *KERNEL_SP;
extern int remaining_time;

extern void env_pop_tf(struct Trapframe *tf);
extern void lcontext(uint32_t contxt, int n);
extern void set_asid(uint32_t id);
extern u32 get_asid(void);
extern void set_epc(uint32_t epc);
extern struct Page *create_share_vm(int key, size_t size);
extern void *insert_share_vm(struct Env *e, struct Page *p);
extern u32 get_status();
extern void copy_curenv(struct Env *e, struct Env *env_src, void *func, int arg);
/*
申请一个envid, 低位为 e 在 envs 中的位置，高位为自增编号
 */
u_int mkenvid(struct Env *e)
{
	static u_int next_env_id = 0;

	/* Hint: lower bits of envid hold e's position in the envs array. */
	u_int low = e - envs;

	/* Hint: high bits of envid hold an increasing number. */
	++next_env_id;
	return (next_env_id << (1 + LOG2NENV)) | low;
}

// 根据环境ID (envid) 查找对应的 Env 结构体指针
/* Overview:
 *  Converts an envid to an env pointer.
 *  If envid is 0 , set *penv = curenv;otherwise set *penv = envs[ENVX(envid)];
 *
 * Pre-Condition:
 *  Env penv is exist,checkperm is 0 or 1.
 *
 * Post-Condition:
 *  return 0 on success,and sets *penv to the environment.
 *  return -E_BAD_ENV on error,and sets *penv to NULL.
 */
int envid2env(u_int envid, struct Env **penv, int checkperm)
{
	struct Env *e;
	/* Hint:
	 *      *  If envid is zero, return the current environment.*/
	/*If envid is zero, return the current environment.*/
	/*Step 1: Assign value to e using envid. */
	if (envid == 0)
	{
		*penv = curenv;
		return 0;
	}
	e = envs + GET_ENV_ASID(envid);
	if (e->env_status == ENV_FREE || e->env_id != envid) // ENV_FREE表示进程列表的这个位置是空的
	{
		// 空闲或 id 不匹配则失败
		*penv = NULL;
		return -E_BAD_ENV;
	}
	/* Hint:
	 *      *  Check that the calling environment has legitimate permissions
	 *           *  to manipulate the specified environment.
	 *                *  If checkperm is set, the specified environment
	 *                     *  must be either the current environment.
	 *                          *  or an immediate child of the current environment.If not, error! */
	/*Check that the calling environment has legitimate permissions
		  **to manipulate the specified environment.
			  **If checkperm is set,
		the specified environment
			**must be either the current environment.
				**or an immediate child of the current environment.If not,
		error !*/
	/*Step 2: Make a check according to checkperm. */
	if (checkperm)
	{
		// 需要权限时仅允许自己或子进程
		if (e != curenv && e->env_parent_id != curenv->env_id)
		{
			*penv = NULL;
			return -E_BAD_ENV;
		}
	}
	*penv = e;
	return 0;
}

// 初始化所有 env，链到 env_free_list 上
/*
初始化环境管理系统。
遍历整个 envs 数组（共 NENV 个）。
将每个 Env 结构体的 env_id 初始化为无效值 0xFFFFFFFF。
将 env_status 设置为 ENV_FREE，表示初始时所有环境都是空闲的。
将每个 Env 结构体通过其 env_link 指针链接到 env_free_list 链表上，形成一个空闲池。注意是从后往前插入，所以最后 envs[0] 会在链表头。
heap_pc 初始化为 UTOP，表示用户堆尚未分配任何空间。
*/
void env_init(void)
{
	// 存放进程控制块 (PCB) 的物理内存，在系统启动后就要分配好，并且这块内存不可以被换出
	// 因此, 在系统启动之后, 就要为进程控制块数组 envs 分配好内存
	int i;
	for (i = NENV - 1; i >= 0; i--) // NENV 应该是我们一共支持多少进程
	{
		envs[i].env_id = 0XFFFFFFFF;
		envs[i].env_status = ENV_FREE;
		// 插入到 env_free_list 链表头节点
		envs[i].env_link = env_free_list;
		env_free_list = &envs[i];
		envs[i].heap_pc = UTOP;
	}
}

// 初始化 e 的虚拟地址空间
/*
为指定环境 e 设置初始的虚拟内存布局。
分配一个物理页 (page_alloc) 作为该环境的页目录 (pgdir)。
增加该物理页的引用计数 (p->pp_ref++)。
通过 page2kva 获取该物理页的内核虚拟地址，赋给 pgdir 和 e->env_pgdir。
通过 page2pa 获取该物理页的物理地址，赋给 e->env_cr3（用于加载到MMU的页表基址寄存器）。
将页目录中低于 UTOP 的项全部置零，意味着用户空间初始时没有映射。
设置 VPT 和 UVPT 映射项：
VPT: 映射到环境自己的页目录物理地址，通常用于内核访问该进程的页表结构。
UVPT: 映射到环境自己的页目录物理地址，但带有 PTE_V (有效) 和 PTE_R (可读) 属性，允许用户态程序只读地访问自己的页表结构。
*/
static int env_setup_vm(struct Env *e)
{

	int i, r;
	struct Page *p = NULL;
	Pde *pgdir;

	/*Step 1: Allocate a page for the page directory using a function you completed in the lab2.
	 * and add its reference.
	 *pgdir is the page directory of Env e, assign value for it. */
	if ((r = page_alloc(&p)) < 0)
	{ /* Todo here*/
		panic("env_setup_vm - page alloc error\n");
		return r;
	}
	p->pp_ref++;
	pgdir = (Pde *)(page2kva(p));
	e->env_pgdir = pgdir;
	e->env_cr3 = page2pa(p);

	/*Step 2: Zero pgdir's field before UTOP. */
	// UTOP 以下清零，以上拷贝内核映射
	for (i = 0; i < PDX(UTOP); i++)
	{
		pgdir[i] = 0;
	}
	for (; i < PTE2PT; i++)
	{
		pgdir[i] = boot_pgdir[i];
	}
	/*VPT and UVPT map the env's own page table, with
	 *      *different permissions. */

	e->env_pgdir[PDX(VPT)] = e->env_cr3 | PTE_V;		  // 自映射
	e->env_pgdir[PDX(UVPT)] = e->env_cr3 | PTE_V | PTE_R; // 用户只读
	return 0;
}

/* Overview:
 *  Allocates and Initializes a new environment.
 *  On success, the new environment is stored in *new.
 *
 * Pre-Condition:
 *  If the new Env doesn't have parent, parent_id should be zero.
 *  env_init has been called before this function.
 *
 * Post-Condition:
 *  return 0 on success, and set appropriate values for Env new.
 *  return -E_NO_FREE_ENV on error, if no free env.
 *
 * Hints:
 *  You may use these functions and defines:
 *      LIST_FIRST,LIST_REMOVE,mkenvid (Not All)
 *  You should set some states of Env:
 *      id , status , the sp register, CPU status , parent_id
 *      (the value of PC should NOT be set in env_alloc)
 */
/*
分配并初始化一个新的环境（进程）控制块。
参数：
 - new: 输出参数，指向新分配的 Env 结构体指针。
 - parent_id: 新环境的父环境ID（如果是内核创建的第一个用户进程，则为0）。
功能：
 - 从 env_free_list 中取出第一个空闲的 Env 结构体 e。如果没有可用的，返回 -E_NO_FREE_ENV。
 - 调用 env_setup_vm(e) 为新环境 e 初始化其页目录和虚拟内存布局。
 - 初始化 e 的各个字段
*/
int env_alloc(struct Env **new, u_int parent_id)
{
	int r;
	struct Env *e;
	/*Step 1: Get a new Env from env_free_list*/
	e = env_free_list; // 从 env_free_list 中取出第一个空闲 PCB 块
	if (e == NULL)
	{
		return -E_NO_FREE_ENV;
	}

	/*Step 2: Call certain function(has been implemented) to init kernel memory layout for this new Env.
	 *The function mainly maps the kernel address to this new Env address. */
	if ((r = env_setup_vm(e)) < 0)
	{
		panic("env_alloc: env_setup_vm failed");
		return r;
	}
	/*Step 3: Initialize every field of new Env with appropriate values*/
	// 初始化 PCB 项
	e->env_id = mkenvid(e);
	e->env_parent_id = parent_id;
	e->env_status = ENV_RUNNABLE;

	/*Step 4: focus on initializing env_tf structure, located at this new Env.
	 * especially the sp register,CPU status. */
	// env_tf 是进程上下文（32 个通用寄存器、cp0 之类的）
	e->env_tf.cp0_status = 0x10007c01;
	e->env_tf.regs[29] = USTACKTOP;	 // 栈顶
	e->env_tf.regs[31] = 0x90000000; // 返回地址（指向结束的系统调用）
	e->env_runs = 0;

	/*Step 5: Remove the new Env from Env free list*/
	env_free_list = env_free_list->env_link;
	*new = e;
	return 0;
}
// 类似于 env_alloc，但增加了传递命令行参数的功能
int env_alloc_arg(struct Env **new, u_int parent_id, char *arg)
{
	int r;
	struct Env *e;

	/*Step 1: Get a new Env from env_free_list*/
	e = env_free_list;
	if (e == NULL)
	{
		*new = NULL;
		return -E_NO_FREE_ENV;
	}
	/*Step 2: Call certain function(has been implemented) to init kernel memory layout for this new Env.
	 *The function mainly maps the kernel address to this new Env address. */
	if ((r = env_setup_vm(e)) < 0)
	{
		panic("env_alloc_arg: env_setup_vm failed");
		*new = NULL;
		return r;
	}
	/*Step 3: Initialize every field of new Env with appropriate values*/
	e->env_id = mkenvid(e);
	e->env_parent_id = parent_id;
	e->env_status = ENV_RUNNABLE;

	/*Step 4: focus on initializing env_tf structure, located at this new Env.
	 * especially the sp register,CPU status. */
	e->env_tf.cp0_status = 0x10007c01;
	e->env_tf.regs[29] = USTACKTOP;	 // 栈顶
	e->env_tf.regs[31] = 0x90000000; // 返回地址（指向结束的系统调用处理函数）
	e->env_runs = 0;
	*new = e;

	/*Step 5: Remove the new Env from Env free list*/
	env_free_list = env_free_list->env_link;
	if (arg)
	{
		e->env_tf.regs[4] = 1;
		e->env_tf.regs[5] = arg;
	}

	*new = e;

	return 0;
}

/*
定义与文件系统和内存相关的常量及辅助函数。
FatFs: FatFs文件系统的根对象。
MAX_FILE_SIZE: 单个文件的最大大小（16MB）。
DDR_SIZE: DDR内存总大小（256MB）。
SD_READ_SIZE: 从SD卡读取数据的块大小（4KB）。
get_ddr_base(): 获取DDR内存基地址（0x80000000）
*/
FATFS FatFs; // Work area (file system object) for logical drive

// max size of file image is 16M
#define MAX_FILE_SIZE 0x1000000

// size of DDR RAM (256M for Minisys)
#define DDR_SIZE 0x10000000

// 4K size read burst
#define SD_READ_SIZE 4096

// 得到我们添加的 DDR3SDRAM 的基址，load elf 时, 貌似是放到 DDR 最末端
uint32_t get_ddr_base()
{
	return 0x80000000;
}

// 从文件系统中读取elf_name，加载到指定环境的内存中
/*
参数：
elf_name: ELF文件名。
e: 目标环境。
功能：
使用FatFs挂载SD卡。
打开名为 elf_name 的文件。
将文件内容读入到DDR内存末尾预留的大缓冲区 boot_file_buf 中。
关键部分：为了使ELF加载过程中产生的缺页中断能够正确地更新目标环境 e 的TLB（Translation Lookaside Buffer），需要临时切换当前的地址空间上下文 (lcontext) 和ASID (set_asid) 到环境 e。
调用 load_elf_sd 解析 boot_file_buf 中的ELF数据，并根据ELF头部信息将其各段（代码、数据）加载到环境 e 的虚拟地址空间中（这个过程会触发缺页中断，由内核处理并建立正确的页表映射）。
调用 get_entry 获取ELF文件的入口点地址。
恢复之前的地址空间上下文和ASID。
关闭文件。
返回ELF的入口点地址。
*/
uint32_t load_elf_mapper(char *elf_name, struct Env *e)
{
	FIL fil;	// File object
	FRESULT fr; // FatFs return code

	uint8_t *boot_file_buf = (uint8_t *)(get_ddr_base()) + DDR_SIZE - MAX_FILE_SIZE; // at the end of DDR space

	// Register work area to the default drive
	if (f_mount(&FatFs, "", 1))
	{
		printf("Fail to mount SD driver!\n\r", 0);
		return 1;
	}

	// Open a file
	printf("Loading %s into memory...\n\r", elf_name);
	fr = f_open(&fil, elf_name, FA_READ);
	if (fr)
	{
		printf("Failed to open %s!\n\r", elf_name);
		// return (int)fr;
		return 1;
	}

	// Read file into memory
	uint8_t *buf = boot_file_buf; // boot_file_buf 是个固定值
	uint32_t fsize = 0;			  // file size count
	uint32_t br;				  // Read count
	// 以下就是指导手册 61 页，boot loader 中 load elf 的前置代码
	do
	{
		if (fsize % 1024 == 0)
		{
			printf("Loading %d KB to memory address \r", fsize / 1024);
		}
		// Read a chunk of source file
		fr = f_read(&fil, buf, SD_READ_SIZE, &br);
		fsize += br;
		buf += br;

	} while (!(fr || br == 0));

	printf("Load %d bytes to memory address ", fsize);
	printf("%x \n\r", (uint32_t)boot_file_buf);
	printf("BeforeLOAD:  Mcontext : 0x%x  ASID: 0x%x\n", mCONTEXT, get_asid());
	// 保存当前环境
	int pre_pgdir = mCONTEXT;
	int pre_curtf = curtf;
	int pre_asid = curenv->env_id;

	// 加载 elf 进内存时会触发缺页中断，缺页中断会填当前调用进程的 asid 和页表基址进 tlb 页表项
	lcontext(e->env_pgdir, 0); // 因此，上下文切换到要新建的进程的 asid，之后缺页中断会填这个进程的 tlb
	set_asid(GET_ENV_ASID(e->env_id));

	// read elf
	if (load_elf_sd(boot_file_buf, fsize) != 0)
		if (br = load_elf_sd(boot_file_buf, fil.fsize))
			printf("elf read failed with code %d \n\r", br);

	uint32_t entry_point = get_entry(boot_file_buf, fil.fsize);

	// 这里和上面是一对的
	lcontext(pre_pgdir, pre_curtf);	  // context 换回来
	set_asid(GET_ENV_ASID(pre_asid)); // sid 换回来

	printf("\nfinish load elf!\n");

	// Close the file
	if (f_close(&fil))
	{
		printf("fail to close file!\n\r", 0);
	}

	return entry_point;
}

/* Overview:
 *  Sets up the the initial stack and program binary for a user process.
 *  This function loads the complete binary image by using elf loader,
 *  into the environment's user memory. The entry point of the binary image
 *  is given by the elf loader. And this function maps one page for the
 *  program's initial stack at virtual address USTACKTOP - BY2PG.
 *
 * Hints:
 *  All mappings are read/write including those of the text segment.
 *  You may use these :
 *      page_alloc, page_insert, page2kva , e->env_pgdir and load_elf.
 */
// 为用户进程加载代码和设置初始栈
static void
load_icode(struct Env *e, char *elf_name)
{
	/* Hint:
	 *  You must figure out which permissions you'll need
	 *  for the different mappings you create.
	 *  Remember that the binary image is an a.out format image,
	 *  which contains both text and data.
	 */
	struct Page *p = NULL; // 为新建的进程分配一个物理页，并映射到它的栈的地址上去
	uint32_t entry_point;
	u_long r;
	u_long perm;
	/*Step 1: alloc a page. */
	r = page_alloc(&p);
	p->pp_ref++;
	if (r < 0)
	{
		printf("ERROR in load_icode:page_alloc failed\n");
		return;
	}

	/*Step 2: Use appropriate perm to set initial stack for new Env. */
	/*Hint: The user-stack should be writable? */
	perm = PTE_V | PTE_R;
	r = page_insert(e->env_pgdir, p, USTACKTOP - BY2PG, perm); // USTACKTOP向下增长一页的大小
	if (r < 0)
	{
		printf("error,load_icode:page_insert failed\n");
		return;
	}

	printf("load_elf:%s\n", elf_name);
	entry_point = load_elf_mapper(elf_name, e); // 将完整的二进制镜像 (elf) 加载到进程的用户内存中去
	// 目前, 设计上会需要将完整的 elf 通过文件系统读到内存中一个固定地址（boot_file_buf 是个固定值），
	// 然后根据这部分内存的内容，读出 elf 的管理信息，再将实际的代码存到 elf 指定的进程虚拟地址空间中去。
	assert(entry_point != 1); // load 失败

	e->env_tf.cp0_epc = entry_point; // 将 elf 指定的的代码入口地址 entry_point,
									 // 存在当前进程 env 的 env_tf.cp0_epc 当中，
									 // 作为后续代码运行的起始 pc 地址

	/* 对于 PIC (Position Independent Code) 程序，入口处会用 t9 计算 GP 寄存器
	 * 例如：
	 *   lui  gp, %hi(_gp_disp)
	 *   addiu gp, gp, %lo(_gp_disp)
	 *   addu gp, gp, t9    <-- 需要 t9 = 入口地址
	 * 因此需要将 t9 ($25) 设置为入口地址
	 */
	e->env_tf.regs[25] = entry_point; // t9 = 入口地址，用于 PIC 代码的 GP 计算

	return;
}

/* Overview:
 *  Allocates a new env with env_alloc, loads the named elf binary into
 *  it with load_icode and then set its priority value. This function is
 *  ONLY called during kernel initialization, before running the first
 *  user_mode environment.
 *
 * Hints:
 *  this function wrap the env_alloc and load_icode function.
 */
// 创建一个具有指定优先级的新环境（进程）
void env_create_priority(char *binary, int priority)
{
	struct Env *e, *tmp;
	int r;
	extern void debug();
	/*Step 1: Use env_alloc to alloc a new env. */
	r = env_alloc(&e, 0); // 新建一个初始化好的 env
	if (r < 0)
	{
		panic("sorry, env_create_priority:env_alloc failed");
		return;
	}

	/*Step 2: assign priority to the new env. */
	e->env_pri = priority;

	/*Step 3: Use load_icode() to load the named elf binary. */
	printf("load_icode:%s\n", binary);
	load_icode(e, binary);

	/* Step 4 (additional): 将 env 加到 env_runnable 链表里*/
	if (env_runnable_head == NULL)
	{ // env_runnable 链表是否为空
		e->env_link = e;
		env_runnable_head = env_runnable_tail = e;
	}
	else
	{
		e->env_link = env_runnable_head;
		env_runnable_tail->env_link = e;
		env_runnable_tail = e;
	}
	// 调试：遍历输出 env_runnable 链表
	tmp = env_runnable_head;
	printf("list ID: 0x%x \n", env_runnable_head->env_id);
	while (tmp != env_runnable_tail)
	{
		printf(" 0x%x ", tmp->env_id);
		tmp = tmp->env_link;
	}
	printf("\ntail ID: 0x%x \n", env_runnable_tail->env_id);
}
// 参数化创建一个具有指定优先级的新环境（进程）
void env_create_priority_arg(char *binary, int priority, char *arg)
{
	struct Env *e, *tmp;
	int r;
	extern void debug();
	/*Step 1: Use env_alloc to alloc a new env. */
	r = (r = env_alloc_arg(&e, 0, arg));
	if (r < 0)
	{
		panic("sorry, env_create_priority:env_alloc failed");
		return;
	}
	/*Step 2: assign priority to the new env. */
	e->env_pri = priority;

	/*Step 3: Use load_icode() to load the named elf binary. */
	printf("load_icode:%s\n", binary);
	load_icode(e, binary);

	/* Step 4 (additional): 将 env 加到 env_runnable 链表里*/
	if (env_runnable_head == NULL)
	{ // env_runnable 链表是否为空
		e->env_link = e;
		env_runnable_head = env_runnable_tail = e;
	}
	else
	{
		e->env_link = env_runnable_head;
		env_runnable_tail->env_link = e;
		env_runnable_tail = e;
	}
	// 调试：遍历输出 env_runnable 链表
	tmp = env_runnable_head;
	printf("list ID: 0x%x \n", env_runnable_head->env_id);
	while (tmp != env_runnable_tail)
	{
		printf(" 0x%x ", tmp->env_id);
		tmp = tmp->env_link;
	}
	printf("\ntail ID: 0x%x \n", env_runnable_tail->env_id);
}

/* Overview:
 * Allocates a new env with default priority value.
 *
 * Hints:
 *  this function warp the env_create_priority function/
 */
// 创建一个具有默认优先级（1）的新环境
void env_create(char *binary, int *pt)
{
	env_create_priority(binary, 1);
}
// 创建一个新环境，并为其分配一个共享内存页
void env_create_share(char *binary, int num, int priority)
{
	struct Env *e;
	int r;
	extern void debug();
	/*Step 1: Use env_alloc to alloc a new env. */
	r = env_alloc(&e, 0);
	if (r < 0)
	{
		panic("sorry, env_create_priority:env_alloc failed");
		return;
	}
	/*Step 2: assign priority to the new env. */
	e->env_pri = priority;

	/*Step 3: Use load_icode() to load the named elf binary. */
	printf("load_icode:%s\n", binary);
	load_icode(e, binary);

	/* Step 4 (additional): 将 env 加到 env_runnable 链表里*/
	if (env_runnable_head == NULL)
	{ // env_runnable 链表是否为空
		e->env_link = e;
		env_runnable_head = env_runnable_tail = e;
	}
	else
	{
		e->env_link = env_runnable_head;
		env_runnable_tail->env_link = e;
		env_runnable_tail = e;
	}
	// 调试：输出 env_runnable 链表的 head tail
	printf("list ID: 0x%x \n", env_runnable_head->env_id);

	struct Page *p = NULL;
	u_long rr;
	u_long perm;
	p = create_share_vm(1, BY2PG); // todo 测试 key先都给1
	p->pp_ref++;
	if (p == NULL)
	{
		printf("alloc shared page failed\n");
		return;
	}
	printf("alloc shared page success\n");
	insert_share_vm(e, p);
	printf("insert shared page success\n");
	return;
}
// 创建一个轻量级线程（pthread）
/*
调用 env_alloc(&e, 0) 分配一个新的环境（线程）控制块 e。
调用 copy_curenv(e, curenv, func, arg)。这个函数的作用是让新线程 e 与当前进程 curenv 共享大部分资源（特别是页表，除了栈）。同时，它会将 func 设置为新线程的入口点 (e->env_tf.cp0_epc)，并将 arg 放入合适的寄存器（如 $a0）。
将新线程 e 添加到 env_runnable 循环链表中。
*/
void pthread_create(void *func, int arg)
{
	printf("pthread_create!!!!!!!!!!!!!!!!!\n");
	struct Env *e;
	int r;
	printf("status : %x \n", get_status());
	extern void debug();

	// 首先, 调用 env_alloc 函数分配一个线程控制块 env
	r = env_alloc(&e, 0);
	if (r < 0)
	{
		panic("sorry, env_create_priority:env_alloc failed");
		return;
	}

	// 调用 copy_curenv 函数, 将 新创建的线程 与 当前运行的进程, 设定一些共享的资源, 如页表
	copy_curenv(e, curenv, func, arg);

	// 将 env 加入 env_runnable_list
	if (env_runnable_head == NULL)
	{
		// env 是第一个加入 list 的进程（线程）
		printf("\nlist=null\n");
		env_runnable_head = env_runnable_tail = e;
		env_runnable_tail->env_link = env_runnable_head; // 成环
	}
	else
	{
		printf("list!=null\n");
		env_runnable_tail->env_link = e;
		env_runnable_tail = e;
		env_runnable_tail->env_link = env_runnable_head;
	}
	printf("list ID: 0x%x \n", env_runnable_head->env_id);
}

/*
 - 实现线程间的资源共享。让新线程 e 与源进程 env_src (即 curenv) 共享除栈之外的大部分内存资源。
 - 设置新线程的入口点 e->env_tf.cp0_epc = func 和参数 e->env_tf.regs[4] = arg。
 - 为新线程 e 分配一个新的页目录 pgdir。
	跟当前进程共享的内容：
	1. 堆（无）  全局变量 静态变量
	2. 文件控制块 elf代码的入口地址
*/
void copy_curenv(struct Env *e, struct Env *env_src, void *func, int arg) // 不确定
{
	Pte *pt;
	u_int pdeno, pteno, pa;
	e->env_tf.cp0_epc = func;
	e->env_tf.regs[4] = arg;
	printf("### curenv->CONTEXT: 0x%x \n", env_src->env_pgdir);
	Pde *pgdir;			   // 新的一级页表项
	struct Page *p = NULL; // 以及它对应的新页
	int r;

	// 为新线程分配页目录页并加引用
	if ((r = page_alloc(&p)) < 0)
	{
		panic("env_setup_vm - page alloc error\n");
		return;
	}
	p->pp_ref++;
	pgdir = (Pde *)(page2kva(p));
	printf("### e->CONTEXT: 0x%x \n", pgdir);
	e->env_pgdir = pgdir; // 将这个新的一级页表项，作为 copy env 的页表项
	for (pdeno = 0; pdeno < PDX(UTOP); pdeno++)
	{
		/* Hint: only look at mapped page tables. */
		if (!(env_src->env_pgdir[pdeno] & PTE_V))
		{ // 如果原 env 的一级页表项，没有映射到二级页表地址
			e->env_pgdir[pdeno] = 0;
			continue;
		}

		/* Hint: find the pa and va of the page table. */
		// e->env_pgdir[pdeno] = env_src->env_pgdir[pdeno]; // 直接拷贝二级页表地址，共享二级页表
		printf("content:0x%x\n", e->env_pgdir[pdeno]);
		pa = PTE_ADDR(env_src->env_pgdir[pdeno]); // 源二级页表物理地址
		pt = (Pte *)KADDR(pa);					  // 源二级页表虚拟地址
		pa2page(pa)->pp_ref++;					  // 增加二级页表的物理引用

		/* Hint: Unmap all PTEs in this page table. */
		for (pteno = 0; pteno <= PTX(~0); pteno++)
		{
			if (pt[pteno] & PTE_V)
			{
				int pa_tmp = PTE_ADDR(pt[pteno]);
				page_insert(e->env_pgdir, pa2page(pa_tmp), (pdeno << PDSHIFT) | (pteno << PGSHIFT), PTE_V | PTE_R);
				pa2page(pa_tmp)->pp_ref++; // 增加物理页的物理引用
			}
		}
	}
	// 线程具有自己独立的栈空间，因此，在拷贝完一级页表后，我们需要其中清空栈地址
	// 这样，线程就拥有了自身独立的栈
	for (pdeno = PDX(USTACKTOP); pdeno >= 0; pdeno--)
	{
		// 清空栈地址
		if (!(e->env_pgdir[pdeno] & PTE_V))
		{
			break;
		}
		pa = PTE_ADDR(env_src->env_pgdir[pdeno]); // 源二级页表物理地址
		pt = (Pte *)KADDR(pa);					  // 源二级页表虚拟地址
		pa2page(pa)->pp_ref--;					  // 减少二级页表的物理引用

		// 减少栈区域所有页面的引用计数
		for (pteno = 0; pteno <= PTX(~0); pteno++)
		{
			if (pt[pteno] & PTE_V)
			{
				int pa_tmp = PTE_ADDR(pt[pteno]);
				pa2page(pa_tmp)->pp_ref--; // 减少物理页的物理引用
			}
		}
		e->env_pgdir[pdeno] = 0;
	}

	// 设置自映射
	e->env_cr3 = PADDR(pgdir);
	e->env_pgdir[PDX(VPT)] = e->env_cr3;
	e->env_pgdir[PDX(UVPT)] = e->env_cr3 | PTE_V | PTE_R;
	printf("### e->CONTEXT: 0x%x \n", e->env_pgdir);
}

// 释放进程及其占用的所有资源
/* Overview:
 *  Frees env e and all memory it uses.
 * - 释放用户空间内存
 * - 释放页目录本身
 * - 从可运行队列中移除
 * - 归还环境控制块
 */
// 由于tlb_invalidate接口问题，现在只能释放curenv!!!
// 由于tlb_invalidate接口问题，现在只能释放curenv!!!
int env_free(struct Env *e)
{

	Pte *pt;
	u_int pdeno, pteno, pa;

	/* Hint: Note the environment's demise.*/
	printf("free env->id: 0x%x isCur? %d\n", e->env_id, curenv == e);

	/* Hint: Flush all mapped pages in the user portion of the address space */
	// 释放该 env 所分配的所有内存页
	for (pdeno = 0; pdeno < PDX(UTOP); pdeno++) // 遍历该进程的一级页表
	{
		/* Hint: only look at mapped page tables. */
		if (!(e->env_pgdir[pdeno] & PTE_V))
		{ // 是并没有映射的页表项
			continue;
		}

		/* Hint: find the pa and va of the page table. */
		pa = PTE_ADDR(e->env_pgdir[pdeno]); // 物理地址
		pt = (Pte *)KADDR(pa);				// 虚拟地址

		/* Hint: Unmap all PTEs in this page table. */
		for (pteno = 0; pteno <= PTX(~0); pteno++)
		{ // 遍历调用 page_remove 函数清空二级页表
			if (pt[pteno] & PTE_V)
			{
				page_remove(e->env_pgdir, (pdeno << PDSHIFT) | (pteno << PGSHIFT));
			}
		}

		/* Hint: free the page table itself. */
		e->env_pgdir[pdeno] = 0;
		page_decref(pa2page(pa)); // 释放
	}
	/* Hint: free the page directory. */
	pa = e->env_cr3;
	e->env_pgdir = 0;
	e->env_cr3 = 0;
	page_decref(pa2page(pa));

	// 从可运行队列中移除该进程
	struct Env *tempE = env_runnable_head;
	struct Env *tempE_pre = env_runnable_tail;
	while (tempE != e)
	{
		tempE_pre = tempE;
		tempE = tempE->env_link;
		if (tempE == env_runnable_head) // 回到起点
		{
			return 0; // 没找到
		}
	}
	// 现在我们找到了 e，tempE 指向 e，tempE_pre 是 e 前一个
	// 先保存下一个要运行的进程
	struct Env *next_env = e->env_link;

	// 检查是否还有可运行的进程（如果 next_env == e，说明只有一个进程）
	int has_runnable = (next_env != e);

	// 从 env_runnable 里删去 e
	tempE_pre->env_link = next_env;

	// 更新链表头尾指针
	if (env_runnable_head == e)
	{
		env_runnable_head = has_runnable ? next_env : NULL;
	}
	if (env_runnable_tail == e)
	{
		env_runnable_tail = has_runnable ? tempE_pre : NULL;
	}

	// 把 e 加入 env_free_list
	e->env_status = ENV_FREE;
	e->env_link = env_free_list; // e 指向 env_free_list 链表头
	env_free_list = e;			 // 把新的链表头赋为 e

	if (e == curenv)
	{
		clear_timer0_int();
		if (has_runnable)
		{
			// 还有其他可运行的进程，调度它
			printf("next env->id: 0x%x  cur env->id: %x\n", next_env->env_id, curenv->env_id);
			printf("free->sched \n");
			env_run(next_env);
		}
		else
		{
			// 没有可运行的进程了，进入空闲状态
			printf("All processes finished. System idle.\n");
			curenv = NULL;
			while (1)
			{
				// 空闲循环，等待中断或新进程
			}
		}
	}
	else
	{
		printf("env_free_not_current \n");
		return 1;
	}
}

/* Overview:
 *  Restores the register values in the Trapframe with the
 *  env_pop_tf, and context switch from curenv to env e.
 *
 * Post-Condition:
 *  Set 'e' as the curenv running environment.
 *
 * Hints:
 *  You may use these functions:
 *      env_pop_tf and lcontext.
 */
// 切换到环境 e 并运行它
void env_run(struct Env *e)
{

	curenv = e;
	curenv->env_runs++; // 该进程已经跑过的次数
	/*Step 3: Use lcontext() to switch to its address space. */
	lcontext((curenv->env_pgdir), &(curenv->env_tf)); // 切换上下文

	printf("### curenv-> ID: 0x%x  CONTEXT: 0x%x \n", curenv->env_id, curenv->env_pgdir);
	printf("### curenv-> env_runs: %d nextenv->env_id: 0x%x\n", curenv->env_runs, curenv->env_link->env_id);
	printf("### curenv-> epc:%x\n", curenv->env_tf.cp0_epc);
	printf("----------------------------\n");
	/*Step 4: Use env_pop_tf() to restore the environment's
	 * environment   registers and drop into user mode in the
	 * the   environment.
	 */
	/*environment registers and drop into user mode in the
			*the environment.
				* /
		/* Hint: You should use GET_ENV_ASID there.Think why? */
	set_asid(GET_ENV_ASID(curenv->env_id));
	env_pop_tf(&(curenv->env_tf)); // 恢复上下文

	// lcontext、set_asid、env_pop_tf，都在 env/env_asm.S 汇编里
}
