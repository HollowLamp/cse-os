/**
 * hwtest.c - 硬件基本测试程序
 * Hardware Test Program for Aurora OS
 */

#include "../user/lib.h"

/* 蜂鸣器音符频率值 */
#define NOTE_STOP   0x0000
#define NOTE_DO     0x0106
#define NOTE_RE     0x0126
#define NOTE_MI     0x014A
#define NOTE_FA     0x015D
#define NOTE_SO     0x0188
#define NOTE_LA     0x01B8
#define NOTE_XI     0x01EE

/* 设备 ID */
#define LED_ID      0
#define SEG_ID      1

/* 简单延时 */
static void delay(int count) {
    volatile int i;
    for (i = 0; i < count * 100000; i++);
}

/* 计算开关中有多少位为 1 */
static int count_bits(u32 val) {
    int count = 0;
    while (val) {
        count += val & 1;
        val >>= 1;
    }
    return count;
}

/**
 * 测试 1: LED 流水灯
 */
void test_led(void) {
    syscall_printf("[Test 1] LED...\n");

    int i;
    /* 流水灯 */
    for (i = 0; i < 24; i++) {
        syscall_set_leds(1 << i);
        delay(2);
    }
    /* 全亮后熄灭 */
    syscall_set_leds(0xFFFFFF);
    delay(5);
    syscall_set_leds(0);

    syscall_printf("LED OK\n");
}

/**
 * 测试 2: 七段数码管
 */
void test_seven_seg(void) {
    syscall_printf("[Test 2] Seven Segment...\n");

    /* 申请数码管资源 */
    if (!syscall_rt_require_device(SEG_ID, 4)) {
        syscall_printf("SEG require failed!\n");
        return;
    }

    /* 计数显示 0-F */
    int i;
    for (i = 0; i <= 0xF; i++) {
        char val = (char)i;
        syscall_rt_write_by_num(SEG_ID, 0, &val);
        syscall_rt_write_by_num(SEG_ID, 1, &val);
        syscall_rt_write_by_num(SEG_ID, 2, &val);
        syscall_rt_write_by_num(SEG_ID, 3, &val);
        delay(3);
    }

    /* 释放资源 */
    syscall_rt_release_device(SEG_ID, 4);

    syscall_printf("Seven Segment OK\n");
}

/**
 * 测试 3: 开关读取
 * 拨动 4 个及以上开关后自动进入下一测试
 */
void test_switch(void) {
    syscall_printf("[Test 3] Switch (toggle 4+ to continue)...\n");

    u32 sw;
    int bits;

    /* 等待用户拨动至少 4 个开关 */
    while (1) {
        sw = syscall_get_switchs();
        syscall_set_leds(sw);  /* 实时显示开关状态 */
        bits = count_bits(sw);

        if (bits >= 4) {
            delay(5);  /* 稳定一下 */
            break;
        }
        delay(1);
    }

    /* 清除显示 */
    syscall_set_leds(0);

    syscall_printf("Switch OK (detected %d switches)\n", bits);
}

/**
 * 测试 4: 蜂鸣器
 */
void test_buzzer(void) {
    syscall_printf("[Test 4] Buzzer...\n");

    /* 播放音阶 */
    u32 notes[] = {NOTE_DO, NOTE_RE, NOTE_MI, NOTE_FA, NOTE_SO, NOTE_LA, NOTE_XI};
    int i;
    for (i = 0; i < 7; i++) {
        syscall_set_buzzer(notes[i]);
        delay(3);
    }
    syscall_set_buzzer(NOTE_STOP);

    syscall_printf("Buzzer OK\n");
}

/**
 * 主函数
 */
int main(void) {
    syscall_printf("\n=== Hardware Test ===\n");

    test_led();
    test_seven_seg();
    test_switch();
    test_buzzer();

    syscall_printf("=== All Tests Done ===\n");

    return 0;
}
