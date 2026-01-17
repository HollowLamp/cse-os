#include "elf.h"
#include "ff.h"
#include <stddef.h>
#include "..\inc\printf.h"

/* 共享库加载缓冲区（静态分配） */
#define SO_BUF_SIZE 0x100000  /* 1MB */
static uint8_t so_load_buf[SO_BUF_SIZE] __attribute__((aligned(4)));

/* 共享库加载后的动态链接信息 */
static DynLinkInfo so_dyninfo;

/**
 * 内存填充函数
 * Memory set function
 * s 目标内存指针
 * c 填充字节值
 * n 填充字节数
 * return 目标内存指针
 */
static void *memSet(void *s, int c, size_t n)
{
  if (NULL == s || n < 0)
    return NULL;
  char * tmpS = (char *)s;
  while(n-- > 0)
    *tmpS++ = c;
  return s;
}

/**
 * 内存拷贝函数
 * Memory copy function
 * dest 目标内存指针
 * src 源内存指针
 * n 拷贝字节数
 * return 成功返回 1，失败返回 0
 */
int memCpy(void *dest, void *src, uint32_t n)
{
  if (NULL == dest || NULL == src || n < 0)
    return 0;

  uint32_t *tempDest = (uint32_t *)dest;
  uint32_t *tempSrc = (uint32_t *)src;
  uint32_t i = 0;

  // Copy 4 bytes at a time - 每次拷贝 4 字节
  for(i = 0; i < n / 4; i++) {
    tempDest[i] = tempSrc[i];
  }

  // Copy remaining bytes - 拷贝剩余字节
  char *destByte = (char *)dest + (i * 4);
  char *srcByte = (char *)src + (i * 4);
  for(i = 0; i < n % 4; i++) {
    destByte[i] = srcByte[i];
  }

  return 1;
}

/**
 * 从内存加载 ELF 文件
 * Load ELF file from memory
 *
 * 功能：
 * 1. 验证 ELF 文件头的合法性
 * 2. 解析 Program Headers
 * 3. 将各个可加载段复制到指定的虚拟地址
 * 4. 对 BSS 段进行零填充
 *
 * elf ELF 文件在内存中的起始地址
 * elf_size ELF 文件大小（字节）
 * return 成功返回 0，失败返回 -1
 */
int load_elf(const uint8_t *elf, const uint32_t elf_size) {
  // sanity checks - 合法性检查

  // Check if file is too small - 检查文件大小是否太小
  if (elf_size < sizeof(Elf32_Ehdr)) {
    printf("ELF file too small\n");
    return -1;                            /* too small */
  }

  const Elf32_Ehdr *eh = (const Elf32_Ehdr *)elf;

  // Check if it's a valid ELF32 file - 检查是否为有效的 ELF32 文件
  if (!IS_ELF32(*eh)) {
    printf("Not a valid ELF32 file\n");
    return -1;                            /* not a elf32 file */
  }

  // Check if Program Headers are valid - 检查 Program Headers 是否有效
  if (eh->e_phoff + eh->e_phnum * sizeof(Elf32_Phdr) > elf_size) {
    printf("ELF file internal damaged\n");
    return -1;                            /* internal damaged */
  }

  const Elf32_Phdr *ph = (const Elf32_Phdr *)(elf + eh->e_phoff);

  // Iterate through all Program Headers - 遍历所有程序头
  uint32_t i;
  for(i = 0; i < eh->e_phnum; i++) {
    if(ph[i].p_type == PT_LOAD && ph[i].p_memsz) { /* need to load this physical section */

      // Copy segment data from file to memory - 从文件复制段数据到内存
      if(ph[i].p_filesz) {                         /* has data */
        memCpy((void *)ph[i].p_vaddr,              // 目标虚拟地址
               (void *)(elf + ph[i].p_offset),     // 源文件偏移
               ph[i].p_filesz);                    // 文件中的大小
      }

      // Zero padding for BSS section - 对 BSS 段进行零填充
      if(ph[i].p_memsz > ph[i].p_filesz) {         /* zero padding */
        memSet((void *)(ph[i].p_vaddr + ph[i].p_filesz),  // 填充起始地址
               0,                                          // 填充值为 0
               ph[i].p_memsz - ph[i].p_filesz);            // 填充大小
      }
    }
  }
  return 0;
}

/**
 * 内部函数：加载共享库文件
 */
static uint32_t load_so_file(const char *so_name) {
    FIL fil;
    FRESULT fr;
    uint32_t br;

    printf("dynlink: Loading shared library: %s\n", so_name);

    /* 打开 .so 文件 */
    fr = f_open(&fil, so_name, FA_READ);
    if (fr != FR_OK) {
        printf("dynlink: Failed to open %s (error %d)\n", so_name, fr);
        return 0;
    }

    /* 检查文件大小 */
    if (fil.fsize > SO_BUF_SIZE) {
        printf("dynlink: %s too large (%d > %d)\n", so_name, fil.fsize, SO_BUF_SIZE);
        f_close(&fil);
        return 0;
    }

    /* 读取整个文件到缓冲区 */
    fr = f_read(&fil, so_load_buf, fil.fsize, &br);
    if (fr != FR_OK || br != fil.fsize) {
        printf("dynlink: Failed to read %s\n", so_name);
        f_close(&fil);
        return 0;
    }

    uint32_t so_size = fil.fsize;
    f_close(&fil);

    /* 验证 ELF 格式 */
    if (so_size < sizeof(Elf32_Ehdr)) {
        printf("dynlink: %s too small\n", so_name);
        return 0;
    }

    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)so_load_buf;
    if (!IS_ELF32(*eh)) {
        printf("dynlink: %s is not a valid ELF32 file\n", so_name);
        return 0;
    }

    /* 加载共享库的 PT_LOAD 段 */
    const Elf32_Phdr *ph = (const Elf32_Phdr *)(so_load_buf + eh->e_phoff);

    /*
     * 确定共享库的加载偏移
     * 如果第一个 LOAD 段的 p_vaddr 是 0（PIC），需要分配一个合适的加载地址
     * 使用固定的共享库加载基址：0x20000000
     */
    uint32_t first_vaddr = 0xFFFFFFFF;
    for (uint32_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_LOAD && ph[i].p_vaddr < first_vaddr) {
            first_vaddr = ph[i].p_vaddr;
        }
    }

    /* 计算加载偏移：如果共享库基址为 0，使用固定偏移 */
    uint32_t load_offset = 0;
    if (first_vaddr == 0) {
        load_offset = 0x20000000;  /* 共享库加载到 512MB 处 */
        printf("dynlink: PIC library, using load offset 0x%x\n", load_offset);
    }

    uint32_t so_base = load_offset + first_vaddr;

    for (uint32_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_LOAD && ph[i].p_memsz) {
            /* 实际加载地址 = 加载偏移 + 段虚拟地址 */
            uint32_t load_addr = load_offset + ph[i].p_vaddr;

            if (ph[i].p_filesz) {
                memCpy((void *)load_addr,
                       (void *)(so_load_buf + ph[i].p_offset),
                       ph[i].p_filesz);
            }

            if (ph[i].p_memsz > ph[i].p_filesz) {
                memSet((void *)(load_addr + ph[i].p_filesz),
                       0,
                       ph[i].p_memsz - ph[i].p_filesz);
            }

            printf("dynlink: Loaded segment to 0x%x (size=%x)\n", load_addr, ph[i].p_memsz);
        }
    }

    /* 解析共享库的动态节 */
    if (parse_dynamic_section(so_load_buf, so_size, &so_dyninfo) != 0) {
        printf("dynlink: Failed to parse %s dynamic section\n", so_name);
        return 0;
    }

    /*
     * 对于 PIC 共享库，DT_SYMTAB/DT_STRTAB 是相对于基址 0 的虚拟地址
     * 由于我们将共享库加载到了 load_offset 处，需要调整这些地址
     */
    if (load_offset != 0) {
        uint32_t symtab_vaddr = (uint32_t)so_dyninfo.symtab;
        uint32_t strtab_vaddr = (uint32_t)so_dyninfo.strtab;
        uint32_t got_vaddr = (uint32_t)so_dyninfo.got;

        /* 将虚拟地址转换为实际加载后的内存地址 */
        so_dyninfo.symtab = (Elf32_Sym *)(load_offset + symtab_vaddr);
        so_dyninfo.strtab = (const char *)(load_offset + strtab_vaddr);
        so_dyninfo.got = (uint32_t *)(load_offset + got_vaddr);
        so_dyninfo.base_addr = load_offset;  /* 记录加载偏移，用于符号地址计算 */

        printf("dynlink: Adjusted SO addresses: symtab=%x, strtab=%x, base=%x\n",
               (uint32_t)so_dyninfo.symtab, (uint32_t)so_dyninfo.strtab, load_offset);
    }

    /* 填充共享库自己的 GOT 表（用于访问自己的全局变量） */
    if (fill_got_table(&so_dyninfo, NULL) != 0) {
        printf("dynlink: Failed to fill %s GOT table\n", so_name);
        return 0;
    }

    printf("dynlink: %s loaded at base 0x%x\n", so_name, so_base);
    return so_base;
}

/**
 * 从 SD 卡加载 ELF 文件
 * Load ELF file from SD card
 *
 * 功能：
 * 1. 验证 ELF 文件头的合法性
 * 2. 解析 Program Headers
 * 3. 将各个可加载段复制到指定的虚拟地址
 * 4. 对 BSS 段进行零填充
 * 5. 【新增】自动检测并处理动态链接
 *
 * elf ELF 文件在内存中的起始地址（从 SD 卡读取后）
 * elf_size ELF 文件大小（字节）
 * return 成功返回 0，失败返回 -1
 */
int load_elf_sd(const uint8_t *elf, const uint32_t elf_size) {

  // sanity checks - 合法性检查

  // Check if file is too small - 检查文件大小
  if (elf_size < sizeof(Elf32_Ehdr)) {
    printf("ELF file too small\n");
    return -1;                             /* too small */
  }

  const Elf32_Ehdr *eh = (const Elf32_Ehdr *)elf;

  // Check if it's a valid ELF32 file - 检查是否为有效的 ELF32 文件
  if (!IS_ELF32(*eh)) {
    printf("Not a valid ELF32 file\n");
    return -1;                             /* not a elf32 file */
  }

  // Check if Program Headers are valid - 检查 Program Headers 是否有效
  if (eh->e_phoff + eh->e_phnum * sizeof(Elf32_Phdr) > elf_size) {
    printf("ELF file internal damaged\n");
    return -1;                             /* internal damaged */
  }

  const Elf32_Phdr *ph = (const Elf32_Phdr *)(elf + eh->e_phoff);

  // Iterate through all Program Headers - 遍历所有程序头，加载 PT_LOAD 段
  uint32_t i;
  for(i = 0; i < eh->e_phnum; i++) {
    if(ph[i].p_type == PT_LOAD && ph[i].p_memsz) {     /* need to load this physical section */
      if(ph[i].p_filesz) {                         /* has data */
        // Additional validation for SD card data - SD 卡数据的额外验证
        if (ph[i].p_offset + ph[i].p_filesz > elf_size) {
          printf("Segment exceeds file size\n");
          return -1;                                   /* internal damaged */
        }

        // Copy segment data - 复制段数据
        memCpy((void *)ph[i].p_vaddr,
               (void *)(elf + ph[i].p_offset),
               ph[i].p_filesz);
      }

      // Zero padding for BSS section - 对 BSS 段进行零填充
      if(ph[i].p_memsz > ph[i].p_filesz) {         /* zero padding */
        memSet((void *)(ph[i].p_vaddr + ph[i].p_filesz),
               0,
               ph[i].p_memsz - ph[i].p_filesz);
      }
    }
  }

  /*
   * 动态链接处理
   */

  /* 检查是否需要动态链接 */
  if (!elf_needs_dynlink(elf, elf_size)) {
    /* 静态链接的 ELF，直接返回 */
    return 0;
  }

  printf("dynlink: Detected dynamic linking, processing...\n");

  /* 解析主程序的动态节 */
  DynLinkInfo main_info;
  if (parse_dynamic_section(elf, elf_size, &main_info) != 0) {
    printf("dynlink: Failed to parse main program dynamic section\n");
    return -1;
  }

  /* 查找 PT_DYNAMIC 段中的 DT_NEEDED，加载依赖的共享库 */
  int has_so = 0;
  for (i = 0; i < eh->e_phnum; i++) {
    if (ph[i].p_type == PT_DYNAMIC) {
      const Elf32_Dyn *dyn = (const Elf32_Dyn *)(elf + ph[i].p_offset);

      for (int j = 0; dyn[j].d_tag != DT_NULL; j++) {
        if (dyn[j].d_tag == DT_NEEDED) {
          /* 获取库名（从已加载到内存的字符串表中读取） */
          const char *so_name = main_info.strtab + dyn[j].d_val;

          /* 加载共享库 */
          uint32_t so_base = load_so_file(so_name);
          if (so_base == 0) {
            printf("dynlink: Failed to load required library %s\n", so_name);
            return -1;
          }
          has_so = 1;
        }
      }
      break;
    }
  }

  /* 填充 GOT 表 */
  if (fill_got_table(&main_info, has_so ? &so_dyninfo : NULL) != 0) {
    printf("dynlink: Failed to fill GOT table\n");
    return -1;
  }

  printf("dynlink: Dynamic linking complete!\n");
  return 0;
}

/**
 * 获取 ELF 文件的入口地址
 * Get the entry point address of ELF file
 *
 * 功能：
 * 从 ELF 文件头中提取程序入口地址
 * 这是程序开始执行的第一条指令的地址
 *
 * elf ELF 文件在内存中的起始地址
 * elf_size ELF 文件大小（字节）
 * return ELF 程序入口地址
 */
uint32_t get_entry(const uint8_t *elf, const uint32_t elf_size)
{
    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)elf;
    const Elf32_Phdr *ph = (const Elf32_Phdr *)(elf + eh->e_phoff);
    return  eh->e_entry;
}

/*
 * 动态链接器实现
 */

/**
 * 字符串比较函数（避免依赖外部库）
 */
static int strCmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/**
 * 检查 ELF 是否需要动态链接
 * 通过查找 PT_INTERP 或 PT_DYNAMIC 段判断
 */
int elf_needs_dynlink(const uint8_t *elf, const uint32_t elf_size)
{
    if (elf_size < sizeof(Elf32_Ehdr)) {
        return 0;
    }

    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)elf;
    if (!IS_ELF32(*eh)) {
        return 0;
    }

    const Elf32_Phdr *ph = (const Elf32_Phdr *)(elf + eh->e_phoff);

    for (uint32_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_INTERP || ph[i].p_type == PT_DYNAMIC) {
            return 1;
        }
    }

    return 0;
}

/**
 * 解析动态节，提取符号表、字符串表、GOT 等信息
 */
int parse_dynamic_section(const uint8_t *elf, const uint32_t elf_size,
                          DynLinkInfo *info)
{
    if (!elf || !info || elf_size < sizeof(Elf32_Ehdr)) {
        return -1;
    }

    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)elf;
    const Elf32_Phdr *ph = (const Elf32_Phdr *)(elf + eh->e_phoff);

    /* 初始化 info */
    info->base_addr = 0;
    info->symtab = NULL;
    info->strtab = NULL;
    info->got = NULL;
    info->symtab_count = 0;
    info->local_gotno = 0;
    info->gotsym = 0;

    /* 查找 PT_DYNAMIC 段，同时确定是否是 PIC */
    const Elf32_Dyn *dyn = NULL;
    uint32_t first_load_vaddr = 0xFFFFFFFF;

    for (uint32_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_DYNAMIC) {
            dyn = (const Elf32_Dyn *)(elf + ph[i].p_offset);
        }
        /* 记录第一个 LOAD 段的地址 */
        if (ph[i].p_type == PT_LOAD && ph[i].p_vaddr < first_load_vaddr) {
            first_load_vaddr = ph[i].p_vaddr;
        }
    }

    /*
     * 对于非 PIC 可执行文件（first_load_vaddr != 0），符号地址是绝对地址
     * base_addr 应该是 0，不需要重定位
     * 对于 PIC 共享库（first_load_vaddr == 0），符号地址是相对偏移
     * base_addr 将由调用者设置为实际加载地址
     */
    info->base_addr = (first_load_vaddr == 0) ? 0 : 0;

    if (!dyn) {
        printf("dynlink: No PT_DYNAMIC segment found\n");
        return -1;
    }

    /* 解析动态节条目 */
    uint32_t strtab_addr = 0;
    uint32_t symtab_addr = 0;
    uint32_t got_addr = 0;

    for (int i = 0; dyn[i].d_tag != DT_NULL; i++) {
        switch (dyn[i].d_tag) {
            case DT_STRTAB:
                strtab_addr = dyn[i].d_val;
                break;
            case DT_SYMTAB:
                symtab_addr = dyn[i].d_val;
                break;
            case DT_PLTGOT:
                got_addr = dyn[i].d_val;
                break;
            case DT_MIPS_SYMTABNO:
                info->symtab_count = dyn[i].d_val;
                break;
            case DT_MIPS_LOCAL_GOTNO:
                info->local_gotno = dyn[i].d_val;
                break;
            case DT_MIPS_GOTSYM:
                info->gotsym = dyn[i].d_val;
                break;
            case DT_NEEDED:
                /* 记录需要的共享库（字符串表偏移） */
                printf("dynlink: DT_NEEDED at offset %d\n", dyn[i].d_val);
                break;
        }
    }

    /* 将文件偏移转换为内存地址 */
    /* 注意：对于已加载到内存的 ELF，这些地址就是实际地址 */
    info->strtab = (const char *)strtab_addr;
    info->symtab = (Elf32_Sym *)symtab_addr;
    info->got = (uint32_t *)got_addr;

    printf("dynlink: symtab=%x, strtab=%x, got=%x\n",
           symtab_addr, strtab_addr, got_addr);
    printf("dynlink: symtab_count=%d, local_gotno=%d, gotsym=%d\n",
           info->symtab_count, info->local_gotno, info->gotsym);

    return 0;
}

/**
 * 在符号表中查找符号
 * 对于 PIC 共享库，返回的地址需要加上 base_addr
 */
uint32_t lookup_symbol(const char *name, const DynLinkInfo *info)
{
    if (!name || !info || !info->symtab || !info->strtab) {
        return 0;
    }

    for (uint32_t i = 0; i < info->symtab_count; i++) {
        const Elf32_Sym *sym = &info->symtab[i];
        const char *sym_name = info->strtab + sym->st_name;

        if (strCmp(name, sym_name) == 0) {
            /* 检查符号是否已定义（st_shndx != 0 表示已定义） */
            if (sym->st_shndx != 0) {
                /* 符号地址 = 基址 + st_value */
                uint32_t addr = info->base_addr + sym->st_value;
                printf("dynlink: Found symbol '%s' at %x (base=%x, value=%x)\n",
                       name, addr, info->base_addr, sym->st_value);
                return addr;
            }
        }
    }

    return 0; /* 未找到 */
}

/**
 * 填充 GOT 表（MIPS 特定）
 *
 * MIPS GOT 结构：
 * GOT[0]: 保留（lazy resolver）
 * GOT[1]: 保留（模块信息）
 * GOT[2..local_gotno-1]: 本地项（已由链接器填充）
 * GOT[local_gotno..]: 全局符号项（需要动态链接器填充）
 *
 * 全局符号项对应符号表中从 gotsym 开始的符号
 */
int fill_got_table(DynLinkInfo *main_info, const DynLinkInfo *so_info)
{
    if (!main_info || !main_info->got || !main_info->symtab || !main_info->strtab) {
        printf("dynlink: Invalid main_info\n");
        return -1;
    }

    /* 计算需要填充的 GOT 项数量 */
    uint32_t global_gotno = main_info->symtab_count - main_info->gotsym;

    printf("dynlink: Filling %d GOT entries (starting at GOT[%d])\n",
           global_gotno, main_info->local_gotno);

    /* 遍历全局 GOT 项 */
    for (uint32_t i = 0; i < global_gotno; i++) {
        uint32_t got_index = main_info->local_gotno + i;
        uint32_t sym_index = main_info->gotsym + i;

        const Elf32_Sym *sym = &main_info->symtab[sym_index];
        const char *sym_name = main_info->strtab + sym->st_name;

        /* 调试：显示符号信息 */
        printf("dynlink: sym[%d] '%s': shndx=%d, value=%x\n",
               sym_index, sym_name, sym->st_shndx, sym->st_value);

        /* 如果符号在主程序中已定义，使用主程序的地址 */
        if (sym->st_shndx != 0 && sym->st_value != 0) {
            /* 对于 PIC 代码，st_value 是相对地址，需要加上 base_addr */
            uint32_t addr = main_info->base_addr + sym->st_value;
            main_info->got[got_index] = addr;
            printf("dynlink: GOT[%d] = %x (local: %s, base=%x, value=%x)\n",
                   got_index, addr, sym_name, main_info->base_addr, sym->st_value);
            continue;
        }

        /* 在共享库中查找 */
        uint32_t addr = 0;
        if (so_info) {
            addr = lookup_symbol(sym_name, so_info);
        }

        /* 如果共享库找不到，尝试在主程序符号表中搜索（可能是本地定义） */
        if (addr == 0) {
            addr = lookup_symbol(sym_name, main_info);
        }

        if (addr != 0) {
            main_info->got[got_index] = addr;
            printf("dynlink: GOT[%d] = %x (resolved: %s)\n",
                   got_index, addr, sym_name);
        } else {
            printf("dynlink: WARNING: Unresolved symbol '%s'\n", sym_name);
            /* 保持原值或设为 0 */
        }
    }

    return 0;
}

/**
 * 加载动态链接的 ELF 文件
 *
 * 处理流程：
 * 1. 加载主程序的 PT_LOAD 段
 * 2. 解析 PT_DYNAMIC 段，获取依赖库信息
 * 3. 调用 so_loader 加载依赖的 .so 文件
 * 4. 解析共享库的符号表
 * 5. 填充主程序的 GOT 表
 * 6. 返回（调用者通过 get_entry 获取入口地址）
 */
int load_elf_dynamic(const uint8_t *elf, const uint32_t elf_size,
                     uint32_t (*so_loader)(const char *so_name))
{
    printf("dynlink: Starting dynamic loading...\n");

    /* 1. 首先用静态加载器加载 PT_LOAD 段 */
    if (load_elf(elf, elf_size) != 0) {
        printf("dynlink: Failed to load PT_LOAD segments\n");
        return -1;
    }

    /* 2. 解析主程序的动态节 */
    DynLinkInfo main_info;
    if (parse_dynamic_section(elf, elf_size, &main_info) != 0) {
        printf("dynlink: Failed to parse dynamic section\n");
        return -1;
    }

    /* 3. 查找并加载依赖的共享库 */
    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)elf;
    const Elf32_Phdr *ph = (const Elf32_Phdr *)(elf + eh->e_phoff);

    int has_so = 0;

    /* 查找 PT_DYNAMIC 段中的 DT_NEEDED */
    for (uint32_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_DYNAMIC) {
            const Elf32_Dyn *dyn = (const Elf32_Dyn *)(elf + ph[i].p_offset);

            for (int j = 0; dyn[j].d_tag != DT_NULL; j++) {
                if (dyn[j].d_tag == DT_NEEDED && so_loader) {
                    /* 获取库名（从加载后的字符串表中读取） */
                    const char *so_name = main_info.strtab + dyn[j].d_val;
                    printf("dynlink: Loading shared library: %s\n", so_name);

                    /* 调用回调函数加载 .so 文件 */
                    /* load_so_file 会设置全局变量 so_dyninfo */
                    uint32_t so_base = so_loader(so_name);
                    if (so_base == 0) {
                        printf("dynlink: Failed to load %s\n", so_name);
                        return -1;
                    }
                    has_so = 1;
                }
            }
            break;
        }
    }

    /* 4. 填充 GOT 表，使用全局 so_dyninfo（由 load_so_file 设置） */
    if (fill_got_table(&main_info, has_so ? &so_dyninfo : NULL) != 0) {
        printf("dynlink: Failed to fill GOT table\n");
        return -1;
    }

    printf("dynlink: Dynamic loading complete!\n");
    return 0;
}

