#include "black_level.h"

#include <algorithm>

cv::Mat NormalizeBlackLevel(const unsigned short* rawImg, const RawMeta& meta) {
    cv::Mat normImg = cv::Mat::zeros(meta.realH, meta.realW, CV_32F);
    for (int r = 0; r < meta.realH; r++) {
        for (int c = 0; c < meta.realW; c++) {
            int pixel = (r + meta.top) * meta.rawW + (c + meta.left);
            int base = meta.bases[r % 2][c % 2];

            if (meta.patternR && meta.patternC) {
                int patIdx = (r % meta.patternR) * meta.patternC + (c % meta.patternC);
                base += meta.cblackPattern[patIdx];
            }

            // float로 바꾼 뒤 빼고 나누기 → 0~1로 자르기
            float v = (static_cast<float>(rawImg[pixel]) - base) / (meta.WL - base);
            normImg.at<float>(r, c) = std::clamp(v, 0.0f, 1.0f);
        }
    }

    return normImg;
}
