#pragma once

#include <opencv2/core.hpp>

// 입력 x: 선형 값 0~1, 반환: 감마 적용 값 0~1
float GammaExp(float x);
float GammaSRGB(float x);

// 4단계: 이미지 전체(CV_32FC3, RGB, 0~1)의 모든 값에 gamma 함수를 적용
// 반환: 입력과 같은 크기의 CV_32FC3
cv::Mat ApplyGamma(const cv::Mat& linearImg, float (*gamma)(float));
