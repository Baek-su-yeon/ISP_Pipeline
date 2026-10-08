#pragma once

#include <string>
#include <opencv2/core.hpp>

// LibRaw 반환값 확인. 실패하면 에러 메시지를 출력하고 false
bool check(int result);

// outdir/name 으로 이미지 저장. 실패하면 에러 메시지를 출력하고 false
bool save(const std::string& outdir, const std::string& name, const cv::Mat& img);

// 두 이미지의 차이 통계를 출력하고, 차이를 눈으로 보기 위한 그림을 반환
cv::Mat printDiff(const cv::Mat& img1, const cv::Mat& img2);

// 저장용: 내부 RGB(0~1, CV_32FC3) → BGR 순서로 바꾼 뒤 8비트로 저장
// OpenCV는 BGR 순서로 저장하므로 저장 직전에만 바꾼다
bool saveRGB8(const std::string& outdir, const std::string& name, const cv::Mat& rgb32f);
