#pragma once

#include <opencv2/core.hpp>
#include "raw_loader.h"

// 실험용 보조 함수 모음. 단계 함수(black_level, white_balance 등)는 건드리지 않고
// main이 입력(메타데이터, 패턴, 영상)을 바꿔서 넘기는 데 쓴다.

// 패턴 번호 R↔B 바꾸기: 0↔2, 그리고 G_R·G_B(1↔3) (G의 옆 색이 R↔B로 바뀌므로)
void SwapRBColors(int (&colors)[2][2]);

// 실험 1-a: 카메라 패턴 메타데이터를 R↔B로 잘못 읽은 상황을 흉내 낸다.
// colors, bases, gains가 모두 같은 바뀐 패턴을 따르도록 R 위치와 B 위치의 값을 맞바꾼다.
// (위치별 cblack 패턴은 색이 아니라 위치 기준이라 그대로)
void SwapRB(RawMeta& meta);

// 실험 2: Black Level을 빼지 않는다. base = 0 (공통값, 색별 값, 위치 패턴 모두 0)
void ZeroBlack(RawMeta& meta);

// 밴딩 실험: 선형 영상(CV_32FC3, 0~1)을 8비트로 양자화한 뒤 다시 float(/255)로 되돌린다.
cv::Mat QuantizeLinear(const cv::Mat& linear);

// 3채널 영상의 채널별 평균/최댓값을 R, G, B 순서로 한 줄 출력 (CV_32FC3, RGB 순서)
void PrintChannelMean(const char* label, const cv::Mat& img3);
void PrintChannelMax(const char* label, const cv::Mat& img3);

// 실험 1-b 확인: 바뀐 패턴으로 만든 Demosaic(swapped)과 원래 패턴 결과(normal)를 비교.
// 채널 평균이 R↔B 정확히 맞바뀌었는지, 화소 단위 최대 차이는 얼마인지 출력
void PrintSwapCheck(const cv::Mat& swapped, const cv::Mat& normal);

// 밴딩 실험: 입력 linearIn이 0 이상 maxIn 이하인 화소에서, 감마 결과(gammaImg)를
// 8비트(convertTo, 저장할 때와 같은 변환)로 바꿨을 때 서로 다른 값의 개수를 채널별로 출력
void PrintDistinctCounts(const char* label, const cv::Mat& linearIn, const cv::Mat& gammaImg, double maxIn);
