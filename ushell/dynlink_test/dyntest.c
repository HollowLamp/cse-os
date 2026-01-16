/**
 * dyntest.c - 动态链接测试主程序
 * 调用 libmath.so 中的函数
 */

#include "../user/lib.h"

/* 声明外部函数（来自共享库） */
extern int add(int a, int b);
extern int mul(int a, int b);
extern int get_counter(void);

/* 声明外部变量（来自共享库） */
extern int lib_counter;

/* 本地全局变量 */
static int local_var = 42;

/**
 * 主函数
 */
int main(void) {
    int result1, result2, result3;
    
    syscall_printf("=== Dynamic Linking Test ===\n");
    
    /* 测试函数调用 */
    result1 = add(3, 4);        /* 应该返回 7 */
    syscall_printf("add(3, 4) = %d\n", result1);
    
    result2 = mul(5, 6);        /* 应该返回 30 */
    syscall_printf("mul(5, 6) = %d\n", result2);
    
    result3 = add(result1, result2);  /* 应该返回 37 */
    syscall_printf("add(%d, %d) = %d\n", result1, result2, result3);
    
    /* 测试全局变量访问 */
    int counter = get_counter();
    syscall_printf("get_counter() = %d\n", counter);
    
    /* 测试直接访问外部变量 */
    syscall_printf("lib_counter before = %d\n", lib_counter);
    lib_counter = 200;
    syscall_printf("lib_counter after = %d\n", lib_counter);
    
    /* 本地变量 */
    syscall_printf("local_var = %d\n", local_var);
    
    syscall_printf("=== Test Complete ===\n");
    
    return result3;
}
