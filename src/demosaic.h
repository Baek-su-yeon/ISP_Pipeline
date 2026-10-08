#pragma once

#include <opencv2/core.hpp>

// 화소 하나의 R·G·B 계산. 이웃 8칸을 색별로 모아 평균. G_R·G_B는 G 하나로 합친다.
// 반환: [0] R, [1] G, [2] B
// OutlineIgnoreDemosaic: 반사 처리가 없으므로 테두리 1화소는 호출하면 안 된다 (밖으로 나감)
cv::Vec3f OutlineIgnoreDemosaic(int r, int c, const int (&colors)[2][2], const cv::Mat& img);

// ReflectionDemosaic: 배열 밖 좌표를 반사로 안쪽에 접으므로 전체 화소에 쓸 수 있다
cv::Vec3f ReflectionDemosaic(int r, int c, const int (&colors)[2][2], const cv::Mat& img);

// 3단계: Bayer 이미지(CV_32F) 전체에 ReflectionDemosaic 적용
// 반환: CV_32FC3, 내부 순서는 RGB ([0] R, [1] G, [2] B)
cv::Mat DemosaicReflection(const cv::Mat& bayer, const int (&colors)[2][2]);
