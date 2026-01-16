/**
 * libmath.c - 简单的共享库测试
 * 用于测试动态链接功能
 */

/* 全局变量 - 测试数据重定位 */
int lib_counter = 100;

/**
 * 加法函数
 */
int add(int a, int b) {
    lib_counter++;
    return a + b;
}

/**
 * 乘法函数
 */
int mul(int a, int b) {
    lib_counter++;
    return a * b;
}

/**
 * 获取计数器值 - 测试全局变量访问
 */
int get_counter(void) {
    return lib_counter;
}
