#include "console.h"

#include "uart.h"

#include "vga_print.h"

static void cons_intr(int (*proc)(void));
static void cons_putc(int c);

/***** Serial I/O code *****/

static int serial_proc_data(void) {
    if (!get_UART_DR(get_UART_LSR())) // get when data is ready
        return -1;
    return get_UART_RBR();
}

void serial_intr(void) {
    cons_intr(serial_proc_data);
}

static void serial_putc(int c) {
    while (!get_UART_TEMT(get_UART_LSR()));
    set_UART_THR(c);
}

static void serial_init(void) {
    init_uart();
}

/***** General device-independent console code *****/
// Here we manage the console input buffer,
// where we stash characters received from the keyboard or serial port
// whenever the corresponding interrupt occurs.

#define CONSBUFSIZE 512

static struct {
    u8 buf[CONSBUFSIZE]; // buffer
    u32 rpos;            // read position
    u32 wpos;            // write position
} cons;

/**
 * 中断处理函数：将串口接收到的字符存入循环缓冲区
 * proc 函数指针，用于读取硬件数据（如 serial_proc_data）
 *
 * 工作流程：
 * 1. 循环调用 proc() 读取硬件数据
 * 2. 如果有数据，存入循环缓冲区 cons.buf
 * 3. 更新写位置 wpos
 * 4. 如果缓冲区满，丢弃新数据
 */
static void cons_intr(int (*proc)(void)) {
    int c;

    // 循环读取所有可用的字符
    while ((c = (*proc)()) != -1) {
        // 检查缓冲区是否还有空间
        if (cons.wpos - cons.rpos < CONSBUFSIZE) {
            // 存入循环缓冲区（使用取模实现循环）
            cons.buf[cons.wpos % CONSBUFSIZE] = c;
            cons.wpos++;
        }
        // 如果缓冲区满了，丢弃字符
    }
}

/**
 * 从控制台缓冲区读取一个字符
 * 返回读取到的字符，如果没有字符则返回 0
 *
 * 工作流程：
 * 1. 轮询检查串口是否有新数据（即使中断被禁用也能工作）
 * 2. 检查缓冲区是否有未读数据
 * 3. 如果有，从缓冲区读取并更新读位置
 * 4. 如果没有，返回 0
 */
int cons_getc(void) {
	int c;

	// 轮询检查是否有新的输入字符
	// 这样即使在中断被禁用时（如内核调试器中）也能正常工作
	serial_intr();

	// 从输入缓冲区读取下一个字符
	if (cons.rpos < cons.wpos) {
		// 缓冲区中有未读数据
		c = cons.buf[cons.rpos % CONSBUFSIZE];
		cons.rpos++;
		return c;
	}

	// 缓冲区为空，没有字符可读
	return 0;
}

/**
 * 输出一个字符到控制台（串口）
 * c 要输出的字符
 *
 * 功能：
 * 1. 处理特殊字符（换行、退格等）
 * 2. 将字符输出到串口
 * 3. 忽略 VGA 输出（硬件不支持）
 */
static void cons_putc(int c) {
	// 处理换行符：串口终端需要 \r\n 才能正确换行
	if (c == '\n') {
		serial_putc('\r');  // 回车（Carriage Return）
		serial_putc('\n');  // 换行（Line Feed）
	}
	// 处理退格符：输出 退格+空格+退格 来清除字符
	else if (c == '\b') {
		serial_putc('\b');  // 光标后退
		serial_putc(' ');   // 用空格覆盖
		serial_putc('\b');  // 光标再次后退
	}
	// 普通字符直接输出
	else {
		serial_putc(c);
	}
}

// initialize the console devices
void cons_init(void) {
	serial_init();
	vga_print_init();
}

// `High'-level console I/O.  Used by readline and cprintf.

void cputchar(int c) {
	cons_putc(c);
}

int getchar(void) {
	int c;

	while ((c = cons_getc()) == 0)
		/* do nothing */;
	return c;
}

int iscons(int fdnum) {
	// used by readline
	return 1;
}