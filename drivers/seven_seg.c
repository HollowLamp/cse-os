#include "seven_seg.h"

#include <mips/cpu.h>
#include <mfp_io.h>
static u32 seg_status[4];
u32 get_seven_seg_enable() { return mips_get_word(SEVEN_SEG_EN_ADDR, NULL); }
void set_seven_seg_enable(u32 v) { mips_put_word(SEVEN_SEG_EN_ADDR, v); }
u32 get_seven_seg_value() { return mips_get_word(SEVEN_SEG_ADDR, NULL); }
void set_seven_seg_value(u32 v) { mips_put_word(SEVEN_SEG_ADDR, v); }

bool rt_segs_write(char * v){
    mips_put_word(SEVEN_SEG_ADDR, *((u32*)v));
}

bool rt_segs_read (char * v){
  *((u32*)v)=mips_get_word(SEVEN_SEG_ADDR, NULL);
 }

void init_seven_seg() {
    disable_all_seven_seg();
    clean_seven_seg_value();
    for (int i = 0; i < 4 ; i++)
        seg_status[i] = -1;
}

void enable_one_seven_seg(u32 pos) {
    if (pos < 8)
        set_seven_seg_enable(get_seven_seg_enable() | 1 << pos);
}

void enable_all_seven_seg() { set_seven_seg_enable(0xff); }

void disable_one_seven_seg(u32 pos) {
    if (pos < 8)
        set_seven_seg_enable(get_seven_seg_enable() & ~(1 << pos));
}

void disable_all_seven_seg() { set_seven_seg_enable(0); }

void set_seven_seg_digit(u32 val, u32 pos) {
    if (val < 16 && pos < 8)
        set_seven_seg_value(get_seven_seg_enable() & ~(0xf << pos * 4) | val << pos * 4);
}

/**
 * 写入单个半字节到指定的数码管（控制1个数码管显示）
 * v 指向要显示的数值的指针（0-15，对应0-9和A-F）
 * i 数码管编号（0-7）
 *
 * 8个数码管，每个显示一位十六进制数（0-F），每个数码管占用4位（半字节），自动使能对应的数码管
 */
bool rt_segs_write_byte(char *v,u32 i){
    if (i >= 8) {  // 只有8个数码管（0-7）
        return false;
    }

    u32 value = (*v) & 0x0F;  // 只取低4位（0-15）

    // 读取当前显示值
    u32 current = get_seven_seg_value();

    // 构造掩码：清除目标半字节位
    u32 mask = 0xF << (i * 4);
    current = (current & ~mask);  // 清除目标4位

    // 设置新值
    current |= (value << (i * 4));

    // 写回硬件
    set_seven_seg_value(current);

    // 使能对应的数码管
    enable_one_seven_seg(i);

    return true;
}

void clean_seven_seg_value() { set_seven_seg_value(0); }
/**
 * 通过资源编号写入数码管（需要先申请资源）
 * @param buf 指向要显示的数值的指针（16位，控制2个数码管）
 * num 资源编号（0-3），每个资源控制2个数码管
 *
 * 8个数码管分为4组资源，每组2个数码管，num=0: 数码管[0:1], num=1: 数码管[2:3]，num=2: 数码管[4:5], num=3: 数码管[6:7]，buf的低8位写入第一个数码管，高8位写入第二个数码管
 */
bool rt_seven_seg_write_by_num(char* buf,u32 num){
    if (num >= 4) {
        return false;
    }

    // 检查资源是否已被申请
    if (seg_status[num] == -1) {
        // 资源未被申请，不允许写入
        return false;
    }

    // 计算对应的数码管编号
    u32 seg_base = num * 2;  // 每个资源控制2个数码管

    // 写入第一个数码管（低4位）
    char val0 = buf[0] & 0x0F;
    rt_segs_write_byte(&val0, seg_base);

    // 写入第二个数码管（高4位）
    char val1 = (buf[0] >> 4) & 0x0F;
    rt_segs_write_byte(&val1, seg_base + 1);

    return true;
}

/**
 * 申请数码管资源（互斥访问控制）
 * num 请求的资源数量（1-4），每个资源控制2个数码管
 *
 * 8个数码管分为4组资源（第0组、第1组、第2组、第3组），从第0组开始依次分配num个资源。
 * 支持多任务独立控制，资源被占用时，其他任务无法申请，使用完毕后需调用 rt_seven_seg_release() 释放
 */
bool rt_seven_seg_require(u32 num){
    if (num == 0 || num > 4) {
        return false;
    }

    // 检查是否有足够的连续空闲资源（从第0组开始）
    for (u32 i = 0; i < num; i++) {
        if (seg_status[i] != -1) {
            // 资源已被占用，无法满足请求
            return false;
        }
    }

    // 标记前num个资源为已占用，并使能对应的数码管
    for (u32 i = 0; i < num; i++) {
        seg_status[i] = 0;

        // 使能对应的2个数码管
        u32 seg_base = i * 2;
        enable_one_seven_seg(seg_base);
        enable_one_seven_seg(seg_base + 1);
    }

    return true;
}
/**
 * 释放数码管资源
 * num 释放的资源数量（1-4）
 *
 * 从第0组开始依次释放num个数码管资源，供其他任务使用，释放时会禁用并清除对应数码管的显示
 */
bool rt_seven_seg_release(u32 num){
    if (num == 0 || num > 4) {
        return false;
    }

    // 释放前num个资源
    for (u32 i = 0; i < num; i++) {
        // 只释放已被占用的资源
        if (seg_status[i] != -1) {
            // 计算对应的数码管编号
            u32 seg_base = i * 2;

            // 清除对应的2个数码管显示值
            u32 current = get_seven_seg_value();
            u32 mask = 0xFF << (seg_base * 4);  // 2个数码管占8位
            current = current & ~mask;
            set_seven_seg_value(current);

            // 禁用对应的2个数码管
            disable_one_seven_seg(seg_base);
            disable_one_seven_seg(seg_base + 1);

            // 标记资源为未占用
            seg_status[i] = -1;
        }
    }

    return true;
}