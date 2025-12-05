#include "buzzer.h"

#include <mips/cpu.h>
#include <mfp_io.h>

void set_buzzers(u32 v)
{
    mips_put_word(BUZZER_ADDR,v);
}
void delay_zero()
{
    volatile unsigned int j = 0;
    for (; j < 2500000; j++);

}
/**
 * 播放Do音符（262Hz）
 *
 * 向蜂鸣器写入Do音的频率值（0x00000106）
 * 延时控制音符持续时长
 * 结束后停止发声
 */
void delay_do()
{
    set_buzzers(0x00000106);  // Do: 262Hz

    volatile unsigned int j = 0;
    for (; j < 2500000; j++);  // 延时控制音符时长

    set_buzzers(0);  // 停止发声
}
/**
 * 播放Re音符（294Hz）
 */
void delay_re()
{
    set_buzzers(0x00000126);  // Re: 294Hz

    volatile unsigned int j = 0;
    for (; j < 2500000; j++);

    set_buzzers(0);
}
/**
 * 播放Mi音符（330Hz）
 */
void delay_mi()
{
    set_buzzers(0x0000014A);  // Mi: 330Hz

    volatile unsigned int j = 0;
    for (; j < 2500000; j++);

    set_buzzers(0);
}
/**
 * 播放长Mi音符（330Hz，时长加倍）
 */
void delay_long_mi()
{
    set_buzzers(0x0000014A);  // Mi: 330Hz

    volatile unsigned int j = 0;
    for (; j < 5000000; j++);  // 延时加倍，实现长音符

    set_buzzers(0);
}
/**
 * 播放Fa音符（349Hz）
 */
void delay_fa()
{
    set_buzzers(0x0000015D);  // Fa: 349Hz

    volatile unsigned int j = 0;
    for (; j < 2500000; j++);

    set_buzzers(0);
}
/**
 * 播放So音符（392Hz）
 */
void delay_so()
{
    set_buzzers(0x00000188);  // So: 392Hz

    volatile unsigned int j = 0;
    for (; j < 2500000; j++);

    set_buzzers(0);
}
/**
 * 播放La音符（440Hz）
 */
void delay_la()
{
    set_buzzers(0x000001B8);  // La: 440Hz

    volatile unsigned int j = 0;
    for (; j < 2500000; j++);

    set_buzzers(0);
}
/**
 *播放Xi音符（494Hz）
 */
void delay_xi()
{
    set_buzzers(0x000001EE);  // Xi: 494Hz

    volatile unsigned int j = 0;
    for (; j < 2500000; j++);

    set_buzzers(0);
}

//0
//1 262 0x00000106
//2 294 0x00000126
//3 330 0x0000014A
//4 349 0x0000015D
//5 392 0x00000188
//6 440 0x000001B8
//7 494 0x000001EE

void start_ringtone()
{
    //SONG OF JOY
    delay_xi();
    delay_zero();
    delay_mi();
    delay_mi();
    delay_fa();
    delay_so();
    delay_zero();
    delay_so();
    delay_fa();
    delay_mi();
    delay_re();
    delay_zero();
    delay_do();
    delay_do();
    delay_re();
    delay_mi();
    delay_zero();
    delay_long_mi();
    delay_re();
    delay_re();
}

void boot_music()
{
    //1233 5661 1321 123
    delay_do();
    delay_mi();
    delay_re();
    delay_mi();
    delay_mi();
    delay_so();
    delay_la();
    delay_la();
    delay_do();
    delay_zero();
    delay_do();
    delay_mi();
    delay_re();
    delay_do();
    delay_do();
    delay_re();
    delay_mi();
}
