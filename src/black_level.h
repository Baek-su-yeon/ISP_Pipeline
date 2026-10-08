#pragma once

#include <opencv2/core.hpp>
#include "raw_loader.h"

// 1단계: Black Level 보정 + 0~1 정규화
// rawImg: raw_image 포인터 (LibRaw 객체가 살아 있어야 유효)
// 반환: 유효 영역 크기(realH × realW)의 CV_32F, 0~1 선형 Bayer
cv::Mat NormalizeBlackLevel(const unsigned short* rawImg, const RawMeta& meta);
