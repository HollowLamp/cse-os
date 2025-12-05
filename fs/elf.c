#include "elf.h"
#include <stddef.h>
#include "..\inc\printf.h"

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
 * 从 SD 卡加载 ELF 文件
 * Load ELF file from SD card
 *
 * 功能：
 * 与 load_elf 类似，但可能需要额外的 SD 卡读取验证
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

  // Iterate through all Program Headers - 遍历所有程序头
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

