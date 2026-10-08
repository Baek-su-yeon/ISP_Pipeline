#include "verify.h"

#include <iostream>
#include "gamma.h"

void PrintGammaExpected() {
    double target[5] = {0.001, 0.0031308, 0.18, 0.5, 1.0};
    for (const auto& temp : target) {
        std::cout << "input: " << temp << ", Exp: " << GammaExp(temp) << ", sRGB: " << GammaSRGB(temp) << std::endl;
    }
}

void PrintGammaRegions(const cv::Mat& linearImg, const cv::Mat& gamExpImg, const cv::Mat& gamsRGBImg) {
    int realH = linearImg.rows;
    int realW = linearImg.cols;

    // 감마 검증: 입력값 기준으로 구간을 나눠 첫 화소 출력 + 구간별 개수
    const float thr = 0.0031308f;
    bool foundLin = false, foundPow = false;
    long long cntZero = 0, cntLin = 0, cntPow = 0;

    auto printPixel = [&](const char* name, int r, int c, int k, float x) {
        std::cout << name << " (" << r << ", " << c << ") ch " << k
                  << " | input: " << x
                  << ", sRGB: " << gamsRGBImg.at<cv::Vec3f>(r, c)[k]
                  << ", Exp: " << gamExpImg.at<cv::Vec3f>(r, c)[k] << std::endl;
    };

    for (int r = 0; r < realH; r++) {
        for (int c = 0; c < realW; c++) {
            const cv::Vec3f& v = linearImg.at<cv::Vec3f>(r, c);
            for (int k = 0; k < 3; k++) {
                float x = v[k];
                if (x == 0.0f) { cntZero++; continue; }   // 0은 두 식 모두 0이라 분기 확인 불가

                if (x <= thr) {
                    cntLin++;
                    if (!foundLin) { printPixel("linear", r, c, k, x); foundLin = true; }
                } else {
                    cntPow++;
                    if (!foundPow) { printPixel("power ", r, c, k, x); foundPow = true; }
                }
            }
        }
    }

    long long total = static_cast<long long>(realH) * realW * 3;
    std::cout << "zero: "   << cntZero << " (" << 100.0 * cntZero / total << "%)"
              << ", linear: " << cntLin  << " (" << 100.0 * cntLin  / total << "%)"
              << ", power: "  << cntPow  << " (" << 100.0 * cntPow  / total << "%)" << std::endl;
}
