#include "RDH.h"

// 内存对齐
#define RDH_MALLOC_SIZE(size) (((size) + 7) & ~7)

// 高低位掩码
#define RDH_IMG_MASK_HIGH 0xF8
#define RDH_IMG_MASK_LOW (~(RDH_IMG_MASK_HIGH))
#define RDH_IMG_MASK_SET(x, mask) ((x) & (mask))

// 获取高位的值
#define RDH_IMG_GET_HIGH(x) (RDH_IMG_MASK_SET((x), RDH_IMG_MASK_HIGH) >> 3)

// 图像数据位置
#define RDH_IMG_POS(img, w, x, y) ((img)[(y) * (w) + (x)])

// 图像哈希表
static uint8_t hash[4 * (RDH_IMG_GET_HIGH(RDH_IMG_MASK_HIGH) + 1)];
#define RDH_HASH_INIT() memset(hash, 0, sizeof(hash))
#define RDH_HASH_GET(x) (hash)[2 * (RDH_IMG_GET_HIGH(RDH_IMG_MASK_HIGH) + 1) + (x)]
#define RDH_HASH_SET(x) (RDH_HASH_GET(x)++)

void rdhShuffleImage(uint8_t *img, int size, uint64_t key)
{
    uint64_t *chunk = (uint64_t *)img;
    size /= sizeof(uint64_t) / sizeof(uint8_t);
    xSrand32(key);
    for (int i = size - 1; i > 0; i--)
    {
        int j = xRand32() % (i + 1);
        uint64_t temp = chunk[i];
        chunk[i] = chunk[j];
        chunk[j] = temp;
    }
}

void rdhUnshuffleImage(uint8_t *img, int size, uint64_t key)
{
    uint64_t *chunk = (uint64_t *)img;
    size /= sizeof(uint64_t) / sizeof(uint8_t);
    xSrand32(key);
    int *indices = (int *)malloc(size * sizeof(int));
    uint64_t *tempData = (uint64_t *)malloc(size * sizeof(uint64_t));

    // 初始化索引数组
    for (int i = 0; i < size; i++)
    {
        indices[i] = i;
    }

    // 使用Fisher-Yates算法打乱索引数组
    for (int i = size - 1; i > 0; i--)
    {
        int j = xRand32() % (i + 1);
        int temp = indices[i];
        indices[i] = indices[j];
        indices[j] = temp;
    }

    // 根据打乱的索引数组重新排列数据
    for (int i = 0; i < size; i++)
    {
        tempData[indices[i]] = chunk[i];
    }

    // 将恢复后的数据复制回原数组
    memcpy(chunk, tempData, size * sizeof(uint64_t));

    free(indices);
    free(tempData);
}

void rdhSplitImage(const uint8_t *img, int size, uint8_t **img1, uint8_t **img2)
{
    *img1 = (uint8_t *)rdhMalloc(size);
    *img2 = (uint8_t *)rdhMalloc(size);

    const uint8_t *t = img;
    uint8_t *t1 = *img1;
    uint8_t *t2 = *img2;

    srand((unsigned int)time(NULL));
    xSrand8(rand());

    // 随机分割
    while (size--)
    {
        uint8_t r = xRand8();
        uint8_t high_masked_t = RDH_IMG_MASK_SET(*t, RDH_IMG_MASK_HIGH);
        uint8_t low_masked_t = RDH_IMG_MASK_SET(*t, RDH_IMG_MASK_LOW);
        uint8_t high_masked_r = RDH_IMG_MASK_SET(r, RDH_IMG_MASK_HIGH);
        uint8_t low_masked_r = RDH_IMG_MASK_SET(r, RDH_IMG_MASK_LOW);

        // 随机分割
        // *t1 = (high_masked_t - (high_masked_t ? (high_masked_r % high_masked_t) : 0)) |
        //       (low_masked_t - (low_masked_t ? (low_masked_r % low_masked_t) : 0));

        // 快速随机分割
        *t1 = (high_masked_t - RDH_IMG_MASK_SET(high_masked_r, high_masked_t)) |
              (low_masked_t - RDH_IMG_MASK_SET(low_masked_r, low_masked_t));

        *t2 = (high_masked_t - RDH_IMG_MASK_SET(*t1, RDH_IMG_MASK_HIGH)) |
              (low_masked_t - RDH_IMG_MASK_SET(*t1, RDH_IMG_MASK_LOW));

        t++;
        t1++;
        t2++;
    }
}
void rdhCombineImage(const uint8_t *img1, const uint8_t *img2, int size, uint8_t **img)
{
    *img = (uint8_t *)rdhMalloc(size);

    uint8_t *t, *t1, *t2;
    t = *img;
    t1 = (uint8_t *)img1;
    t2 = (uint8_t *)img2;

    // 合并
    while (size--)
    {
        *t = *t1 + *t2;

        t++;
        t1++;
        t2++;
    }
}

#define RDH_ME_MASK_COUNT_EP 0xE0
#define RDH_ME_MASK_COUNT_SP 0x0C
#define RDH_ME_MASK_RDU_EP 0x10
#define RDH_ME_MASK_RDU_SP 0x02
#define RDH_ME_MASK_INUSE 0x01
#define RDH_ME_OFFSET_COUNT_EP 5
#define RDH_ME_OFFSET_COUNT_SP 2
#define RDH_ME_OFFSET_RDU_EP 4
#define RDH_ME_OFFSET_RDU_SP 1
#define RDH_ME_OFFSET_INUSE 0

#define RDH_ME_MASK_SET(x, mask) ((x) & (mask))
#define RDH_ME_OFFSET_SET(x, offset) ((x) << (offset))
#define RDH_ME_OFFSET_GET(x, offset) ((x) >> (offset))

#define RDH_ME_GET(me, field) \
    RDH_ME_OFFSET_GET(RDH_ME_MASK_SET((me), RDH_ME_MASK_##field), RDH_ME_OFFSET_##field)

#define RDH_ME_SET(value, field) \
    RDH_ME_MASK_SET(RDH_ME_OFFSET_SET((value), RDH_ME_OFFSET_##field), RDH_ME_MASK_##field)

#define RDH_EP_VALUE_ADD 0x08
#define RDH_EP_VALUE_MAX (0xFF - RDH_EP_VALUE_ADD)
#define RDH_SP_VALUE_ADD 0x08
#define RDH_SP_VALUE_MAX (0xFF - RDH_SP_VALUE_ADD)

#define RDH_DATA_BYTE_2_BIT(byte) ((byte) << 3)
#define RDH_DATA_BIT_2_BYTE(bit) ((bit) >> 3)

#define RDH_DATA_INDEX(size) ((size) >> 3)
#define RDH_DATA_BIT(size) ((size) & 7)
#define RDG_DATA_GET_BYTE(byte, size) (((byte) & (1 << RDH_DATA_BIT(size))) >> RDH_DATA_BIT(size)) // 获取这个字节的size位
#define RDH_DATA_GET(data, now) (RDG_DATA_GET_BYTE(((const uint8_t *)data)[RDH_DATA_INDEX(now)], now))
#define RDH_DATA_SET(data, now, value) (((uint8_t *)data)[RDH_DATA_INDEX(now)] |= ((value) << RDH_DATA_BIT(now)))

#define divide_by_2_floor(num) (((num) - ((num) < 0)) >> 1)
#define divide_by_4_floor(num) (((num) - ((num) < 0 ? 3 : 0)) >> 2)

typedef struct
{
    uint8_t EP[5];
    uint8_t SP[4];
} rdhChunk;
#define RDH_CHUNK_EP(chunk, i) ((chunk).EP[(i) - 1])
#define RDH_CHUNK_SP(chunk, i) ((chunk).SP[(i) - 1])

/**
 * \brief 嵌入数据
 * \param img1Line1 图像1行1
 * \param img1Line2 图像1行2
 * \param img1Line3 图像1行3
 * \param img2Line1 图像2行1
 * \param img2Line2 图像2行2
 * \param img2Line3 图像2行3
 * \param byte 字节流
 * \param total bit位总数
 * \param now 当前bit位
 * \return 嵌入的额外数据
 */
uint8_t rdhEmbedDataByte(uint8_t *img1Line1, uint8_t *img1Line2, uint8_t *img1Line3,
                         uint8_t *img2Line1, uint8_t *img2Line2, uint8_t *img2Line3,
                         const uint8_t *byte, int total, int *now)
{
    uint8_t m = 0;

    // 获取采样像素采样像素 SPs 和可嵌入像素 EPs
    rdhChunk imgChunk1;
    rdhChunk imgChunk2;
    RDH_CHUNK_SP(imgChunk1, 1) = img1Line1[0];
    RDH_CHUNK_EP(imgChunk1, 1) = img1Line1[1];
    RDH_CHUNK_SP(imgChunk1, 2) = img1Line1[2];
    RDH_CHUNK_EP(imgChunk1, 2) = img1Line2[0];
    RDH_CHUNK_EP(imgChunk1, 3) = img1Line2[1];
    RDH_CHUNK_EP(imgChunk1, 4) = img1Line2[2];
    RDH_CHUNK_SP(imgChunk1, 3) = img1Line3[0];
    RDH_CHUNK_EP(imgChunk1, 5) = img1Line3[1];
    RDH_CHUNK_SP(imgChunk1, 4) = img1Line3[2];

    RDH_CHUNK_SP(imgChunk2, 1) = img2Line1[0];
    RDH_CHUNK_EP(imgChunk2, 1) = img2Line1[1];
    RDH_CHUNK_SP(imgChunk2, 2) = img2Line1[2];
    RDH_CHUNK_EP(imgChunk2, 2) = img2Line2[0];
    RDH_CHUNK_EP(imgChunk2, 3) = img2Line2[1];
    RDH_CHUNK_EP(imgChunk2, 4) = img2Line2[2];
    RDH_CHUNK_SP(imgChunk2, 3) = img2Line3[0];
    RDH_CHUNK_EP(imgChunk2, 5) = img2Line3[1];
    RDH_CHUNK_SP(imgChunk2, 4) = img2Line3[2];

    // 检查是否存在溢出
    for (int i = 0; i < 5; i++)
        if (RDH_CHUNK_EP(imgChunk1, i + 1) > RDH_EP_VALUE_MAX)
        {
            return 0;
        }

    {
        // DEBUG("|------img1------|\n");
        // DEBUG("%2d %2d %2d\n", RDH_CHUNK_SP(imgChunk1, 1), RDH_CHUNK_EP(imgChunk1, 1), RDH_CHUNK_SP(imgChunk1, 2));
        // DEBUG("%2d %2d %2d\n", RDH_CHUNK_EP(imgChunk1, 2), RDH_CHUNK_EP(imgChunk1, 3), RDH_CHUNK_EP(imgChunk1, 4));
        // DEBUG("%2d %2d %2d\n\n", RDH_CHUNK_SP(imgChunk1, 3), RDH_CHUNK_EP(imgChunk1, 5), RDH_CHUNK_SP(imgChunk1, 4));
        // DEBUG("|------img2------|\n");
        // DEBUG("%2d %2d %2d\n", RDH_CHUNK_SP(imgChunk2, 1), RDH_CHUNK_EP(imgChunk2, 1), RDH_CHUNK_SP(imgChunk2, 2));
        // DEBUG("%2d %2d %2d\n", RDH_CHUNK_EP(imgChunk2, 2), RDH_CHUNK_EP(imgChunk2, 3), RDH_CHUNK_EP(imgChunk2, 4));
        // DEBUG("%2d %2d %2d\n\n", RDH_CHUNK_SP(imgChunk2, 3), RDH_CHUNK_EP(imgChunk2, 5), RDH_CHUNK_SP(imgChunk2, 4));
    }

    // 获取采样像素采样像素 SPs 和可嵌入像素 EPs的HSB值
    rdhChunk imgChunkHSB1;
    rdhChunk imgChunkHSB2;
    RDH_CHUNK_SP(imgChunkHSB1, 1) = RDH_IMG_GET_HIGH(RDH_CHUNK_SP(imgChunk1, 1));
    RDH_CHUNK_EP(imgChunkHSB1, 1) = RDH_IMG_GET_HIGH(RDH_CHUNK_EP(imgChunk1, 1));
    RDH_CHUNK_SP(imgChunkHSB1, 2) = RDH_IMG_GET_HIGH(RDH_CHUNK_SP(imgChunk1, 2));
    RDH_CHUNK_EP(imgChunkHSB1, 2) = RDH_IMG_GET_HIGH(RDH_CHUNK_EP(imgChunk1, 2));
    RDH_CHUNK_EP(imgChunkHSB1, 3) = RDH_IMG_GET_HIGH(RDH_CHUNK_EP(imgChunk1, 3));
    RDH_CHUNK_EP(imgChunkHSB1, 4) = RDH_IMG_GET_HIGH(RDH_CHUNK_EP(imgChunk1, 4));
    RDH_CHUNK_SP(imgChunkHSB1, 3) = RDH_IMG_GET_HIGH(RDH_CHUNK_SP(imgChunk1, 3));
    RDH_CHUNK_EP(imgChunkHSB1, 5) = RDH_IMG_GET_HIGH(RDH_CHUNK_EP(imgChunk1, 5));
    RDH_CHUNK_SP(imgChunkHSB1, 4) = RDH_IMG_GET_HIGH(RDH_CHUNK_SP(imgChunk1, 4));

    RDH_CHUNK_SP(imgChunkHSB2, 1) = RDH_IMG_GET_HIGH(RDH_CHUNK_SP(imgChunk2, 1));
    RDH_CHUNK_EP(imgChunkHSB2, 1) = RDH_IMG_GET_HIGH(RDH_CHUNK_EP(imgChunk2, 1));
    RDH_CHUNK_SP(imgChunkHSB2, 2) = RDH_IMG_GET_HIGH(RDH_CHUNK_SP(imgChunk2, 2));
    RDH_CHUNK_EP(imgChunkHSB2, 2) = RDH_IMG_GET_HIGH(RDH_CHUNK_EP(imgChunk2, 2));
    RDH_CHUNK_EP(imgChunkHSB2, 3) = RDH_IMG_GET_HIGH(RDH_CHUNK_EP(imgChunk2, 3));
    RDH_CHUNK_EP(imgChunkHSB2, 4) = RDH_IMG_GET_HIGH(RDH_CHUNK_EP(imgChunk2, 4));
    RDH_CHUNK_SP(imgChunkHSB2, 3) = RDH_IMG_GET_HIGH(RDH_CHUNK_SP(imgChunk2, 3));
    RDH_CHUNK_EP(imgChunkHSB2, 5) = RDH_IMG_GET_HIGH(RDH_CHUNK_EP(imgChunk2, 5));
    RDH_CHUNK_SP(imgChunkHSB2, 4) = RDH_IMG_GET_HIGH(RDH_CHUNK_SP(imgChunk2, 4));

    // {
    //     DEBUG("|------img1HSB------|\n");
    //     DEBUG("%2d %2d %2d\n", RDH_CHUNK_SP(imgChunkHSB1, 1), RDH_CHUNK_EP(imgChunkHSB1, 1), RDH_CHUNK_SP(imgChunkHSB1, 2));
    //     DEBUG("%2d %2d %2d\n", RDH_CHUNK_EP(imgChunkHSB1, 2), RDH_CHUNK_EP(imgChunkHSB1, 3), RDH_CHUNK_EP(imgChunkHSB1, 4));
    //     DEBUG("%2d %2d %2d\n", RDH_CHUNK_SP(imgChunkHSB1, 3), RDH_CHUNK_EP(imgChunkHSB1, 5), RDH_CHUNK_SP(imgChunkHSB1, 4));
    //     DEBUG("|------img2HSB------|\n");
    //     DEBUG("%2d %2d %2d\n", RDH_CHUNK_SP(imgChunkHSB2, 1), RDH_CHUNK_EP(imgChunkHSB2, 1), RDH_CHUNK_SP(imgChunkHSB2, 2));
    //     DEBUG("%2d %2d %2d\n", RDH_CHUNK_EP(imgChunkHSB2, 2), RDH_CHUNK_EP(imgChunkHSB2, 3), RDH_CHUNK_EP(imgChunkHSB2, 4));
    //     DEBUG("%2d %2d %2d\n", RDH_CHUNK_SP(imgChunkHSB2, 3), RDH_CHUNK_EP(imgChunkHSB2, 5), RDH_CHUNK_SP(imgChunkHSB2, 4));
    // }

    // 计算dHSB
    int16_t dHSB[5];
    dHSB[0] = divide_by_2_floor(2 * RDH_CHUNK_EP(imgChunkHSB1, 1) - (RDH_CHUNK_SP(imgChunkHSB1, 1) + RDH_CHUNK_SP(imgChunkHSB1, 2)) +
                                2 * RDH_CHUNK_EP(imgChunkHSB2, 1) - (RDH_CHUNK_SP(imgChunkHSB2, 1) + RDH_CHUNK_SP(imgChunkHSB2, 2)));
    dHSB[1] = divide_by_2_floor(2 * RDH_CHUNK_EP(imgChunkHSB1, 2) - (RDH_CHUNK_SP(imgChunkHSB1, 1) + RDH_CHUNK_SP(imgChunkHSB1, 3)) +
                                2 * RDH_CHUNK_EP(imgChunkHSB2, 2) - (RDH_CHUNK_SP(imgChunkHSB2, 1) + RDH_CHUNK_SP(imgChunkHSB2, 3)));
    dHSB[2] = divide_by_4_floor(4 * RDH_CHUNK_EP(imgChunkHSB1, 3) - (RDH_CHUNK_SP(imgChunkHSB1, 1) + RDH_CHUNK_SP(imgChunkHSB1, 2) + RDH_CHUNK_SP(imgChunkHSB1, 3) + RDH_CHUNK_SP(imgChunkHSB1, 4)) +
                                4 * RDH_CHUNK_EP(imgChunkHSB2, 3) - (RDH_CHUNK_SP(imgChunkHSB2, 1) + RDH_CHUNK_SP(imgChunkHSB2, 2) + RDH_CHUNK_SP(imgChunkHSB2, 3) + RDH_CHUNK_SP(imgChunkHSB2, 4)));
    dHSB[3] = divide_by_2_floor(2 * RDH_CHUNK_EP(imgChunkHSB1, 4) - (RDH_CHUNK_SP(imgChunkHSB1, 2) + RDH_CHUNK_SP(imgChunkHSB1, 4)) +
                                2 * RDH_CHUNK_EP(imgChunkHSB2, 4) - (RDH_CHUNK_SP(imgChunkHSB2, 2) + RDH_CHUNK_SP(imgChunkHSB2, 4)));
    dHSB[4] = divide_by_2_floor(2 * RDH_CHUNK_EP(imgChunkHSB1, 5) - (RDH_CHUNK_SP(imgChunkHSB1, 3) + RDH_CHUNK_SP(imgChunkHSB1, 4)) +
                                2 * RDH_CHUNK_EP(imgChunkHSB2, 5) - (RDH_CHUNK_SP(imgChunkHSB2, 3) + RDH_CHUNK_SP(imgChunkHSB2, 4)));

    // 计算Me1
    int16_t Me1 = dHSB[0];
    RDH_HASH_INIT();
    for (int i = 0; i < 5; i++) // 计算哈希表
    {
        RDH_HASH_SET(dHSB[i]);
    }
    for (int i = 1; i < 5; i++) // 计算出现最多次的值，如果出现次数相同则取最大值
    {
        if (RDH_HASH_GET(Me1) < RDH_HASH_GET(dHSB[i]) ||
            (RDH_HASH_GET(Me1) == RDH_HASH_GET(dHSB[i]) && Me1 < dHSB[i]))
            Me1 = dHSB[i];
    }

    // {
    //     DEBUG("|------dHSB------|\n");
    //     DEBUG("dHSB1:%2d\n", dHSB[0]);
    //     DEBUG("dHSB2:%2d\n", dHSB[1]);
    //     DEBUG("dHSB3:%2d\n", dHSB[2]);
    //     DEBUG("dHSB4:%2d\n", dHSB[3]);
    //     DEBUG("dHSB5:%2d\n", dHSB[4]);
    //     DEBUG("Me1:%2d\n", Me1);
    // }

    // 嵌入数据
    // DEBUG("|------嵌入------|\n");
    bool first = false;
    for (int i = 0; i < 5; i++)
    {
        if (dHSB[i] == Me1) // 峰值，嵌入
        {
            // 获取要嵌入的bit
            uint8_t value = 0;
            if (*now < total)
            {
                value = RDH_DATA_GET(byte, *now);
                *now += 1;
            }

            // 嵌入bit
            RDH_CHUNK_EP(imgChunk1, i + 1) += RDH_EP_VALUE_ADD * value;

            DEBUG("%d", value);
            // 设置m
            if (first == false)
            {
                first = true;
                m |= RDH_ME_SET(i, COUNT_EP);
                m |= value ? RDH_ME_SET(1, RDU_EP)
                           : RDH_ME_SET(0, RDU_EP);
            }
        }
        else if (dHSB[i] > Me1) // 大于峰值，直方图平移
        {
            RDH_CHUNK_EP(imgChunk1, i + 1) += RDH_EP_VALUE_ADD;
        }
        else // 小于峰值不变
        {
        }
    }
    // DEBUG("\n");

    // 复制EP和SP
    img1Line1[0] = RDH_CHUNK_SP(imgChunk1, 1);
    img1Line1[1] = RDH_CHUNK_EP(imgChunk1, 1);
    img1Line1[2] = RDH_CHUNK_SP(imgChunk1, 2);
    img1Line2[0] = RDH_CHUNK_EP(imgChunk1, 2);
    img1Line2[1] = RDH_CHUNK_EP(imgChunk1, 3);
    img1Line2[2] = RDH_CHUNK_EP(imgChunk1, 4);
    img1Line3[0] = RDH_CHUNK_SP(imgChunk1, 3);
    img1Line3[1] = RDH_CHUNK_EP(imgChunk1, 5);
    img1Line3[2] = RDH_CHUNK_SP(imgChunk1, 4);

    img2Line1[0] = RDH_CHUNK_SP(imgChunk2, 1);
    img2Line1[1] = RDH_CHUNK_EP(imgChunk2, 1);
    img2Line1[2] = RDH_CHUNK_SP(imgChunk2, 2);
    img2Line2[0] = RDH_CHUNK_EP(imgChunk2, 2);
    img2Line2[1] = RDH_CHUNK_EP(imgChunk2, 3);
    img2Line2[2] = RDH_CHUNK_EP(imgChunk2, 4);
    img2Line3[0] = RDH_CHUNK_SP(imgChunk2, 3);
    img2Line3[1] = RDH_CHUNK_EP(imgChunk2, 5);
    img2Line3[2] = RDH_CHUNK_SP(imgChunk2, 4);

    // 设置m
    m |= RDH_ME_SET(1, INUSE);

    return m;
}

/**
 * \brief 提取数据
 * \param img1Line1 图像1行1
 * \param img1Line2 图像1行2
 * \param img1Line3 图像1行3
 * \param img2Line1 图像2行1
 * \param img2Line2 图像2行2
 * \param img2Line3 图像2行3
 * \param byte 字节流
 * \param now 当前bit位
 * \param m 额外数据
 */
void rdhExtractDataByte(uint8_t *img1Line1, uint8_t *img1Line2, uint8_t *img1Line3,
                        uint8_t *img2Line1, uint8_t *img2Line2, uint8_t *img2Line3,
                        uint8_t *byte, int *now,
                        uint8_t m)
{
    // 检查m是否跳过
    if (RDH_ME_GET(m, INUSE) == 0)
    {
        return;
    }

    // 获取采样像素采样像素 SPs 和可嵌入像素 EPs
    rdhChunk imgChunk1;
    rdhChunk imgChunk2;
    RDH_CHUNK_SP(imgChunk1, 1) = img1Line1[0];
    RDH_CHUNK_EP(imgChunk1, 1) = img1Line1[1];
    RDH_CHUNK_SP(imgChunk1, 2) = img1Line1[2];
    RDH_CHUNK_EP(imgChunk1, 2) = img1Line2[0];
    RDH_CHUNK_EP(imgChunk1, 3) = img1Line2[1];
    RDH_CHUNK_EP(imgChunk1, 4) = img1Line2[2];
    RDH_CHUNK_SP(imgChunk1, 3) = img1Line3[0];
    RDH_CHUNK_EP(imgChunk1, 5) = img1Line3[1];
    RDH_CHUNK_SP(imgChunk1, 4) = img1Line3[2];

    RDH_CHUNK_SP(imgChunk2, 1) = img2Line1[0];
    RDH_CHUNK_EP(imgChunk2, 1) = img2Line1[1];
    RDH_CHUNK_SP(imgChunk2, 2) = img2Line1[2];
    RDH_CHUNK_EP(imgChunk2, 2) = img2Line2[0];
    RDH_CHUNK_EP(imgChunk2, 3) = img2Line2[1];
    RDH_CHUNK_EP(imgChunk2, 4) = img2Line2[2];
    RDH_CHUNK_SP(imgChunk2, 3) = img2Line3[0];
    RDH_CHUNK_EP(imgChunk2, 5) = img2Line3[1];
    RDH_CHUNK_SP(imgChunk2, 4) = img2Line3[2];

    {
        // DEBUG("|------img1------|\n");
        // DEBUG("%2d %2d %2d\n", RDH_CHUNK_SP(imgChunk1, 1), RDH_CHUNK_EP(imgChunk1, 1), RDH_CHUNK_SP(imgChunk1, 2));
        // DEBUG("%2d %2d %2d\n", RDH_CHUNK_EP(imgChunk1, 2), RDH_CHUNK_EP(imgChunk1, 3), RDH_CHUNK_EP(imgChunk1, 4));
        // DEBUG("%2d %2d %2d\n\n", RDH_CHUNK_SP(imgChunk1, 3), RDH_CHUNK_EP(imgChunk1, 5), RDH_CHUNK_SP(imgChunk1, 4));
        // DEBUG("|------img2------|\n");
        // DEBUG("%2d %2d %2d\n", RDH_CHUNK_SP(imgChunk2, 1), RDH_CHUNK_EP(imgChunk2, 1), RDH_CHUNK_SP(imgChunk2, 2));
        // DEBUG("%2d %2d %2d\n", RDH_CHUNK_EP(imgChunk2, 2), RDH_CHUNK_EP(imgChunk2, 3), RDH_CHUNK_EP(imgChunk2, 4));
        // DEBUG("%2d %2d %2d\n\n", RDH_CHUNK_SP(imgChunk2, 3), RDH_CHUNK_EP(imgChunk2, 5), RDH_CHUNK_SP(imgChunk2, 4));
    }

    // 获取采样像素采样像素 SPs 和可嵌入像素 EPs的HSB值
    rdhChunk imgChunkHSB1;
    rdhChunk imgChunkHSB2;
    RDH_CHUNK_SP(imgChunkHSB1, 1) = RDH_IMG_GET_HIGH(RDH_CHUNK_SP(imgChunk1, 1));
    RDH_CHUNK_EP(imgChunkHSB1, 1) = RDH_IMG_GET_HIGH(RDH_CHUNK_EP(imgChunk1, 1));
    RDH_CHUNK_SP(imgChunkHSB1, 2) = RDH_IMG_GET_HIGH(RDH_CHUNK_SP(imgChunk1, 2));
    RDH_CHUNK_EP(imgChunkHSB1, 2) = RDH_IMG_GET_HIGH(RDH_CHUNK_EP(imgChunk1, 2));
    RDH_CHUNK_EP(imgChunkHSB1, 3) = RDH_IMG_GET_HIGH(RDH_CHUNK_EP(imgChunk1, 3));
    RDH_CHUNK_EP(imgChunkHSB1, 4) = RDH_IMG_GET_HIGH(RDH_CHUNK_EP(imgChunk1, 4));
    RDH_CHUNK_SP(imgChunkHSB1, 3) = RDH_IMG_GET_HIGH(RDH_CHUNK_SP(imgChunk1, 3));
    RDH_CHUNK_EP(imgChunkHSB1, 5) = RDH_IMG_GET_HIGH(RDH_CHUNK_EP(imgChunk1, 5));
    RDH_CHUNK_SP(imgChunkHSB1, 4) = RDH_IMG_GET_HIGH(RDH_CHUNK_SP(imgChunk1, 4));

    RDH_CHUNK_SP(imgChunkHSB2, 1) = RDH_IMG_GET_HIGH(RDH_CHUNK_SP(imgChunk2, 1));
    RDH_CHUNK_EP(imgChunkHSB2, 1) = RDH_IMG_GET_HIGH(RDH_CHUNK_EP(imgChunk2, 1));
    RDH_CHUNK_SP(imgChunkHSB2, 2) = RDH_IMG_GET_HIGH(RDH_CHUNK_SP(imgChunk2, 2));
    RDH_CHUNK_EP(imgChunkHSB2, 2) = RDH_IMG_GET_HIGH(RDH_CHUNK_EP(imgChunk2, 2));
    RDH_CHUNK_EP(imgChunkHSB2, 3) = RDH_IMG_GET_HIGH(RDH_CHUNK_EP(imgChunk2, 3));
    RDH_CHUNK_EP(imgChunkHSB2, 4) = RDH_IMG_GET_HIGH(RDH_CHUNK_EP(imgChunk2, 4));
    RDH_CHUNK_SP(imgChunkHSB2, 3) = RDH_IMG_GET_HIGH(RDH_CHUNK_SP(imgChunk2, 3));
    RDH_CHUNK_EP(imgChunkHSB2, 5) = RDH_IMG_GET_HIGH(RDH_CHUNK_EP(imgChunk2, 5));
    RDH_CHUNK_SP(imgChunkHSB2, 4) = RDH_IMG_GET_HIGH(RDH_CHUNK_SP(imgChunk2, 4));

    // {
    //     DEBUG("|------img1HSB------|\n");
    //     DEBUG("%2d %2d %2d\n", RDH_CHUNK_SP(imgChunkHSB1, 1), RDH_CHUNK_EP(imgChunkHSB1, 1), RDH_CHUNK_SP(imgChunkHSB1, 2));
    //     DEBUG("%2d %2d %2d\n", RDH_CHUNK_EP(imgChunkHSB1, 2), RDH_CHUNK_EP(imgChunkHSB1, 3), RDH_CHUNK_EP(imgChunkHSB1, 4));
    //     DEBUG("%2d %2d %2d\n", RDH_CHUNK_SP(imgChunkHSB1, 3), RDH_CHUNK_EP(imgChunkHSB1, 5), RDH_CHUNK_SP(imgChunkHSB1, 4));
    //     DEBUG("|------img2HSB------|\n");
    //     DEBUG("%2d %2d %2d\n", RDH_CHUNK_SP(imgChunkHSB2, 1), RDH_CHUNK_EP(imgChunkHSB2, 1), RDH_CHUNK_SP(imgChunkHSB2, 2));
    //     DEBUG("%2d %2d %2d\n", RDH_CHUNK_EP(imgChunkHSB2, 2), RDH_CHUNK_EP(imgChunkHSB2, 3), RDH_CHUNK_EP(imgChunkHSB2, 4));
    //     DEBUG("%2d %2d %2d\n", RDH_CHUNK_SP(imgChunkHSB2, 3), RDH_CHUNK_EP(imgChunkHSB2, 5), RDH_CHUNK_SP(imgChunkHSB2, 4));
    // }

    // 计算dHSB
    int16_t dHSB[5];
    dHSB[0] = divide_by_2_floor(2 * RDH_CHUNK_EP(imgChunkHSB1, 1) - (RDH_CHUNK_SP(imgChunkHSB1, 1) + RDH_CHUNK_SP(imgChunkHSB1, 2)) +
                                2 * RDH_CHUNK_EP(imgChunkHSB2, 1) - (RDH_CHUNK_SP(imgChunkHSB2, 1) + RDH_CHUNK_SP(imgChunkHSB2, 2)));
    dHSB[1] = divide_by_2_floor(2 * RDH_CHUNK_EP(imgChunkHSB1, 2) - (RDH_CHUNK_SP(imgChunkHSB1, 1) + RDH_CHUNK_SP(imgChunkHSB1, 3)) +
                                2 * RDH_CHUNK_EP(imgChunkHSB2, 2) - (RDH_CHUNK_SP(imgChunkHSB2, 1) + RDH_CHUNK_SP(imgChunkHSB2, 3)));
    dHSB[2] = divide_by_4_floor(4 * RDH_CHUNK_EP(imgChunkHSB1, 3) - (RDH_CHUNK_SP(imgChunkHSB1, 1) + RDH_CHUNK_SP(imgChunkHSB1, 2) + RDH_CHUNK_SP(imgChunkHSB1, 3) + RDH_CHUNK_SP(imgChunkHSB1, 4)) +
                                4 * RDH_CHUNK_EP(imgChunkHSB2, 3) - (RDH_CHUNK_SP(imgChunkHSB2, 1) + RDH_CHUNK_SP(imgChunkHSB2, 2) + RDH_CHUNK_SP(imgChunkHSB2, 3) + RDH_CHUNK_SP(imgChunkHSB2, 4)));
    dHSB[3] = divide_by_2_floor(2 * RDH_CHUNK_EP(imgChunkHSB1, 4) - (RDH_CHUNK_SP(imgChunkHSB1, 2) + RDH_CHUNK_SP(imgChunkHSB1, 4)) +
                                2 * RDH_CHUNK_EP(imgChunkHSB2, 4) - (RDH_CHUNK_SP(imgChunkHSB2, 2) + RDH_CHUNK_SP(imgChunkHSB2, 4)));
    dHSB[4] = divide_by_2_floor(2 * RDH_CHUNK_EP(imgChunkHSB1, 5) - (RDH_CHUNK_SP(imgChunkHSB1, 3) + RDH_CHUNK_SP(imgChunkHSB1, 4)) +
                                2 * RDH_CHUNK_EP(imgChunkHSB2, 5) - (RDH_CHUNK_SP(imgChunkHSB2, 3) + RDH_CHUNK_SP(imgChunkHSB2, 4)));

    // 获取Me1
    int16_t Me1 = dHSB[RDH_ME_GET(m, COUNT_EP)] - RDH_ME_GET(m, RDU_EP);

    // {
    //     DEBUG("|------dHSB------|\n");
    //     DEBUG("dHSB1:%2d\n", dHSB[0]);
    //     DEBUG("dHSB2:%2d\n", dHSB[1]);
    //     DEBUG("dHSB3:%2d\n", dHSB[2]);
    //     DEBUG("dHSB4:%2d\n", dHSB[3]);
    //     DEBUG("dHSB5:%2d\n", dHSB[4]);
    //     DEBUG("Me1:%2d\n", Me1);
    // }

    // 提取EP中的数据
    // DEBUG("|------提取------|\n");
    for (int i = 0; i < 5; i++)
    {
        // 提取数据
        if (dHSB[i] == Me1)
        {
            // 0
            RDH_DATA_SET(byte, *now, 0);
            *now += 1;
        }
        else if (dHSB[i] == Me1 + 1)
        {
            // 1
            RDH_DATA_SET(byte, *now, 1);
            *now += 1;
        }

        // 恢复图像
        if (dHSB[i] > Me1)
        {
            RDH_CHUNK_EP(imgChunk1, i + 1) -= RDH_EP_VALUE_ADD;
        }
        else
        {
            continue;
        }
    }
    // DEBUG("\n\n\n");

    // 复制EP和SP
    img1Line1[0] = RDH_CHUNK_SP(imgChunk1, 1);
    img1Line1[1] = RDH_CHUNK_EP(imgChunk1, 1);
    img1Line1[2] = RDH_CHUNK_SP(imgChunk1, 2);
    img1Line2[0] = RDH_CHUNK_EP(imgChunk1, 2);
    img1Line2[1] = RDH_CHUNK_EP(imgChunk1, 3);
    img1Line2[2] = RDH_CHUNK_EP(imgChunk1, 4);
    img1Line3[0] = RDH_CHUNK_SP(imgChunk1, 3);
    img1Line3[1] = RDH_CHUNK_EP(imgChunk1, 5);
    img1Line3[2] = RDH_CHUNK_SP(imgChunk1, 4);

    img2Line1[0] = RDH_CHUNK_SP(imgChunk2, 1);
    img2Line1[1] = RDH_CHUNK_EP(imgChunk2, 1);
    img2Line1[2] = RDH_CHUNK_SP(imgChunk2, 2);
    img2Line2[0] = RDH_CHUNK_EP(imgChunk2, 2);
    img2Line2[1] = RDH_CHUNK_EP(imgChunk2, 3);
    img2Line2[2] = RDH_CHUNK_EP(imgChunk2, 4);
    img2Line3[0] = RDH_CHUNK_SP(imgChunk2, 3);
    img2Line3[1] = RDH_CHUNK_EP(imgChunk2, 5);
    img2Line3[2] = RDH_CHUNK_SP(imgChunk2, 4);
}

rdhStatus rdhEmbedData(uint8_t *img1, uint8_t *img2,
                       int w, int h,
                       uint8_t **m, int *mSize,
                       const uint8_t *data, int size)
{
    // 将size转化为字节流大小
    size = RDH_DATA_BYTE_2_BIT(size);

    // 为m分配内存
    *m = (uint8_t *)rdhMalloc((w / 3) * (h / 3));
    *mSize = 0;

    // 复制图像数据
    uint8_t *img1Copy = (uint8_t *)rdhMalloc(w * h);
    uint8_t *img2Copy = (uint8_t *)rdhMalloc(w * h);
    memcpy(img1Copy, img1, w * h);
    memcpy(img2Copy, img2, w * h);

    // 嵌入数据
    int now = 0;
    int i, j;
    for (i = 0; i < w; i += 3)
    {
        for (int j = 0; j < h; j += 3)
        {
            (*m)[(*mSize)++] = rdhEmbedDataByte(&RDH_IMG_POS(img1Copy, w, i, j), &RDH_IMG_POS(img1Copy, w, i, j + 1), &RDH_IMG_POS(img1Copy, w, i, j + 2),
                                                &RDH_IMG_POS(img2Copy, w, i, j), &RDH_IMG_POS(img2Copy, w, i, j + 1), &RDH_IMG_POS(img2Copy, w, i, j + 2),
                                                data, size, &now);
            // 检查数据是否嵌入完毕
            if (now >= size)
            {
                goto SUCESS;
            }
        }
    }

    // 检查是否嵌入完毕
    if (now < size)
    {
        ERROR("嵌入数据失败 : 数据过大\n");
        return RDH_ERROR;
    }

SUCESS:
    // 复制图像数据
    memcpy(img1, img1Copy, w * h);
    memcpy(img2, img2Copy, w * h);
    // 释放内存
    rdhFree(img1Copy);
    rdhFree(img2Copy);

    DEBUG("\n嵌入结束now : %d size : %d 原size : %d\n", now, size, RDH_DATA_BIT_2_BYTE(size));
    return RDH_SUCESS;
}

rdhStatus rdhExtractData(uint8_t *img1, uint8_t *img2,
                         int w, int h,
                         uint8_t *m, int mSize,
                         uint8_t **data)
{
    // 为data分配初始内存
    int size = 0x10;
    *data = (uint8_t *)rdhMalloc(size);
    memset(*data, 0, size);

    // 安全检查
    if (mSize > (w / 3) * (h / 3))
    {
        ERROR("提取数据失败 : m过大\n");
        return RDH_ERROR;
    }

    // 提取数据
    int now = 0;
    int mindex = 0;
    for (int i = 0; i < w; i += 3)
    {
        for (int j = 0; j < h; j += 3)
        {
            // 检查是否结束
            if (mindex >= mSize)
            {
                DEBUG("提取数据结束 : now : %d\n", now);
                return RDH_SUCESS;
            }
            // 检查空间是否足够
            if (RDH_DATA_BIT_2_BYTE(now) >= size - 0x8)
            {
                size += 0x10;
                *data = (uint8_t *)realloc(*data, size);
                memset(*data + size - 0x10, 0, 0x10);
            }
            rdhExtractDataByte(&RDH_IMG_POS(img1, w, i, j), &RDH_IMG_POS(img1, w, i, j + 1), &RDH_IMG_POS(img1, w, i, j + 2),
                               &RDH_IMG_POS(img2, w, i, j), &RDH_IMG_POS(img2, w, i, j + 1), &RDH_IMG_POS(img2, w, i, j + 2),
                               *data, &now, m[mindex++]);
        }
    }

    // 检查是否提取完成
    if (mindex >= mSize)
    {
        return RDH_SUCESS;
    }
    else
    {
        ERROR("提取数据失败 : mindex<mSize???\n");
        return RDH_ERROR;
    }
}

void *rdhMalloc(size_t size)
{
    return malloc(RDH_MALLOC_SIZE(size));
}
void rdhFree(void *data)
{
    free(data);
}