#pragma once

#include <opencv2/core.hpp>

// WB 결과 두 가지
struct WBResult {
    cv::Mat img;    // WBImg: 자르기 전 (0 ~ 최대 이득). 실험 3에서 Demosaic부터 다시 돌릴 때 사용
    cv::Mat clip;   // WBClip: 1에서 자른 것 (0~1). 기본 Demosaic 입력
};

// 2단계: 정규화 결과(CV_32F, 0~1)에 위치별 WB 이득을 곱한다.
// gains: 2×2 위치별 이득 (G = 1 기준)
WBResult WhiteBalance(const cv::Mat& normImg, const float (&gains)[2][2]);
