#include <inc/rtThread.h>
#define DEVICE_NUM 10

static struct rt_device device_list[DEVICE_NUM];
static u32 nres = 0; // 资源数
#define NUMBER_OF_CUSTOMERS 2
#define NUMBER_OF_RESOURCES 2                                    // 使用两个
static int all_devices[NUMBER_OF_RESOURCES];                     // 设备数量数组
static int available[NUMBER_OF_RESOURCES];                       // 剩余可用设备数量
static int maximum[NUMBER_OF_CUSTOMERS][NUMBER_OF_RESOURCES];    // 进程要求的最大数量
static int allocation[NUMBER_OF_CUSTOMERS][NUMBER_OF_RESOURCES]; // 已经分配给进程的数量
static int need[NUMBER_OF_CUSTOMERS][NUMBER_OF_RESOURCES];       // 进程还需要的数量
static int status[NUMBER_OF_CUSTOMERS];                          // 进程是否完成 1 为完成
static int asid_list[NUMBER_OF_CUSTOMERS];                       // asid to index mapping

/**
 * @brief 初始化 ASID 映射表，将所有项设为 -1（表示未分配）
 */
void asid_list_init()
{
    u32 i;
    for (i = 0; i < NUMBER_OF_CUSTOMERS; i++)
        asid_list[i] = -1; // -1 表示该槽位空闲
}

/**
 * @brief 获取当前任务在客户数组中的索引
 * @return 客户索引（0 或 1），若无法分配则返回 -1
 *
 * 逻辑：
 * 1. 获取当前任务的 ASID（低 8 位）
 * 2. 查找 asid_list 中是否已有该 ASID
 *    - 若有，返回对应索引
 *    - 若无，寻找第一个空闲槽位（值为 -1）注册该 ASID 并返回索引
 * 3. 若无空闲槽位（超过 NUMBER_OF_CUSTOMERS），返回 -1
 */
int getAsidIndex()
{
    int temp = 0xff & get_asid(); // 只取 ASID 的低 8 位作为标识

    int i = 0;
    // 先查找是否已存在
    for (i = 0; i < NUMBER_OF_CUSTOMERS; i++)
    {
        if (asid_list[i] == temp)
            return i;
    }

    // 未找到，则尝试注册新客户
    for (i = 0; i < NUMBER_OF_CUSTOMERS; i++)
    {
        if (asid_list[i] == -1)
        {
            asid_list[i] = temp; // 注册 ASID
            return i;            // 返回新分配的客户索引
        }
    }

    return -1; // 超过最大客户数，拒绝
}

/**
 * @brief 请求分配指定数量的设备资源（使用银行家算法进行安全检查）
 * @param device_id 资源类型 ID（必须 < NUMBER_OF_RESOURCES）
 * @param request_num 请求的数量
 * @return true 表示分配成功，false 表示被拒绝（不安全或无效请求）
 *
 * 实现银行家算法的核心函数：
 * 1. 检查请求是否合法（不超过 need 且不超过 available）
 * 2. 模拟分配
 * 3. 使用安全性算法检查系统是否仍处于安全状态
 * 4. 若安全，则真正分配；否则拒绝
 */
bool rt_require_device(u32 device_id, u32 request_num)
{
    // 检查设备是否已注册且支持资源申请
    if (device_list[device_id].rt_require_device == NULL)
    {
        // printf("return from rt_require");
        return false;
    }

    // 获取当前任务对应的客户索引
    int customer_num = getAsidIndex();
    if (customer_num == -1)
        return false;

    // 用于模拟分配的安全性检查变量
    int result = 1;                                                        // 默认认为安全（1 表示可分配）
    int status_tmp[NUMBER_OF_CUSTOMERS];                                   // 临时状态
    int available_after_assign[NUMBER_OF_RESOURCES];                       // 模拟分配后的可用资源
    int tmp[NUMBER_OF_CUSTOMERS][NUMBER_OF_RESOURCES];                     // 模拟分配后的 need
    int allocation_after_assign[NUMBER_OF_CUSTOMERS][NUMBER_OF_RESOURCES]; // 模拟分配后的 allocation

    // 复制当前系统状态（快照）
    for (u32 d = 0; d < NUMBER_OF_CUSTOMERS; d++)
        status_tmp[d] = status[d];

    for (u32 n = 0; n < NUMBER_OF_RESOURCES; n++)
    {
        printf("available before:%d    ", available[n]);
        available_after_assign[n] = available[n];
        printf("\n");
    }

    for (u32 c = 0; c < NUMBER_OF_CUSTOMERS; c++)
    {
        for (u32 d = 0; d < NUMBER_OF_RESOURCES; d++)
        {
            tmp[c][d] = need[c][d];
            allocation_after_assign[c][d] = allocation[c][d];
        }
        printf("\n");
    }

    // 初步合法性检查：请求不能超过 need 且不能超过 available
    bool sum = true;
    if (!(request_num <= tmp[customer_num][device_id] &&
          request_num <= available_after_assign[device_id]))
    {
        sum = false;
    }

    // 如果初步检查通过，进行模拟分配
    if (sum)
    {
        available_after_assign[device_id] -= request_num;
        allocation_after_assign[customer_num][device_id] += request_num;
        tmp[customer_num][device_id] -= request_num;
    }

    // 打印模拟后的可用资源（调试用）
    for (u32 n = 0; n < NUMBER_OF_RESOURCES; n++)
        printf("%d    ", available_after_assign[n]);
    printf("\n");

    // ========== 银行家算法：安全性检查 ==========
    u32 ptr = 0;
    while (1)
    {
        int flag = 0;  // 标记本轮是否找到可完成的客户
        int count = 0; // 已完成的客户数

        // 遍历所有客户
        for (ptr = 0; ptr < NUMBER_OF_CUSTOMERS; ptr++)
        {
            int mark = 1; // 假设该客户可以完成

            // 跳过已完成的客户
            if (status_tmp[ptr] != 1)
            {
                // 检查该客户的所有资源需求是否 <= 当前可用资源
                for (u32 n = 0; n < NUMBER_OF_RESOURCES; n++)
                {
                    printf("%d available after assign:%d, tmp:%d \n",
                           n, available_after_assign[n], tmp[ptr][n]);
                    if (tmp[ptr][n] > available_after_assign[n])
                    {
                        mark = 0; // 无法满足，不能完成
                        break;
                    }
                }
                if (mark == 1)
                {
                    flag = 1; // 找到一个可完成的客户
                    break;
                }
            }
            else
            {
                count++; // 已完成客户计数
            }
        }

        // 所有客户都已完成 → 安全
        if (count >= NUMBER_OF_CUSTOMERS)
            goto assign;

        // 找到可完成客户，释放其资源
        if (flag == 1)
        {
            for (u32 n = 0; n < NUMBER_OF_RESOURCES; n++)
                available_after_assign[n] += allocation_after_assign[ptr][n];
            status_tmp[ptr] = 1; // 标记为已完成
        }
        else
        {
            // 无法找到可完成客户 → 不安全
            result = 0;
            goto exit;
        }
    }

// ========== 安全，执行真实分配 ==========
assign:
    need[customer_num][device_id] -= request_num;
    allocation[customer_num][device_id] += request_num;
    available[device_id] -= request_num;

// ========== 退出点（无论分配与否都要执行） ==========
exit:
    // 调试输出
    for (u32 n = 0; n < NUMBER_OF_RESOURCES; n++)
        printf("%d ", request_num);
    printf("from %d ", customer_num);

    if (result)
    {
        printf("fullfilled\n");
        // 调用底层设备驱动的分配函数（如增加引用计数等）
        device_list[device_id].rt_require_device(request_num);
    }
    else
    {
        printf("denied\n");
    }

    return result;
}

/**
 * @brief 释放指定数量的设备资源
 * @param device_id 资源类型 ID
 * @param request_num 释放的数量
 * @return 是否成功
 *
 * 注意：此函数不进行安全检查，直接释放并调用驱动回调。
 *       因为释放总是安全的操作。
 */
bool rt_release_device(u32 device_id, u32 request_num)
{
    if (device_list[device_id].rt_release_device == NULL)
        return false;
    // 调用设备驱动的释放函数（如减少引用计数）
    device_list[device_id].rt_release_device(request_num);
    // 同时更新全局可用资源（注意：原代码缺失！应补充如下）
    if (device_id < NUMBER_OF_RESOURCES)
    {
        available[device_id] += request_num;
    }
    return true;
}

/**
 * @brief 初始化一个设备（注册到设备列表）
 * @param device_id 设备 ID（0 ~ DEVICE_NUM-1）
 * @param ... 一系列设备操作回调函数指针
 * @param num 该设备的资源总量（仅对前 NUMBER_OF_RESOURCES 个设备生效）
 * @return 是否成功
 *
 * 说明：只有 device_id < NUMBER_OF_RESOURCES 的设备才参与资源管理（银行家算法）
 */
bool rt_device_init(
    u32 device_id,
    bool (*rt_device_read)(char *),
    bool (*rt_device_write)(char *),
    bool (*rt_device_write_byte)(char *, u32),
    u32 num,
    bool (*rt_device_write_by_num)(char *, u32),
    bool (*rt_device_read_by_num)(char *, u32),
    bool (*rt_require_device)(u32),
    bool (*rt_release_device)(u32))
{
    // 注册设备操作函数
    device_list[device_id].rt_device_read = rt_device_read;
    device_list[device_id].rt_device_write = rt_device_write;
    device_list[device_id].rt_device_write_byte = rt_device_write_byte;
    device_list[device_id].device_num = num;
    device_list[device_id].rt_device_write_by_num = rt_device_write_by_num;
    device_list[device_id].rt_device_read_by_num = rt_device_read_by_num;
    device_list[device_id].rt_release_device = rt_release_device;
    device_list[device_id].rt_require_device = rt_require_device;

    // 如果是受管理的资源类型（前 NUMBER_OF_RESOURCES 个）
    if (device_id < NUMBER_OF_RESOURCES)
    {
        available[device_id] = num;   // 初始化可用数量
        all_devices[device_id] = num; // 记录总量
        printf("%d device has %d items\n", device_id, num);
    }

    nres++; // 增加已注册设备计数
    return true;
}

// 以下为设备 I/O 操作的封装函数（直接调用设备驱动回调）

bool rt_device_write(u32 device_id, char *buf)
{
    if (device_list[device_id].rt_device_write == NULL)
        return false;
    return device_list[device_id].rt_device_write(buf);
}

bool rt_device_write_byte(u32 device_id, char *buf, u32 i)
{
    if (device_list[device_id].rt_device_write == NULL)
        return false;
    return device_list[device_id].rt_device_write_byte(buf, i);
}

bool rt_device_read(u32 device_id, char *buf)
{
    if (device_list[device_id].rt_device_read == NULL)
        return false;
    return device_list[device_id].rt_device_read(buf);
}

/**
 * @brief 声明当前任务对各类资源的最大需求量
 * @param require 指向长度为 NUMBER_OF_RESOURCES 的数组
 *
 * 调用时机：任务启动初期，声明其最大资源需求（用于初始化 need 和 maximum）
 */
bool rt_claim_device(u32 *require)
{
    int index = getAsidIndex();
    printf("%d", index);

    for (int i = 0; i < NUMBER_OF_RESOURCES; i++)
    {
        need[index][i] = require[i];    // 初始化还需数量
        maximum[index][i] = require[i]; // 记录最大需求
        allocation[index][i] = 0;       // 初始分配为 0
    }
    status[index] = 0; // 标记为未完成
    return true;
}

// 按编号读写设备（扩展接口）

bool rt_device_write_by_num(u32 device_id, u32 num, char *buf)
{
    if (device_list[device_id].rt_device_write_by_num == NULL)
        return false;
    return device_list[device_id].rt_device_write_by_num(buf, num);
}

bool rt_device_read_by_num(u32 device_id, u32 num, char *buf)
{
    if (device_list[device_id].rt_device_read_by_num == NULL)
        return false;
    return device_list[device_id].rt_device_read_by_num(buf, num);
}

/**
 * @brief 当前任务退出时调用，释放其 ASID 映射
 * @return true
 *
 * 注意：此函数不会自动释放该任务已占用的资源！
 *       应在任务退出前显式调用 rt_release_device 释放所有资源，
 *       否则会造成资源泄漏。
 */
bool rt_task_exit()
{
    int index = getAsidIndex();
    asid_list[index] = -1; // 释放 ASID 映射槽位
    return true;
}