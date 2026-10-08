#pragma once

#include <opencv2/core.hpp>

// 감마 검증 1: 대표 입력값 5개의 기대값(Exp, sRGB) 출력
void PrintGammaExpected();

// 감마 검증 2: 입력값 기준으로 구간(0 / 직선 / 거듭제곱)을 나눠 구간별 첫 화소 + 개수 출력
// linearImg: 감마 전 (CV_32FC3), gamExpImg·gamsRGBImg: 감마 후 (CV_32FC3)
void PrintGammaRegions(const cv::Mat& linearImg, const cv::Mat& gamExpImg, const cv::Mat& gamsRGBImg);
