# Makefile for dynamic linking test
# 用于编译动态链接测试程序

include ../include.mk

# 编译选项
# -fPIC: 生成位置无关代码（Position Independent Code）
# -shared: 生成共享库
# -mabicalls: MIPS ABI 调用约定（用于动态链接）
PIC_FLAGS = -fPIC -mabicalls
SHARED_FLAGS = -shared

# 目标文件
LIBMATH = libmath.so
DYNTEST = dyntest.elf

# 默认目标
all: $(LIBMATH) $(DYNTEST) analyze

# 编译共享库
libmath.o: libmath.c
	$(CC) $(CFLAGS) $(PIC_FLAGS) -c -o $@ $<

$(LIBMATH): libmath.o
	$(LD) -EL -shared -o $@ $<

# 编译主程序（动态链接版本）
dyntest.o: dyntest.c
	$(CC) $(CFLAGS) $(PIC_FLAGS) -c -o $@ $<

# 链接主程序（动态链接 libmath.so）
$(DYNTEST): dyntest.o $(LIBMATH)
	$(LD) -EL -o $@ dyntest.o -L. -lmath -dynamic-linker /lib/ld.so

# 分析重定位信息
analyze: $(LIBMATH) $(DYNTEST)
	@echo "=========================================="
	@echo "=== libmath.so 重定位信息 ==="
	@echo "=========================================="
	$(OD) -r $(LIBMATH) > libmath_reloc.txt
	$(OD) -t $(LIBMATH) > libmath_symbols.txt
	$(OD) -h $(LIBMATH) > libmath_sections.txt
	@echo ""
	@echo "=========================================="
	@echo "=== dyntest.elf 重定位信息 ==="
	@echo "=========================================="
	$(OD) -r $(DYNTEST) > dyntest_reloc.txt
	$(OD) -t $(DYNTEST) > dyntest_symbols.txt
	$(OD) -h $(DYNTEST) > dyntest_sections.txt
	@echo ""
	@echo "=== 查看生成的 *_reloc.txt 文件了解需要处理的重定位类型 ==="

# 生成反汇编
disasm: $(LIBMATH) $(DYNTEST)
	$(OD) -D $(LIBMATH) > libmath.dis
	$(OD) -D $(DYNTEST) > dyntest.dis

# 静态链接版本（用于对比）
static: libmath_static.o dyntest_static.o
	$(LD) -EL -static -o dyntest_static.elf dyntest_static.o libmath_static.o

libmath_static.o: libmath.c
	$(CC) $(CFLAGS) -c -o $@ $<

dyntest_static.o: dyntest.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 清理
clean:
	rm -f *.o *.so *.elf *.txt *.dis

.PHONY: all analyze disasm static clean
