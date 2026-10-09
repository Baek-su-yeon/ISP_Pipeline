#pragma once

#include <opencv2/core.hpp>

// Bayer 2x2 블록 하나를 화소 하나로 줄인 AWB 통계용 이미지
struct HalfStats {
    cv::Mat img;           // CV_32FC3, 채널 순서 R, G, B. 선형 0~1, WB 전
    cv::Mat mask;          // CV_8UC1, 1 = 유효 블록, 0 = 포화 블록
    int n_blocks = 0;      // 전체 블록 수
    int n_sat_blocks = 0;  // 포화로 제외된 블록 수
};

// norm: Black Level 정규화 결과 (CV_32FC1, 0~1)
// colors: Bayer 패턴 표, colors[r % 2][c % 2] = LibRaw 색 번호
// 입력이 잘못되면 빈 HalfStats (img.empty())
HalfStats computeHalfStats(const cv::Mat& norm, const int (&colors)[2][2]);