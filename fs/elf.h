/**
 * ELF (Executable and Linkable Format) 文件格式定义
 * 用于加载可执行文件到内存
 */
#ifndef _ELF_H
#define _ELF_H

#include <stdint.h>

/**
 * 检查是否为有效的 ELF 文件
 * ELF 魔数：0x7F 'E' 'L' 'F'
 */
#define IS_ELF(hdr) \
  ((hdr).e_ident[0] == 0x7f && (hdr).e_ident[1] == 'E' && \
   (hdr).e_ident[2] == 'L'  && (hdr).e_ident[3] == 'F')

/**
 * 检查是否为 32 位 ELF 文件
 * Check if the ELF file is 32-bit
 */
#define IS_ELF32(hdr) (IS_ELF(hdr) && (hdr).e_ident[4] == 1)

/**
 * 检查是否为 64 位 ELF 文件
 * Check if the ELF file is 64-bit
 */
#define IS_ELF64(hdr) (IS_ELF(hdr) && (hdr).e_ident[4] == 2)

/**
 * Program Header 类型
 */
#define PT_NULL     0   /* 未使用 */
#define PT_LOAD     1   /* 可加载段 */
#define PT_DYNAMIC  2   /* 动态链接信息 */
#define PT_INTERP   3   /* 解释器路径 */

/**
 * Section Header 类型：未初始化数据段（BSS）
 * SHT_NOBITS - Section with no data (BSS)
 */
#define SHT_NOBITS 8

/*
 * 动态链接相关定义
 */

/**
 * 动态节条目结构 (Dynamic Section Entry)
 * 用于描述动态链接信息
 */
typedef struct {
    int32_t  d_tag;     /* 条目类型 */
    uint32_t d_val;     /* 值（可以是地址或整数） */
} Elf32_Dyn;

/**
 * 动态节标签类型 (d_tag values)
 */
#define DT_NULL     0   /* 结束标记 */
#define DT_NEEDED   1   /* 依赖的共享库名称（字符串表偏移） */
#define DT_PLTGOT   3   /* GOT 表地址 */
#define DT_HASH     4   /* 符号哈希表地址 */
#define DT_STRTAB   5   /* 字符串表地址 */
#define DT_SYMTAB   6   /* 符号表地址 */
#define DT_STRSZ    10  /* 字符串表大小 */
#define DT_SYMENT   11  /* 符号表项大小 */

/* MIPS 特定的动态标签 */
#define DT_MIPS_LOCAL_GOTNO  0x7000000a  /* 本地 GOT 项数量 */
#define DT_MIPS_SYMTABNO     0x70000011  /* 符号表项数量 */
#define DT_MIPS_GOTSYM       0x70000013  /* GOT 中第一个符号的索引 */

/**
 * 重定位条目结构 (Relocation Entry)
 */
typedef struct {
    uint32_t r_offset;  /* 重定位位置（相对于段基址的偏移） */
    uint32_t r_info;    /* 符号索引和重定位类型 */
} Elf32_Rel;

/**
 * 带加数的重定位条目结构
 */
typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
    int32_t  r_addend;  /* 加数 */
} Elf32_Rela;

/**
 * 提取重定位信息的宏
 */
#define ELF32_R_SYM(info)   ((info) >> 8)       /* 符号索引 */
#define ELF32_R_TYPE(info)  ((info) & 0xff)     /* 重定位类型 */

/**
 * MIPS 重定位类型（基于测试程序需要的类型）
 */
#define R_MIPS_NONE     0   /* 无操作 */
#define R_MIPS_32       2   /* 32位绝对地址: S + A */
#define R_MIPS_HI16     5   /* 高16位: ((AHL + S) >> 16) */
#define R_MIPS_LO16     6   /* 低16位: (AHL + S) & 0xFFFF */
#define R_MIPS_GOT16    9   /* GOT 项偏移 */
#define R_MIPS_CALL16   11  /* 函数调用 GOT 偏移 */
#define R_MIPS_JALR     45  /* 间接跳转提示（可忽略） */

/**
 * 符号绑定类型
 */
#define STB_LOCAL   0   /* 本地符号 */
#define STB_GLOBAL  1   /* 全局符号 */
#define STB_WEAK    2   /* 弱符号 */

#define ELF32_ST_BIND(info)  ((info) >> 4)
#define ELF32_ST_TYPE(info)  ((info) & 0xf)

/**
 * ELF32 符号表项结构（前置声明，用于 DynLinkInfo）
 * ELF32 Symbol Table Entry Structure
 */
typedef struct
{
  uint32_t st_name;         // 符号名在字符串表中的偏移 (Symbol name offset)
  uint32_t st_value;        // 符号值（地址） (Symbol value/address)
  uint32_t st_size;         // 符号大小 (Symbol size)
  uint8_t  st_info;         // 符号类型和绑定 (Symbol type and binding)
  uint8_t  st_other;        // 符号可见性 (Symbol visibility)
  uint16_t st_shndx;        // 符号所在节索引 (Section index)
} Elf32_Sym;

/**
 * 动态链接信息结构（解析后的缓存）
 */
typedef struct {
    uint32_t    base_addr;      /* 加载基地址 */
    Elf32_Sym  *symtab;         /* 符号表指针 */
    const char *strtab;         /* 字符串表指针 */
    uint32_t   *got;            /* GOT 表指针 */
    uint32_t    symtab_count;   /* 符号表项数量 */
    uint32_t    local_gotno;    /* 本地 GOT 项数量 */
    uint32_t    gotsym;         /* GOT 中第一个符号的索引 */
} DynLinkInfo;

/**
 * ELF32 文件头结构
 * ELF32 File Header Structure
 */
typedef struct {
  uint8_t  e_ident[16];     // ELF 标识魔数 (Magic number and other info)
  uint16_t e_type;          // 文件类型 (Object file type)
  uint16_t e_machine;       // 目标架构 (Target architecture)
  uint32_t e_version;       // ELF 版本 (Object file version)
  uint32_t e_entry;         // 程序入口地址 (Entry point virtual address)
  uint32_t e_phoff;         // Program Header 偏移 (Program header table offset)
  uint32_t e_shoff;         // Section Header 偏移 (Section header table offset)
  uint32_t e_flags;         // 处理器特定标志 (Processor-specific flags)
  uint16_t e_ehsize;        // ELF 文件头大小 (ELF header size)
  uint16_t e_phentsize;     // Program Header 项大小 (Program header entry size)
  uint16_t e_phnum;         // Program Header 项数 (Number of program headers)
  uint16_t e_shentsize;     // Section Header 项大小 (Section header entry size)
  uint16_t e_shnum;         // Section Header 项数 (Number of section headers)
  uint16_t e_shstrndx;      // 字符串表索引 (Section header string table index)
} Elf32_Ehdr;

/**
 * ELF32 节头结构
 * ELF32 Section Header Structure
 */
typedef struct {
  uint32_t sh_name;         // 节名在字符串表中的偏移 (Section name offset in string table)
  uint32_t sh_type;         // 节类型 (Section type)
  uint32_t sh_flags;        // 节标志 (Section flags)
  uint32_t sh_addr;         // 节在内存中的地址 (Section virtual address)
  uint32_t sh_offset;       // 节在文件中的偏移 (Section file offset)
  uint32_t sh_size;         // 节大小 (Section size in bytes)
  uint32_t sh_link;         // 链接到其他节 (Link to another section)
  uint32_t sh_info;         // 附加信息 (Additional section information)
  uint32_t sh_addralign;    // 节对齐 (Section alignment)
  uint32_t sh_entsize;      // 节项大小 (Entry size if section holds table)
} Elf32_Shdr;

/**
 * ELF32 程序头结构
 * ELF32 Program Header Structure
 * 描述如何将 ELF 文件加载到内存
 */
typedef struct
{
  uint32_t p_type;          // 段类型 (Segment type: PT_LOAD, PT_DYNAMIC, etc.)
  uint32_t p_offset;        // 段在文件中的偏移 (Segment file offset)
  uint32_t p_vaddr;         // 段的虚拟地址 (Segment virtual address)
  uint32_t p_paddr;         // 段的物理地址 (Segment physical address)
  uint32_t p_filesz;        // 段在文件中的大小 (Segment size in file)
  uint32_t p_memsz;         // 段在内存中的大小 (Segment size in memory)
  uint32_t p_flags;         // 段标志 (Segment flags: R/W/X)
  uint32_t p_align;         // 段对齐 (Segment alignment)
} Elf32_Phdr;

/* 注意：Elf32_Sym 已在前面定义（动态链接需要） */

/**
 * ELF64 文件头结构（64位系统使用）
 * ELF64 File Header Structure (for 64-bit systems)
 */
typedef struct {
  uint8_t  e_ident[16];     // ELF 标识魔数
  uint16_t e_type;          // 文件类型
  uint16_t e_machine;       // 目标架构
  uint32_t e_version;       // ELF 版本
  uint64_t e_entry;         // 程序入口地址（64位）
  uint64_t e_phoff;         // Program Header 偏移（64位）
  uint64_t e_shoff;         // Section Header 偏移（64位）
  uint32_t e_flags;         // 处理器特定标志
  uint16_t e_ehsize;        // ELF 文件头大小
  uint16_t e_phentsize;     // Program Header 项大小
  uint16_t e_phnum;         // Program Header 项数
  uint16_t e_shentsize;     // Section Header 项大小
  uint16_t e_shnum;         // Section Header 项数
  uint16_t e_shstrndx;      // 字符串表索引
} Elf64_Ehdr;

/**
 * ELF64 节头结构（64位系统使用）
 * ELF64 Section Header Structure
 */
typedef struct {
  uint32_t sh_name;         // 节名偏移
  uint32_t sh_type;         // 节类型
  uint64_t sh_flags;        // 节标志（64位）
  uint64_t sh_addr;         // 节虚拟地址（64位）
  uint64_t sh_offset;       // 节文件偏移（64位）
  uint64_t sh_size;         // 节大小（64位）
  uint32_t sh_link;         // 链接到其他节
  uint32_t sh_info;         // 附加信息
  uint64_t sh_addralign;    // 节对齐（64位）
  uint64_t sh_entsize;      // 节项大小（64位）
} Elf64_Shdr;

/**
 * ELF64 程序头结构（64位系统使用）
 * ELF64 Program Header Structure
 */
typedef struct {
  uint32_t p_type;          // 段类型
  uint32_t p_flags;         // 段标志
  uint64_t p_offset;        // 段文件偏移（64位）
  uint64_t p_vaddr;         // 段虚拟地址（64位）
  uint64_t p_paddr;         // 段物理地址（64位）
  uint64_t p_filesz;        // 段文件大小（64位）
  uint64_t p_memsz;         // 段内存大小（64位）
  uint64_t p_align;         // 段对齐（64位）
} Elf64_Phdr;

/**
 * ELF64 符号表项结构（64位系统使用）
 * ELF64 Symbol Table Entry Structure
 */
typedef struct {
  uint32_t st_name;         // 符号名偏移
  uint8_t  st_info;         // 符号类型和绑定
  uint8_t  st_other;        // 符号可见性
  uint16_t st_shndx;        // 符号所在节索引
  uint64_t st_value;        // 符号值/地址（64位）
  uint64_t st_size;         // 符号大小（64位）
} Elf64_Sym;

/**
 * 从内存加载 ELF 文件
 * Load ELF file from memory
 * elf ELF 文件数据指针
 * elf_size ELF 文件大小
 * return 成功返回 0，失败返回非 0
 */
extern int load_elf(const uint8_t *elf, const uint32_t elf_size);

/**
 * 从 SD 卡加载 ELF 文件
 * Load ELF file from SD card
 * elf ELF 文件数据指针
 * elf_size ELF 文件大小
 * return 成功返回 0，失败返回非 0
 */
int load_elf_sd(const uint8_t *elf, const uint32_t elf_size);

/**
 * 获取 ELF 文件的入口地址
 * Get the entry point address of ELF file
 * elf ELF 文件数据指针
 * elf_size ELF 文件大小
 * return ELF 程序入口地址
 */
uint32_t get_entry(const uint8_t *elf, const uint32_t elf_size);

/**
 * 动态链接器函数
 */

/**
 * 检查 ELF 是否需要动态链接
 */
int elf_needs_dynlink(const uint8_t *elf, const uint32_t elf_size);

/**
 * 加载动态链接的 ELF 文件
 * 处理流程：加载主程序 -> 加载依赖的 .so -> 符号解析 -> 重定位 -> 返回入口
 */
int load_elf_dynamic(const uint8_t *elf, const uint32_t elf_size,
                     uint32_t (*so_loader)(const char *so_name));

/**
 * 解析动态节，提取符号表、字符串表、GOT 等信息
 */
int parse_dynamic_section(const uint8_t *elf, const uint32_t elf_size,
                          DynLinkInfo *info);

/**
 * 在符号表中查找符号
 */
uint32_t lookup_symbol(const char *name, const DynLinkInfo *info);

/**
 * 填充 GOT 表（MIPS 特定）
 */
int fill_got_table(DynLinkInfo *main_info, const DynLinkInfo *so_info);

#endif