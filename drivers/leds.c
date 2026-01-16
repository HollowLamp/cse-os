#include "leds.h"

#include <mips/cpu.h>
#include <mfp_io.h>
static u32 led_status[3];

void led_init(){
    led_status[0] = -1;
    led_status[1] = -1;
    led_status[2] = -1;
    set_leds(0);
}
void set_leds(u32 v) {
    mips_put_word(LEDS_ADDR, v);
}

bool rt_leds_write(char * v) {
    mips_put_word( LEDS_ADDR, *((u32*)v) );
}

bool rt_leds_read (char * v){
    *((u32*)v)=mips_get_word(LEDS_ADDR, NULL);
}

/**
 * 写入单个字节到指定的LED组（控制8个LED）
 * v 指向要写入的字节值的指针（8个LED的状态）
 * i LED组编号（0=LED[0:7], 1=LED[8:15], 2=LED[16:23]）
 *
 * 4个LED分为3组，每组8个通过位操作只修改指定组的LED，不影响其他LED
 */
bool rt_leds_write_byte(char *v,u32 i){
    if (i >= 3) {  // 只有3组LED（0,1,2）
        return false;
    }

    // 读取当前LED状态
    u32 current = mips_get_word(LEDS_ADDR, NULL);

    // 构造掩码：清除目标字节位
    u32 mask = 0xFF << (i * 8);
    current = (current & ~mask);  // 清除目标8位

    // 设置新值
    current |= ((*v) << (i * 8));

    // 写回硬件
    mips_put_word(LEDS_ADDR, current);

    return true;
}

/**
 * 通过资源编号写入LED（需要先申请资源）
 * buf 指向要写入的字节值的指针（8个LED的状态）
 * num 资源编号（0-2），对应3组LED资源
 *
 * 只能写入已申请的LED资源，每个资源对应8个LED，使用前需要调用 rt_leds_require() 申请资源
 */
bool rt_leds_write_by_num(char* buf,u32 num)
{
    if (num >= 3) {
        return false;
    }

    // 检查资源是否已被申请
    if (led_status[num] == -1) {
        // 资源未被申请，不允许写入
        return false;
    }

    // 写入对应的8个LED
    return rt_leds_write_byte(buf, num);
}

/**
 * 申请LED资源（互斥访问控制）
 * num 请求的资源数量（1-3），每个资源控制8个LED
 *
 * 24个LED分为3组资源（第0组、第1组、第2组），从第0组开始依次分配num个资源。
 * 支持多任务独立控制，资源被占用时，其他任务无法申请，使用完毕后需调用 rt_leds_release() 释放
 */
bool rt_leds_require(u32 num)
{
    if (num == 0 || num > 3) {
        return false;
    }

    // 检查是否有足够的连续空闲资源（从第0组开始）
    for (u32 i = 0; i < num; i++) {
        if (led_status[i] != -1) {
            // 资源已被占用，无法满足请求
            return false;
        }
    }

    // 标记前num个资源为已占用（0表示已被当前任务占用）
    for (u32 i = 0; i < num; i++) {
        led_status[i] = 0;
    }

    return true;
}

/**
 * 释放LED资源
 * num 释放的资源数量（1-3）
 *
 * 从第0组开始依次释放num个LED资源，供其他任务使用，释放时会清除对应LED的显示
 */
bool rt_leds_release(u32 num)
{
    if (num == 0 || num > 3) {
        return false;
    }

    // 释放前num个资源
    for (u32 i = 0; i < num; i++) {
        // 只释放已被占用的资源
        if (led_status[i] != -1) {
            // 清除对应的8个LED
            char zero = 0x00;
            rt_leds_write_byte(&zero, i);

            // 标记资源为未占用
            led_status[i] = -1;
        }
    }

    return true;
}
