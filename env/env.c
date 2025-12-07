
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

struct Env *envs = NULL;		  // All environments
struct Env *curenv = NULL;		  // the current env
extern int mCONTEXT;			  // current context
extern int curtf;				  // current trapframe
struct Env *env_free_list = NULL; // Free list

struct Env *env_runnable_head = NULL; // Runnable ring head
struct Env *env_runnable_tail = NULL; // Runnable ring tail
extern Pde *boot_pgdir;				  // kernel page directory
extern char *KERNEL_SP;				  // top of kernel stack
extern int remaining_time;			  // remaining time for current env

extern void env_pop_tf(struct Trapframe *tf); // 进入用户态
extern void lcontext(uint32_t contxt, int n); // 切换地址空间
extern void set_asid(uint32_t id);			  // 设置当前asid
extern int get_asid();						  // 获取当前asid
extern void set_epc(uint32_t epc);			  // 设置当前epc
/*
申请一个envid
 */
u_int mkenvid(struct Env *e)
{
	static u_int next_env_id = 0;

	/*Hint: lower bits of envid hold e's position in the envs array. */
	u_int idx = e - envs;
	/*Hint:  high bits of envid hold an increasing number. */
	return (++next_env_id << (1 + LOG2NENV)) | idx;
}

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
	/*Step 1: Assign value to e using envid. */
	if (envid == 0)
	{
		*penv = curenv;
		return 0;
	}
	e = envs + GET_ENV_ASID(envid);
	if (e->env_status == ENV_FREE || e->env_id != envid) // todo 为什么检查free，这个函数啥时候调用
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

// 初始化所有env，链到env_free_list上
void env_init(void)
{
	int i;
	for (i = NENV - 1; i >= 0; i--)
	{
		envs[i].env_id = 0XFFFFFFFF;
		envs[i].env_status = ENV_FREE;
		envs[i].env_link = env_free_list;
		env_free_list = &envs[i];
		envs[i].heap_pc = UTOP;
	}
}

// 初始化e的虚拟地址空间
static int
env_setup_vm(struct Env *e)
{

	int i, r;
	struct Page *p = NULL;
	Pde *pgdir;

	/*Step 1: Allocate a page for the page directory using a function you completed in the lab2.
	 * and add its reference.
	 *pgdir is the page directory of Env e, assign value for it. */
	// 分配页目录页并加引用
	if ((r = page_alloc(&p)) < 0)
	{
		panic("env_setup_vm - page alloc error\n");
		return r;
	}
	p->pp_ref++;
	pgdir = (Pde *)page2kva(p);
	e->env_pgdir = pgdir;
	e->env_cr3 = PADDR(pgdir);
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

int env_alloc(struct Env **new, u_int parent_id)
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
		panic("env_alloc: env_setup_vm failed");
		return r;
	}
	/*Step 3: Initialize every field of new Env with appropriate values*/
	e->env_id = mkenvid(e);

	/*Step 4: focus on initializing env_tf structure, located at this new Env.
	 * especially the sp register,CPU status. */
	// 初始化父 ID、状态、状态字、栈指针
	e->env_parent_id = parent_id;
	e->env_status = ENV_RUNNABLE;
	e->env_tf.cp0_status = 0x10007c01;
	e->env_tf.regs[29] = USTACKTOP;	 // 栈顶
	e->env_tf.regs[31] = 0x90000000; // 返回地址（指向结束的系统调用处理函数）
	e->env_runs = 0;
	*new = e;

	/*Step 5: Remove the new Env from Env free list*/
	env_free_list = env_free_list->env_link;
	return 0;
}

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
	return 0;
}

FATFS FatFs; // Work area (file system object) for logical drive

// max size of file image is 16M
#define MAX_FILE_SIZE 0x1000000

// size of DDR RAM (256M for Minisys)
#define DDR_SIZE 0x10000000

// 4K size read burst
#define SD_READ_SIZE 4096

uint32_t get_ddr_base()
{
	return 0x80000000;
}

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
	uint8_t *buf = boot_file_buf;
	uint32_t fsize = 0; // file size count
	uint32_t br;		// Read count
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

	lcontext(e->env_pgdir, 0); // 切换到要新建的进程的asid，之后缺页中断会填这个进程的tlb
	set_asid(GET_ENV_ASID(e->env_id));

	// read elf
	if (load_elf_sd(boot_file_buf, fsize) != 0)
		printf("elf read failed with code %d \n\r", br);

	uint32_t entry_point = get_entry(boot_file_buf, fil.fsize);

	// 恢复之前环境
	lcontext(pre_pgdir, pre_curtf);
	set_asid(pre_asid); // 这里和上面是一对的

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
static void
load_icode(struct Env *e, char *elf_name)
{
	/* Hint:
	 *  You must figure out which permissions you'll need
	 *  for the different mappings you create.
	 *  Remember that the binary image is an a.out format image,
	 *  which contains both text and data.
	 */
	struct Page *p = NULL;
	uint32_t entry_point;
	u_long r;
	u_long perm;
	/*Step 1: alloc a page. */
	r = page_alloc(&p);
	if (r < 0)
	{
		panic("load_icode: page alloc failed");
		return;
	}

	/*Step 2: Use appropriate perm to set initial stack for new Env. */
	/*Hint: The user-stack should be writable? */
	// 栈映射 USTACKTOP-BY2PG，可写
	perm = PTE_V | PTE_R;
	r = page_insert(e->env_pgdir, p, USTACKTOP - BY2PG, perm);
	assert(r == 0);

	printf("load_elf:%s\n", elf_name);
	entry_point = load_elf_mapper(elf_name, e);
	assert(entry_point != 1); // load 失败

	e->env_tf.cp0_epc = entry_point;
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
void env_create_priority(char *binary, int priority)
{
	struct Env *e, *tmp;
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
	// 环形链表加入可运行队列
	if (env_runnable_head == NULL) // 第一个
	{
		env_runnable_head = env_runnable_tail = e;
		env_runnable_tail->env_link = env_runnable_head; // 成环
	}
	else
	{
		env_runnable_tail->env_link = e;
		env_runnable_tail = e;
		env_runnable_tail->env_link = env_runnable_head;
	}
	tmp = env_runnable_head;
	printf("list ID: 0x%x \n", env_runnable_head->env_id);
	while (tmp != env_runnable_tail)
	{
		printf(" 0x%x ", tmp->env_id);
	}
	printf("\ntail ID: 0x%x \n", env_runnable_tail->env_id);
}

void env_create_priority_arg(char *binary, int priority, char *arg)
{
	struct Env *e, *tmp;
	int r;
	extern void debug();
	/*Step 1: Use env_alloc to alloc a new env. */
	r = env_alloc_arg(&e, 0, arg);
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
	if (env_runnable_head == NULL) // 第一个
	{
		env_runnable_head = env_runnable_tail = e;
		env_runnable_tail->env_link = env_runnable_head; // 成环
	}
	else
	{
		env_runnable_tail->env_link = e;
		env_runnable_tail = e;
		env_runnable_tail->env_link = env_runnable_head;
	}
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

void env_create(char *binary, int *pt)
{
	env_create_priority(binary, 1);
}

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
	if (env_runnable_head == NULL)
	{
		env_runnable_head = env_runnable_tail = e;
		env_runnable_tail->env_link = env_runnable_head;
	}
	else
	{
		env_runnable_tail->env_link = e;
		env_runnable_tail = e;
		env_runnable_tail->env_link = env_runnable_head;
	}
	printf("list ID: 0x%x \n", env_runnable_head->env_id);
	printf("tail ID: 0x%x \n", env_runnable_tail->env_id);

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

void pthread_create(void *func, int arg)
{
	printf("pthread_create!!!!!!!!!!!!!!!!!\n");
	struct Env *e;
	int r;
	printf("status : %x \n", get_status());
	extern void debug();
	r = env_alloc(&e, 0);
	if (r < 0)
	{
		panic("sorry, env_create_priority:env_alloc failed");
		return;
	}

	copy_curenv(e, curenv, func, arg);
	if (env_runnable_head == NULL) // 第一个
	{
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
	printf("tail ID: 0x%x \n", env_runnable_tail->env_id);
}

/*
	跟当前进程共享的内容：
	1. 堆（无）  全局变量 静态变量
	2. 文件控制块 elf代码的入口地址
*/
void copy_curenv(struct Env *e, struct Env *env_src, void *func, int arg)
{
	/*
	e：新线程
	env_src：当前线程
	func：新线程入口地址
	arg：新线程参数
	*/
	Pte *pt;
	u_int pdeno, pteno, pa;
	// 设置线程的入口点和参数
	e->env_tf.cp0_epc = func;
	e->env_tf.regs[4] = arg;
	printf("### curenv->CONTEXT: 0x%x \n", env_src->env_pgdir);
	struct Page *p = NULL;
	Pde *pgdir;
	int r;

	// 为新线程分配页目录页并加引用
	if ((r = page_alloc(&p)) < 0)
	{
		panic("env_setup_vm - page alloc error\n");
		return r;
	}
	p->pp_ref++;
	pgdir = (Pde *)page2kva(p);
	printf("### e->CONTEXT: 0x%x \n", pgdir);
	e->env_pgdir = pgdir;

	// 复制用户空间的页目录项
	for (pdeno = 0; pdeno < PDX(UTOP); pdeno++)
	{
		/* Hint: only look at mapped page tables. */
		// 只拷贝有映射的二级页表
		if (env_src->env_pgdir[pdeno] == 0)
		{
			continue;
		}
		/* Hint: find the pa and va of the page table. */
		// 拷贝二级页表地址
		printf("content:0x%x\n", e->env_pgdir[pdeno]);
		pa = PTE_ADDR(env_src->env_pgdir[pdeno]);		 // 源二级页表物理地址
		pt = (Pte *)KADDR(pa);							 // 源二级页表虚拟地址
		e->env_pgdir[pdeno] = env_src->env_pgdir[pdeno]; // 复制二级页表地址
		p = pa2page(pa);								 // 二级页表对应的物理页面
		p->pp_ref++;									 // 增加二级页表物理引用

		/* Hint: Unmap all PTEs in this page table. */
		for (pteno = 0; pteno <= PTX(~0); pteno++) //
		{
			if (pt[pteno] & PTE_V)
			{
				pa2page(PTE_ADDR(pt[pteno]))->pp_ref++;
			}
		}
	}

	// 清理用户栈区域（让新线程拥有独立的栈）
	for (pdeno = PDX(USTACKTOP); pdeno >= 0; pdeno--)
	{
		// 清空栈地址
		if (e->env_pgdir[pdeno] == 0) // 如果该页目录为空，说明更低地址也没有映射
		{
			break;
		}
		pa = PTE_ADDR(e->env_pgdir[pdeno]); // 源二级页表物理地址
		pt = (Pte *)KADDR(pa);				// 源二级页表虚拟地址
		pa2page(pa)->pp_ref--;				// 减少二级页表物理引用

		// 减少栈区域所有页面的引用计数
		for (pteno = 0; pteno <= PTX(~0); pteno++)
		{
			if (pt[pteno] & PTE_V)
			{
				page_remove(e->env_pgdir, (pdeno << PDSHIFT) | (pteno << PGSHIFT));
			}
		}
		e->env_pgdir[pdeno] = 0;
	}

	// 设置自映射
	e->env_cr3 = PADDR(pgdir);
	e->env_pgdir[PDX(VPT)] = e->env_cr3;
	e->env_pgdir[PDX(UVPT)] = e->env_cr3 | PTE_V | PTE_R;
	printf("### e->CONTEXT: 0x%x \n", e->env_pgdir);

	return;
}

/* Overview:
 *  Frees env e and all memory it uses.
 */
// 由于tlb_invalidate接口问题，现在只能释放curenv!!!
int env_free(struct Env *e)
{

	Pte *pt;
	u_int pdeno, pteno, pa;

	/* Hint: Note the environment's demise.*/
	printf("free env->id: 0x%x isCur? %d\n", e->env_id, curenv == e);

	/* Hint: Flush all mapped pages in the user portion of the address space */
	for (pdeno = 0; pdeno < PDX(UTOP); pdeno++)
	{
		/* Hint: only look at mapped page tables. */
		if (e->env_pgdir[pdeno] == 0) // 该页目录项为空，跳过
		{
			continue;
		}
		/* Hint: find the pa and va of the page table. */
		pa = PTE_ADDR(e->env_pgdir[pdeno]); // 物理地址
		pt = (Pte *)KADDR(pa);				// 虚拟地址

		/* Hint: Unmap all PTEs in this page table. */
		for (pteno = 0; pteno <= PTX(~0); pteno++)
		{
			if (pt[pteno] & PTE_V) // 页表有效项
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
	e->env_pgdir = NULL;
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

	if (tempE == env_runnable_tail)
	{
		env_runnable_tail = tempE_pre;
	}
	if (env_runnable_head == e)
	{
		env_runnable_head = env_runnable_head->env_link;
	}

	// 从env_runnable里删去e
	tempE_pre->env_link = tempE->env_link;

	// 保存下来下一个
	e->env_link = env_free_list;
	env_free_list = e;

	if (e == curenv)
	{
		printf("next env->id: 0x%x  cur env->id: %x\n", tempE->env_id, curenv->env_id);
		clear_timer0_int();
		printf("free->sched \n");
		env_run(tempE);
	}
	else
	{
		printf("env_free_not_current \n");
		while (1)
			;
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
void env_run(struct Env *e)
{

	curenv = e;
	curenv->env_runs++;
	/*Step 3: Use lcontext() to switch to its address space. */
	lcontext(curenv->env_pgdir, 0);
	set_asid(GET_ENV_ASID(curenv->env_id));

	printf("### curenv-> ID: 0x%x  CONTEXT: 0x%x \n", curenv->env_id, curenv->env_pgdir);
	printf("### curenv-> env_runs: %d nextenv->env_id: 0x%x\n", curenv->env_runs, curenv->env_link->env_id);
	printf("### curenv-> epc:%x\n", curenv->env_tf.cp0_epc);
	printf("----------------------------\n");
	/*Step 4: Use env_pop_tf() to restore the environment's
	 * environment   registers and drop into user mode in the
	 * the   environment.
	 */
	/* Hint: You should use GET_ENV_ASID there.Think why? */
	env_pop_tf(&curenv->env_tf);
}
