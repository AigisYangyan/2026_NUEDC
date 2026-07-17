/**
 * @file    fake_board_gpio.c
 * @brief   Test-only fake for BoardGpio_GetEncoderRawSnapshot().
 *
 * Allows host tests to inject arbitrary raw encoder totals without
 * depending on hardware or MSPM0 runtime.
 */
#include "driver/board_gpio/board_gpio.h"

static BoardEncoderRawSnapshot s_fake_raw = { 0, 0 };
static uint8_t s_fake_key_levels = 0u;
static uint8_t s_fake_key_edges = 0u;
static int s_key_raw_read_count = 0;
static bool s_snapshot_fail = false;

void FakeBoardGpio_SetRaw(int32_t left, int32_t right)
{
    s_fake_raw.left = left;
    s_fake_raw.right = right;
}

/* 采样失败注入：默认关闭，不影响未调用它的既有测试 */
void FakeBoardGpio_SetSnapshotFail(bool fail)
{
    s_snapshot_fail = fail;
}

void FakeBoardGpio_ResetKeys(void)
{
    s_fake_key_levels = 0u;
    s_fake_key_edges = 0u;
    s_key_raw_read_count = 0;
}

void FakeBoardGpio_SetKeyLevels(uint8_t pressed_bits)
{
    s_fake_key_levels = pressed_bits;
}

void FakeBoardGpio_SetKeyEdges(uint8_t edge_bits)
{
    s_fake_key_edges = edge_bits;
}

void FakeBoardGpio_OrKeyEdges(uint8_t edge_bits)
{
    s_fake_key_edges |= edge_bits;
}

void FakeBoardGpio_ResetKeyObservability(void)
{
    s_key_raw_read_count = 0;
}

int FakeBoardGpio_GetKeyLevelReadCount(void)
{
    return s_key_raw_read_count;
}

bool BoardGpio_GetEncoderRawSnapshot(BoardEncoderRawSnapshot *out)
{
    if (out == NULL || s_snapshot_fail) {
        return false;
    }
    *out = s_fake_raw;
    return true;
}

uint8_t BoardGpio_ConsumeKeyIrqEdges(void)
{
    uint8_t edge_bits = s_fake_key_edges;

    s_fake_key_edges = 0u;
    return edge_bits;
}

uint8_t BoardGpio_GetKeyRawLevels(void)
{
    s_key_raw_read_count++;
    return s_fake_key_levels;
}
