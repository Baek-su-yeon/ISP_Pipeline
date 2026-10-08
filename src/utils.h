#pragma once

#include <string>
#include <opencv2/core.hpp>

// LibRaw 반환값 확인. 실패하면 에러 메시지를 출력하고 false
bool check(int result);

// outdir/name 으로 이미지 저장. 실패하면 에러 메시지를 출력하고 false
bool save(const std::string& outdir, const std::string& name, const cv::Mat& img);

// 두 이미지(CV_8U 또는 CV_16U, 1채널 또는 3채널)의 차이 통계를 출력한다.
// 크기·타입이 다르면 에러 메시지를 출력하고 false.
// 출력: 채널별 최대 차이, 채널별 평균 절대 차이,
//       "어느 채널이든 차이가 있는 화소" 수와 비율,
//       화소별 채널 최대 차이의 크기별 화소 수
//       (8비트: 0, 1, 2, 3 이상 / 16비트: 0, 1~16, 17~256, 257 이상)
// border: 바깥 border 화소를 빼고 비교 (통계에만 적용)
// diffVis: 차이를 눈으로 보기 위한 8비트 그림 (|차이| × visScale, 255에서 포화).
//          border와 상관없이 이미지 전체
bool printDiff(const cv::Mat& img1, const cv::Mat& img2, cv::Mat& diffVis,
               int border = 0, double visScale = 255.0);

// 저장용: 내부 RGB(0~1, CV_32FC3) → BGR 순서로 바꾼 뒤 8비트로 저장
// OpenCV는 BGR 순서로 저장하므로 저장 직전에만 바꾼다
bool saveRGB8(const std::string& outdir, const std::string& name, const cv::Mat& rgb32f);
