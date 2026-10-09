#pragma once

#include <optional>
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

// 조명 색 추정값. 길이는 밝기, 방향은 색
struct Illum {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
};

// 채널별 WB 이득
struct Gain {
    double r = 1.0;
    double g = 1.0;
    double b = 1.0;
};

// 이득 → 조명: e_c = 1 / gain_c. 0 이하 이득이 있으면 nullopt
std::optional<Illum> illumFromGain(const Gain& gain);

// 조명 → 이득: gain_c = e_G / e_c (G 이득 = 1). 0 이하 성분이 있으면 nullopt
std::optional<Gain> illumToGain(const Illum& illum);

// G = 1로 정규화 (csv 기록용). e_G가 0 이하면 nullopt
std::optional<Illum> normalizeToG(const Illum& illum);

// 두 조명 사이 각도 (도). 길이가 0인 벡터가 있으면 NaN
double angularError(const Illum& est, const Illum& ref);