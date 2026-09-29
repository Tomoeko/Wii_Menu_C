#include "corruption_vectors.h"
#include "outline_vector.h"

#include <stddef.h>

/* Exact cubic contours for the static English corruption prompt,
 * traced from the user-supplied RodinNTLG Pro DB OTF.
 * Source SHA-256: 2848d0d5d1b6452f201e50ee93f8356b6b91fb9305522a4481464ff80c3b1c1a.
 * Coordinates and advances are in the source font's 1000-unit em.
 * These are paths, not a copy of the complete font or a runtime file.
 * The command lists are intentionally one vector operation per line.
 */
#define MOVE(x, y)                                                                     \
    {                                                                                  \
        WM_CFF_MOVE, {0, 0}, {0, 0}, {                                                 \
            x, y                                                                       \
        }                                                                              \
    }
#define LINE(x, y)                                                                     \
    {                                                                                  \
        WM_CFF_LINE, {0, 0}, {0, 0}, {                                                 \
            x, y                                                                       \
        }                                                                              \
    }
#define CUBIC(ax, ay, bx, by, x, y)                                                    \
    {                                                                                  \
        WM_CFF_CUBIC, {ax, ay}, {bx, by}, {                                            \
            x, y                                                                       \
        }                                                                              \
    }
#define COUNT(items) (sizeof(items) / sizeof((items)[0]))

static const WmCffSegment glyph_period[] = {
    MOVE(236, 0),
    LINE(236, 140),
    LINE(111, 140),
    LINE(111, 0),
};

static const WmCffSegment glyph_upper_m[] = {
    MOVE(832, 0),
    LINE(832, 780),
    LINE(674, 780),
    LINE(491, 235),
    CUBIC(480, 202, 470, 162, 468, 144),
    LINE(465, 144),
    CUBIC(463, 162, 452, 202, 441, 235),
    LINE(258, 780),
    LINE(101, 780),
    LINE(101, 0),
    LINE(200, 0),
    LINE(200, 433),
    CUBIC(200, 504, 196, 592, 195, 647),
    LINE(199, 647),
    CUBIC(206, 616, 220, 570, 228, 547),
    LINE(413, 0),
    LINE(519, 0),
    LINE(705, 550),
    CUBIC(711, 569, 725, 620, 729, 647),
    LINE(733, 647),
    CUBIC(731, 591, 726, 489, 726, 435),
    LINE(726, 0),
};

static const WmCffSegment glyph_upper_o[] = {
    MOVE(738, 391),
    CUBIC(738, 617, 613, 798, 396, 798),
    CUBIC(180, 798, 54, 618, 54, 391),
    CUBIC(54, 161, 180, -18, 396, -18),
    CUBIC(613, -18, 738, 161, 738, 391),
    MOVE(621, 391),
    CUBIC(621, 210, 530, 82, 396, 82),
    CUBIC(261, 82, 170, 210, 170, 391),
    CUBIC(170, 573, 264, 698, 396, 698),
    CUBIC(532, 698, 621, 573, 621, 391),
};

static const WmCffSegment glyph_upper_p[] = {
    MOVE(646, 526), CUBIC(646, 675, 544, 780, 400, 780),
    LINE(94, 780),  LINE(94, 0),
    LINE(209, 0),   LINE(209, 272),
    LINE(399, 272), CUBIC(553, 272, 646, 377, 646, 526),
    MOVE(532, 526), CUBIC(532, 424, 460, 369, 374, 369),
    LINE(207, 369), LINE(207, 683),
    LINE(372, 683), CUBIC(463, 683, 532, 626, 532, 526),
};

static const WmCffSegment glyph_upper_t[] = {
    MOVE(597, 682), LINE(597, 780), LINE(18, 780), LINE(18, 682),
    LINE(249, 682), LINE(249, 0),   LINE(366, 0),  LINE(366, 682),
};

static const WmCffSegment glyph_upper_w[] = {
    MOVE(912, 780),
    LINE(800, 780),
    LINE(686, 237),
    CUBIC(682, 208, 677, 153, 675, 114),
    LINE(672, 114),
    CUBIC(670, 152, 665, 210, 659, 238),
    LINE(538, 780),
    LINE(404, 780),
    LINE(289, 239),
    CUBIC(282, 206, 278, 148, 274, 111),
    LINE(272, 111),
    CUBIC(272, 147, 265, 210, 259, 240),
    LINE(142, 780),
    LINE(26, 780),
    LINE(204, 0),
    LINE(337, 0),
    LINE(457, 544),
    CUBIC(465, 582, 469, 667, 470, 675),
    LINE(473, 675),
    CUBIC(473, 674, 473, 671, 474, 666),
    CUBIC(475, 641, 479, 574, 485, 544),
    LINE(606, 0),
    LINE(737, 0),
};

static const WmCffSegment glyph_lower_a[] = {
    MOVE(585, 85),
    CUBIC(574, 82, 563, 80, 554, 80),
    CUBIC(524, 80, 505, 97, 505, 135),
    LINE(505, 398),
    CUBIC(505, 515, 425, 565, 280, 565),
    CUBIC(183, 565, 100, 528, 54, 470),
    LINE(101, 390),
    CUBIC(133, 442, 216, 478, 273, 478),
    CUBIC(350, 478, 391, 461, 391, 406),
    CUBIC(391, 375, 318, 345, 236, 326),
    CUBIC(117, 300, 44, 246, 44, 139),
    CUBIC(44, 137, 44, 135, 44, 133),
    CUBIC(46, 41, 102, -12, 215, -12),
    CUBIC(301, -12, 388, 16, 419, 66),
    CUBIC(425, 15, 469, -11, 522, -11),
    CUBIC(535, -11, 549, -9, 563, -6),
    MOVE(396, 163),
    CUBIC(378, 108, 297, 74, 238, 74),
    CUBIC(181, 74, 153, 97, 150, 143),
    CUBIC(150, 145, 150, 147, 150, 149),
    CUBIC(150, 211, 194, 232, 263, 249),
    CUBIC(321, 264, 372, 282, 396, 306),
};

static const WmCffSegment glyph_lower_b[] = {
    MOVE(585, 277),
    CUBIC(585, 465, 492, 565, 346, 565),
    CUBIC(272, 565, 207, 534, 183, 485),
    LINE(183, 780),
    LINE(74, 780),
    LINE(74, 0),
    LINE(154, 0),
    LINE(175, 66),
    CUBIC(204, 22, 275, -12, 346, -12),
    CUBIC(489, -12, 585, 87, 585, 277),
    MOVE(475, 277),
    CUBIC(475, 141, 410, 76, 323, 76),
    CUBIC(258, 76, 215, 111, 181, 159),
    LINE(181, 337),
    CUBIC(181, 413, 245, 476, 322, 476),
    CUBIC(412, 476, 475, 411, 475, 277),
};

static const WmCffSegment glyph_lower_c[] = {
    MOVE(540, 77),
    LINE(501, 159),
    CUBIC(465, 106, 401, 78, 340, 78),
    CUBIC(248, 78, 154, 141, 154, 277),
    CUBIC(154, 414, 251, 475, 339, 475),
    CUBIC(400, 475, 458, 449, 495, 396),
    LINE(534, 484),
    CUBIC(484, 537, 417, 565, 326, 565),
    CUBIC(168, 565, 44, 462, 44, 277),
    CUBIC(44, 85, 177, -12, 326, -12),
    CUBIC(423, -12, 489, 18, 540, 77),
};

static const WmCffSegment glyph_lower_d[] = {
    MOVE(564, 0),
    LINE(564, 780),
    LINE(455, 780),
    LINE(455, 496),
    CUBIC(426, 536, 371, 565, 295, 565),
    CUBIC(156, 565, 44, 466, 44, 276),
    CUBIC(44, 88, 150, -12, 295, -12),
    CUBIC(366, -12, 444, 19, 471, 82),
    LINE(471, 0),
    MOVE(458, 215),
    CUBIC(458, 138, 392, 76, 315, 76),
    CUBIC(225, 76, 154, 141, 154, 275),
    CUBIC(154, 411, 227, 476, 314, 476),
    CUBIC(380, 476, 430, 442, 458, 384),
};

static const WmCffSegment glyph_lower_e[] = {
    MOVE(562, 278),
    CUBIC(562, 284, 562, 289, 562, 295),
    CUBIC(562, 460, 462, 565, 310, 565),
    CUBIC(145, 565, 44, 444, 44, 277),
    CUBIC(44, 101, 152, -12, 313, -12),
    CUBIC(430, -12, 505, 35, 558, 115),
    LINE(502, 185),
    CUBIC(467, 117, 396, 77, 320, 77),
    CUBIC(228, 77, 158, 133, 146, 224),
    MOVE(457, 349),
    LINE(144, 307),
    CUBIC(148, 412, 213, 477, 309, 477),
    CUBIC(392, 477, 450, 423, 457, 349),
};

static const WmCffSegment glyph_lower_f[] = {
    MOVE(326, 708), LINE(307, 798), CUBIC(139, 794, 99, 738, 99, 625),
    LINE(99, 553),  LINE(21, 553),  LINE(21, 466),
    LINE(99, 466),  LINE(99, 0),    LINE(208, 0),
    LINE(208, 466), LINE(308, 466), LINE(308, 553),
    LINE(204, 553), LINE(204, 622), CUBIC(204, 682, 216, 707, 326, 708),
};

static const WmCffSegment glyph_lower_g[] = {
    MOVE(564, 13),
    LINE(564, 553),
    LINE(482, 553),
    LINE(465, 489),
    CUBIC(439, 535, 380, 565, 295, 565),
    CUBIC(156, 565, 52, 472, 52, 289),
    CUBIC(52, 108, 156, 19, 298, 19),
    CUBIC(368, 19, 433, 50, 459, 95),
    LINE(459, 21),
    CUBIC(459, -67, 400, -112, 284, -112),
    CUBIC(221, -112, 137, -97, 79, -75),
    LINE(101, -179),
    CUBIC(142, -193, 218, -203, 286, -203),
    CUBIC(476, -203, 564, -128, 564, 13),
    MOVE(459, 231),
    CUBIC(459, 159, 392, 100, 316, 100),
    CUBIC(228, 100, 159, 158, 159, 289),
    CUBIC(159, 420, 228, 476, 315, 476),
    CUBIC(380, 476, 429, 445, 459, 386),
};

static const WmCffSegment glyph_lower_h[] = {
    MOVE(559, 0),
    LINE(559, 382),
    CUBIC(559, 495, 476, 565, 358, 565),
    CUBIC(269, 565, 208, 530, 183, 486),
    LINE(183, 780),
    LINE(74, 780),
    LINE(74, 0),
    LINE(183, 0),
    LINE(183, 373),
    CUBIC(205, 441, 265, 475, 330, 475),
    CUBIC(399, 475, 450, 432, 450, 357),
    LINE(450, 0),
};

static const WmCffSegment glyph_lower_i[] = {
    MOVE(190, 649), LINE(190, 780), LINE(74, 780), LINE(74, 649),
    MOVE(187, 0),   LINE(187, 553), LINE(76, 553), LINE(76, 0),
};

static const WmCffSegment glyph_lower_l[] = {
    MOVE(191, 0),
    LINE(191, 780),
    LINE(78, 780),
    LINE(78, 0),
};

static const WmCffSegment glyph_lower_m[] = {
    MOVE(805, 0),
    LINE(805, 383),
    CUBIC(805, 494, 738, 565, 635, 565),
    CUBIC(555, 565, 493, 526, 472, 474),
    CUBIC(450, 530, 397, 565, 322, 565),
    CUBIC(247, 565, 198, 531, 174, 488),
    LINE(174, 553),
    LINE(74, 553),
    LINE(74, 0),
    LINE(183, 0),
    LINE(183, 387),
    CUBIC(201, 440, 237, 475, 286, 475),
    CUBIC(351, 475, 382, 435, 382, 359),
    LINE(382, 0),
    LINE(490, 0),
    LINE(490, 365),
    CUBIC(492, 426, 538, 475, 598, 475),
    CUBIC(661, 475, 695, 435, 695, 359),
    LINE(695, 0),
};

static const WmCffSegment glyph_lower_n[] = {
    MOVE(559, 0),
    LINE(559, 374),
    CUBIC(559, 487, 477, 565, 358, 565),
    CUBIC(266, 565, 200, 528, 174, 477),
    LINE(174, 553),
    LINE(74, 553),
    LINE(74, 0),
    LINE(183, 0),
    LINE(183, 373),
    CUBIC(205, 441, 265, 475, 330, 475),
    CUBIC(399, 475, 450, 432, 450, 357),
    LINE(450, 0),
};

static const WmCffSegment glyph_lower_o[] = {
    MOVE(592, 277),
    CUBIC(592, 451, 488, 565, 318, 565),
    CUBIC(151, 565, 44, 452, 44, 277),
    CUBIC(44, 101, 151, -12, 318, -12),
    CUBIC(488, -12, 592, 101, 592, 277),
    MOVE(481, 277),
    CUBIC(481, 151, 414, 77, 318, 77),
    CUBIC(226, 77, 155, 153, 155, 277),
    CUBIC(155, 402, 226, 475, 318, 475),
    CUBIC(414, 475, 481, 402, 481, 277),
};

static const WmCffSegment glyph_lower_p[] = {
    MOVE(585, 277),
    CUBIC(585, 465, 488, 565, 342, 565),
    CUBIC(270, 565, 195, 528, 174, 470),
    LINE(174, 553),
    LINE(74, 553),
    LINE(74, -193),
    LINE(183, -193),
    LINE(183, 52),
    CUBIC(215, 16, 283, -12, 342, -12),
    CUBIC(486, -12, 585, 87, 585, 277),
    MOVE(475, 277),
    CUBIC(475, 141, 410, 76, 323, 76),
    CUBIC(258, 76, 215, 111, 181, 159),
    LINE(181, 337),
    CUBIC(181, 413, 245, 476, 322, 476),
    CUBIC(412, 476, 475, 411, 475, 277),
};

static const WmCffSegment glyph_lower_r[] = {
    MOVE(367, 463),
    LINE(346, 565),
    CUBIC(263, 562, 203, 541, 170, 495),
    LINE(170, 553),
    LINE(74, 553),
    LINE(74, 0),
    LINE(183, 0),
    LINE(183, 380),
    CUBIC(204, 435, 255, 467, 325, 467),
    CUBIC(338, 467, 352, 465, 367, 463),
};

static const WmCffSegment glyph_lower_s[] = {
    MOVE(499, 149),
    CUBIC(499, 249, 431, 305, 282, 337),
    CUBIC(189, 358, 164, 379, 164, 416),
    CUBIC(164, 456, 201, 479, 267, 479),
    CUBIC(339, 479, 401, 447, 438, 394),
    LINE(484, 475),
    CUBIC(449, 528, 370, 565, 268, 565),
    CUBIC(132, 565, 55, 502, 55, 412),
    CUBIC(55, 335, 109, 278, 242, 250),
    CUBIC(345, 228, 388, 200, 388, 148),
    CUBIC(388, 104, 350, 79, 274, 79),
    CUBIC(203, 79, 125, 122, 86, 183),
    LINE(37, 100),
    CUBIC(80, 34, 174, -12, 272, -12),
    CUBIC(424, -12, 499, 48, 499, 149),
};

static const WmCffSegment glyph_lower_t[] = {
    MOVE(346, 81),  CUBIC(234, 81, 218, 87, 218, 165),
    LINE(218, 466), LINE(316, 466),
    LINE(316, 553), LINE(218, 553),
    LINE(218, 727), LINE(113, 727),
    LINE(113, 553), LINE(31, 553),
    LINE(31, 466),  LINE(109, 466),
    LINE(109, 145), CUBIC(109, 22, 164, -12, 323, -12),
};

static const WmCffSegment glyph_lower_u[] = {
    MOVE(558, 0),
    LINE(558, 553),
    LINE(448, 553),
    LINE(448, 184),
    CUBIC(426, 117, 367, 82, 301, 82),
    CUBIC(233, 82, 181, 125, 181, 200),
    LINE(181, 553),
    LINE(72, 553),
    LINE(72, 175),
    CUBIC(72, 62, 157, -12, 274, -12),
    CUBIC(359, -12, 430, 23, 457, 75),
    LINE(457, 0),
};

static const WmCffSegment glyph_lower_y[] = {
    MOVE(538, 553),
    LINE(429, 553),
    LINE(323, 221),
    CUBIC(312, 184, 304, 149, 300, 119),
    LINE(299, 119),
    CUBIC(297, 131, 295, 144, 292, 158),
    CUBIC(287, 178, 280, 201, 272, 221),
    LINE(132, 553),
    LINE(15, 553),
    LINE(243, 15),
    CUBIC(207, -86, 184, -108, 68, -108),
    CUBIC(63, -108, 58, -108, 52, -108),
    LINE(81, -203),
    CUBIC(230, -203, 298, -160, 338, -43),
};

typedef struct VectorGlyphRecord {
    unsigned char character;
    unsigned advance_units;
    const WmCffSegment *segments;
    unsigned segment_count;
} VectorGlyphRecord;

static const VectorGlyphRecord GLYPHS[] = {
    {' ', 345, NULL, 0},
    {'.', 345, glyph_period, COUNT(glyph_period)},
    {'M', 919, glyph_upper_m, COUNT(glyph_upper_m)},
    {'O', 798, glyph_upper_o, COUNT(glyph_upper_o)},
    {'P', 691, glyph_upper_p, COUNT(glyph_upper_p)},
    {'T', 614, glyph_upper_t, COUNT(glyph_upper_t)},
    {'W', 940, glyph_upper_w, COUNT(glyph_upper_w)},
    {'a', 591, glyph_lower_a, COUNT(glyph_lower_a)},
    {'b', 629, glyph_lower_b, COUNT(glyph_lower_b)},
    {'c', 576, glyph_lower_c, COUNT(glyph_lower_c)},
    {'d', 638, glyph_lower_d, COUNT(glyph_lower_d)},
    {'e', 602, glyph_lower_e, COUNT(glyph_lower_e)},
    {'f', 327, glyph_lower_f, COUNT(glyph_lower_f)},
    {'g', 630, glyph_lower_g, COUNT(glyph_lower_g)},
    {'h', 632, glyph_lower_h, COUNT(glyph_lower_h)},
    {'i', 264, glyph_lower_i, COUNT(glyph_lower_i)},
    {'l', 265, glyph_lower_l, COUNT(glyph_lower_l)},
    {'m', 877, glyph_lower_m, COUNT(glyph_lower_m)},
    {'n', 632, glyph_lower_n, COUNT(glyph_lower_n)},
    {'o', 636, glyph_lower_o, COUNT(glyph_lower_o)},
    {'p', 629, glyph_lower_p, COUNT(glyph_lower_p)},
    {'r', 384, glyph_lower_r, COUNT(glyph_lower_r)},
    {'s', 538, glyph_lower_s, COUNT(glyph_lower_s)},
    {'t', 361, glyph_lower_t, COUNT(glyph_lower_t)},
    {'u', 632, glyph_lower_u, COUNT(glyph_lower_u)},
    {'y', 555, glyph_lower_y, COUNT(glyph_lower_y)},
};

bool wm_corruption_vector_raster(unsigned char character, unsigned pixel_size,
                                 WmOutlineBitmap *bitmap) {
    if (!bitmap)
        return false;
    for (size_t index = 0; index < COUNT(GLYPHS); index++) {
        const VectorGlyphRecord *glyph = &GLYPHS[index];
        if (glyph->character != character)
            continue;
        return wm_outline_raster_vector(glyph->segments, glyph->segment_count, 1000,
                                        pixel_size, glyph->advance_units, bitmap);
    }
    return false;
}
