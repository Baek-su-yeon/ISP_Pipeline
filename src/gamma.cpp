#include "gamma.h"

#include <cmath>

// 입력 x: 선형 값 0~1, 반환: 감마 적용 값 0~1
float GammaExp(float x) {
    return std::pow(x, 1.0f / 2.2f);
}

float GammaSRGB(float x) {
    if (x <= 0.0031308f) return 12.92f * x;                  // 어두운 구간: 직선
    return 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;       // 나머지: 거듭제곱
}

cv::Mat ApplyGamma(const cv::Mat& linearImg, float (*gamma)(float)) {
    cv::Mat out = cv::Mat::zeros(linearImg.rows, linearImg.cols, CV_32FC3);
    for (int r = 0; r < linearImg.rows; r++) {
        for (int c = 0; c < linearImg.cols; c++) {
            const cv::Vec3f& v = linearImg.at<cv::Vec3f>(r, c);
            cv::Vec3f& o = out.at<cv::Vec3f>(r, c);

            for (int k = 0; k < 3; k++) {
                o[k] = gamma(v[k]);
            }
        }
    }

    return out;
}
