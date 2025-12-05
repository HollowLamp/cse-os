#include <inc/printf.h>
#include <inc/print.h>
#include <drivers/console.h>

/**
 * 字符串输出函数（供 lp_Print 调用）
 * arg 额外参数（通常不使用）
 * s 要输出的字符串
 * l 字符串长度
 *
 * 功能：将字符串逐个字符输出到控制台
 */
void myoutput(void *arg, char *s, int l)
{
	int i;

	// 遍历字符串，逐个输出
	for (i = 0; i < l; i++) {
		cputchar(s[i]);
	}

	// 注意：lp_Print 会在最后调用一次 myoutput(arg, "\0", 1)
	// 作为特殊的结束标志，这里不需要特殊处理
}

/**
 * 格式化输出函数（类似标准库的 printf）
 * fmt 格式化字符串
 * ... 可变参数列表
 *
 * 支持的格式：
 * %d - 十进制整数
 * %x, %X - 十六进制整数
 * %o - 八进制整数
 * %u - 无符号整数
 * %c - 字符
 * %s - 字符串
 * %b - 二进制整数
 */
void printf(char *fmt, ...)
{
	va_list ap;  // 可变参数列表

	// 初始化可变参数列表，从 fmt 之后开始
	va_start(ap, fmt);

	// 调用底层格式化函数
	// myoutput: 输出函数
	// 0: 传递给 myoutput 的额外参数（这里不需要）
	// fmt: 格式化字符串
	// ap: 可变参数列表
	lp_Print(myoutput, 0, fmt, ap);

	// 清理可变参数列表
	va_end(ap);
}

/**
 * 内核 panic 函数（发生严重错误时调用）
 * file 发生错误的文件名
 * line 发生错误的行号
 * fmt 错误信息格式化字符串
 * ... 可变参数
 *
 * 功能：输出错误信息并进入死循环
 */
void _panic(const char *file, int line, const char *fmt, ...)
{
	va_list ap;

	// 输出错误位置信息
	printf("panic at %s:%d: ", file, line);

	// 输出错误详细信息
	va_start(ap, fmt);
	lp_Print(myoutput, 0, (char *)fmt, ap);
	va_end(ap);

	printf("\n");

	// 进入死循环，停止系统运行
	for (;;)
		;
}
