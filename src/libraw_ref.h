#pragma once

#include <string>
#include <opencv2/core.hpp>
#include "raw_loader.h"

// LibRaw 기준 이미지 (검증용).
// 이 프로젝트에서 LibRaw는 원래 읽기·메타데이터 전용이다.
// dcraw_process 같은 LibRaw 처리 함수는 이 파일(libraw_ref.cpp)에서만 쓰고,
// 파이프라인 단계 파일에는 넣지 않는다.

// LibRaw 처리 설정
struct LibRawRefParams {
    // true: 내 파이프라인과 비교 조건을 맞춘 설정 (아래 gamm, outputBps만 바뀜)
    //       output_color = 0, no_auto_bright = 1, use_camera_wb = 1, user_qual = 0,
    //       highlight = 0, adjust_maximum_thr = 0, user_flip = 0,
    //       half_size = 0, four_color_rgb = 0, med_passes = 0
    // false: LibRaw 기본값 그대로. 크기를 맞추기 위해 user_flip = 0만 설정
    bool matchConditions = true;

    double gamm0 = 1.0;    // params.gamm[0]
    double gamm1 = 1.0;    // params.gamm[1]
    int outputBps = 16;    // params.output_bps (8 또는 16)
};

LibRawRefParams LibRawRefA1();   // 선형 16비트 (gamm 1, 1)
LibRawRefParams LibRawRefA2();   // sRGB 8비트 (gamm 1/2.4, 12.92)
LibRawRefParams LibRawRefB();    // LibRaw 기본값

// LibRaw가 쓴 값 (imgdata.color)
struct LibRawValues {
    float camMul[4] = {};
    float preMul[4] = {};
    unsigned black = 0;
    unsigned cblack[4] = {};
    unsigned maximum = 0;
};

struct LibRawRef {
    cv::Mat rgb;                 // RGB 순서, CV_8UC3 또는 CV_16UC3
    LibRawValues afterUnpack;    // unpack 직후
    LibRawValues afterProcess;   // dcraw_process 직후 (실제로 쓰인 값)
};

// 입력 DNG를 새 LibRaw 객체로 다시 열어 dcraw_process → dcraw_make_mem_image로 RGB 이미지를 만든다.
bool MakeLibRawRef(const std::string& path, const LibRawRefParams& params, LibRawRef& out);

// 내 결과와 LibRaw 기준 세 가지(A1, A2, B)를 비교해 수치를 출력하고 outdir/libraw/ 에 저장한다.
// myLinear: 내 Demosaic 결과 (CV_32FC3, RGB, 0~1) → 16비트(× 65535, 반올림)로 바꿔 A1과 비교
// myGamsRGB: 내 sRGB 감마 결과 (CV_32FC3, RGB, 0~1) → 8비트(저장할 때와 같은 convertTo)로 바꿔 A2, B와 비교
bool CompareWithLibRaw(const std::string& input, const std::string& outdir, const RawMeta& meta,
                       const cv::Mat& myLinear, const cv::Mat& myGamsRGB);
