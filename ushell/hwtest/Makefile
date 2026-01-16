# Makefile for Hardware Test Program
# 硬件测试程序编译脚本

include ../include.mk

# 头文件路径
INCLUDES = -I../inc/ -I../user/

# 用户库对象文件
USER_OBJS = ../user/syscall_lib.o ../user/syscall_wrap.o ../user/string.o

# 目标文件
TARGET = hwtest.elf

# 默认目标
all: user_lib $(TARGET)
	@echo "Output: $(TARGET)"

# 编译用户库（确保依赖库是最新的）
user_lib:
	$(MAKE) -C ../user

# 编译主程序
hwtest.o: hwtest.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# 链接生成可执行文件
$(TARGET): hwtest.o $(USER_OBJS)
	$(LD) -EL -static -N -T ../scse0_3.lds -G0 -o $@ hwtest.o $(USER_OBJS)
	$(OC) --remove-section .MIPS.abiflags --remove-section .reginfo $@
	@echo "Generated: $@"

# 生成反汇编（用于调试）
disasm: $(TARGET)
	$(OD) -D $(TARGET) > hwtest.dis
	@echo "Disassembly generated: hwtest.dis"

# 生成符号表
symbols: $(TARGET)
	$(OD) -t $(TARGET) > hwtest_symbols.txt
	@echo "Symbol table generated: hwtest_symbols.txt"

# 安装到 elf 目录
install: $(TARGET)
	cp $(TARGET) ../elf/
	@echo "Installed $(TARGET) to ../elf/"

# 清理
clean:
	rm -f *.o *.elf *.dis *.txt

# 完整构建（编译 + 安装）
build: all install

.PHONY: all user_lib disasm symbols install clean build
