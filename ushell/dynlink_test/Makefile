# Makefile for dynamic linking test
# 用于编译动态链接测试程序 还需要链接自带的系统调用库

include ../include.mk

# 头文件路径
INCLUDES = -I../inc/ -I../user/

# 编译选项
# -fPIC: 生成位置无关代码（Position Independent Code）
# -mabicalls: MIPS ABI 调用约定（用于动态链接）
PIC_FLAGS = -fPIC -mabicalls

# 用户库对象文件
USER_OBJS = ../user/syscall_lib.o ../user/syscall_wrap.o ../user/string.o

# 目标文件
LIBMATH = libmath.so
DYNTEST = dyntest.elf

# 默认目标：先编译用户库，再编译测试程序
all: user_lib $(LIBMATH) $(DYNTEST) analyze

# 编译用户库
user_lib:
	$(MAKE) -C ../user

# 编译共享库
libmath.o: libmath.c
	$(CC) $(CFLAGS) $(PIC_FLAGS) -c -o $@ $<

$(LIBMATH): libmath.o
	$(LD) -EL -shared -o $@ $<

# 编译主程序（动态链接版本）
dyntest.o: dyntest.c
	$(CC) $(CFLAGS) $(PIC_FLAGS) $(INCLUDES) -c -o $@ $<

# 链接主程序（动态链接 libmath.so）
# 注意：这里先生成动态链接格式的 ELF，用于分析重定位类型
# 需要链接系统调用库，使得 syscall_printf 等函数在主程序中定义
$(DYNTEST): dyntest.o $(LIBMATH) $(USER_OBJS)
	$(LD) -EL -o $@ dyntest.o $(USER_OBJS) -L. -lmath

# 分析重定位信息
analyze: $(LIBMATH) $(DYNTEST)
	@echo "=========================================="
	@echo "=== libmath.so Relocation Info ==="
	@echo "=========================================="
	$(OD) -r $(LIBMATH) > libmath_reloc.txt
	$(OD) -t $(LIBMATH) > libmath_symbols.txt
	$(OD) -h $(LIBMATH) > libmath_sections.txt
	@echo ""
	@echo "=========================================="
	@echo "=== dyntest.elf Relocation Info ==="
	@echo "=========================================="
	$(OD) -r $(DYNTEST) > dyntest_reloc.txt
	$(OD) -t $(DYNTEST) > dyntest_symbols.txt
	$(OD) -h $(DYNTEST) > dyntest_sections.txt
	@echo ""
	@echo "=== Relocation types in .o files (before linking) ==="
	$(OD) -r libmath.o > libmath_o_reloc.txt
	$(OD) -r dyntest.o > dyntest_o_reloc.txt
	@echo "See *_reloc.txt and *_o_reloc.txt for relocation types"

# 生成反汇编
disasm: $(LIBMATH) $(DYNTEST)
	$(OD) -D $(LIBMATH) > libmath.dis
	$(OD) -D $(DYNTEST) > dyntest.dis

# 静态链接版本（用于对比和实际运行）
static: libmath_static.o dyntest_static.o $(USER_OBJS)
	$(LD) -EL -static -N -T ../scse0_3.lds -G0 -o dyntest_static.elf \
		dyntest_static.o libmath_static.o $(USER_OBJS)
	$(OC) --remove-section .MIPS.abiflags --remove-section .reginfo dyntest_static.elf
	$(OD) -D dyntest_static.elf > dyntest_static.dis

libmath_static.o: libmath.c
	$(CC) $(CFLAGS) -c -o $@ $<

dyntest_static.o: dyntest.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# 清理
clean:
	rm -f *.o *.so *.elf *.txt *.dis

.PHONY: all user_lib analyze disasm static clean
