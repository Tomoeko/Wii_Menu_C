#include "scanout.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void encode_gpu_color(const uint8_t input[3], bool swap, uint8_t bytes[3])
{
    for (unsigned channel = 0; channel < 3; ++channel)
        bytes[channel] = input[swap ? 2 - channel : channel];
}

static float memory_row(float clip_y, bool bottom_first)
{
    float gl_row = (clip_y + 1.0f) * 0.5f * WM_PSVR2_SCANOUT_HEIGHT;
    return bottom_first ? gl_row : WM_PSVR2_SCANOUT_HEIGHT - gl_row;
}

static void test_detected_layouts(void)
{
    for (unsigned bottom_first = 0; bottom_first < 2; ++bottom_first) {
        for (unsigned swap = 0; swap < 2; ++swap) {
            uint8_t first[3], last[3];
            encode_gpu_color(bottom_first ? wm_psvr2_probe_gl_bottom :
                                            wm_psvr2_probe_gl_top, swap, first);
            encode_gpu_color(bottom_first ? wm_psvr2_probe_gl_top :
                                            wm_psvr2_probe_gl_bottom, swap, last);
            WmPsvr2ScanoutLayout detected = {0};
            assert(wm_psvr2_scanout_classify(first, last, &detected));
            assert(detected.row_zero_is_gl_bottom == (bottom_first != 0));
            assert(detected.gpu_stores_bgr == (swap != 0));

            /* Two separate eyes retain their top, bottom, and FBO UVs under
             * either GPU row mapping, without changing scene/asset coordinates. */
            for (unsigned eye = 0; eye < 2; ++eye) {
                WmVrEyeRect rect = {300 + 2000 * eye, 670, 1300, 731.25f};
                WmPsvr2ScanoutVertex vertices[WM_PSVR2_SCANOUT_VERTICES_PER_EYE];
                wm_psvr2_scanout_eye_vertices(&rect, &detected, vertices);
                static const unsigned corners[] = {0, 1, 3, 0, 3, 2};
                for (unsigned index = 0; index < WM_PSVR2_SCANOUT_VERTICES_PER_EYE; ++index) {
                    bool right = (corners[index] & 1) != 0;
                    bool bottom = (corners[index] & 2) != 0;
                    float x = (vertices[index].x + 1) * 0.5f * WM_PSVR2_SCANOUT_WIDTH;
                    float y = memory_row(vertices[index].y, bottom_first != 0);
                    assert(fabsf(x - (rect.x + (right ? rect.width : 0))) < 0.001f);
                    assert(fabsf(y - (rect.y + (bottom ? rect.height : 0))) < 0.001f);
                    assert(vertices[index].u == (right ? 1 : 0));
                    assert(vertices[index].v == (bottom ? 0 : 1));
                }
            }

            /* Combine observed GPU stores with both native RDMA modes.
             * fmt=1/SWAP clear requires little-endian B,G,R memory bytes. */
            for (unsigned rdma_swap = 0; rdma_swap < 2; ++rdma_swap) {
                uint32_t source[4], source2[4] = {0};
                for (unsigned channel = 0; channel < 4; ++channel)
                    source[channel] = 0x20001u | (rdma_swap ? 1u << 14 : 0);
                assert(wm_psvr2_scanout_rdma_contract(source, source2, &detected));
                assert(detected.native_expects_bgr == (rdma_swap == 0));
                assert(detected.swap_red_blue == ((swap != 0) != (rdma_swap == 0)));
                const uint8_t source_colors[][3] = {
                    {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {19, 105, 213}
                };
                for (unsigned color = 0; color < 4; ++color) {
                    uint8_t shader_output[3], scanout_bytes[3], displayed[3];
                    encode_gpu_color(source_colors[color], detected.swap_red_blue,
                                     shader_output);
                    encode_gpu_color(shader_output, swap, scanout_bytes);
                    encode_gpu_color(scanout_bytes, detected.native_expects_bgr, displayed);
                    assert(memcmp(displayed, source_colors[color], 3) == 0);
                }
            }
            first[0] += 1; /* Tolerate one 8-bit quantization step. */
            assert(wm_psvr2_scanout_classify(first, last, &detected));
            first[0] += 1;
            assert(!wm_psvr2_scanout_classify(first, last, &detected));
        }
    }
}

static void test_invalid_storage(void)
{
    WmPsvr2ScanoutLayout layout = {true, true, true, true};
    uint8_t black[3] = {0};
    uint8_t wrong[3] = {128, 192, 64}; /* Unsupported channel permutation. */
    assert(!wm_psvr2_scanout_classify(black, black, &layout));
    assert(!wm_psvr2_scanout_classify(wrong, wm_psvr2_probe_gl_top, &layout));
    assert(layout.row_zero_is_gl_bottom && layout.gpu_stores_bgr &&
           layout.native_expects_bgr && layout.swap_red_blue);
    assert(!wm_psvr2_scanout_classify(NULL, black, &layout));
    assert(!wm_psvr2_scanout_classify(black, NULL, &layout));
    assert(!wm_psvr2_scanout_classify(black, black, NULL));
}

static void test_invalid_rdma_format(void)
{
    WmPsvr2ScanoutLayout layout = {true, false, false, false};
    uint32_t source[4] = {0x20001, 0x20001, 0x20001, 0x20001};
    uint32_t source2[4] = {0};
    assert(wm_psvr2_scanout_rdma_contract(source, source2, &layout));
    assert(layout.native_expects_bgr && layout.swap_red_blue);
    source[3] |= 1u << 14;
    assert(!wm_psvr2_scanout_rdma_contract(source, source2, &layout));
    assert(layout.native_expects_bgr && layout.swap_red_blue);
    source[3] = 0x20002; /* A 32-bit pixel format cannot use this 3-byte pitch. */
    assert(!wm_psvr2_scanout_rdma_contract(source, source2, &layout));
    source[3] = 0x20001;
    source2[1] = 1; /* Reject an unverified 10-bit format. */
    assert(!wm_psvr2_scanout_rdma_contract(source, source2, &layout));
    assert(!wm_psvr2_scanout_rdma_contract(NULL, source2, &layout));
    assert(!wm_psvr2_scanout_rdma_contract(source, NULL, &layout));
    assert(!wm_psvr2_scanout_rdma_contract(source, source2, NULL));
}

int main(void)
{
    test_detected_layouts();
    test_invalid_storage();
    test_invalid_rdma_format();
    puts("PSVR2 scanout row direction, FBO UVs, GPU byte order, and RDMA color contract passed.");
    return 0;
}
