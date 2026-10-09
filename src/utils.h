#pragma once

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <opencv2/core.hpp>

#include "awb.h"

struct RawInfo;
struct LibRawCompareStats;

// LibRaw 반환값 확인. 실패하면 에러 메시지를 출력하고 false
bool check(int result);

// outdir/name 으로 이미지 저장. 실패하면 에러 메시지를 출력하고 false
bool save(const std::string& outdir, const std::string& name, const cv::Mat& img);

// 두 이미지 차이 통계 (computeDiff가 계산하고 printDiffStats가 출력한다)
struct DiffStats {
    int bits = 0;            // 8 또는 16
    int channels = 0;
    int border = 0;          // 통계에서 뺀 바깥 화소 수
    int cols = 0, rows = 0;  // 통계를 낸 영역 크기

    std::vector<double> max_diff;    // 채널별 최대 차이
    std::vector<double> mean_diff;   // 채널별 평균 절대 차이

    long long total = 0;             // 통계를 낸 화소 수 (cols * rows)
    long long n_nonzero = 0;         // 어느 채널이든 차이가 1 이상인 화소 수
    long long n_over1 = 0;           // 어느 채널이든 차이가 2 이상인 화소 수 (차이가 1을 넘는 화소)
    long long bins[4] = {};          // 화소별 채널 최대 차이의 크기별 화소 수
                                     // (8비트: 0, 1, 2, 3 이상 / 16비트: 0, 1~16, 17~256, 257 이상)
};

// 두 이미지(CV_8U 또는 CV_16U, 1채널 또는 3채널)의 차이 통계를 stats에 계산한다 (출력 없음).
// 크기·타입이 다르면 에러 메시지를 출력하고 false.
// border: 바깥 border 화소를 빼고 비교 (통계에만 적용)
// diffVis: 차이를 눈으로 보기 위한 8비트 그림 (|차이| × visScale, 255에서 포화).
//          border와 상관없이 이미지 전체
bool computeDiff(const cv::Mat& img1, const cv::Mat& img2, DiffStats& stats, cv::Mat& diffVis,
                 int border = 0, double visScale = 255.0);

// 차이 통계를 콘솔에 출력: 채널별 최대 차이, 채널별 평균 절대 차이,
// "어느 채널이든 차이가 있는 화소" 수와 비율, 화소별 채널 최대 차이의 크기별 화소 수
void printDiffStats(const DiffStats& stats);

// computeDiff + printDiffStats (통계가 필요 없을 때)
bool printDiff(const cv::Mat& img1, const cv::Mat& img2, cv::Mat& diffVis,
               int border = 0, double visScale = 255.0);

// ---- csv 기록 (전수 실행 결과: 사진당 한 줄) ----

// csv 한 줄: (열 이름, 값) 쌍을 열 순서대로 담는다. 값은 이미 문자열
using CsvRow = std::vector<std::pair<std::string, std::string>>;

// 사진 id: 파일 이름의 첫 '-' 앞 (a0001-jmac_DSC1459.dng → a0001). '-'가 없으면 확장자를 뺀 이름
std::string photoId(const std::string& path);

// 숫자를 csv 값 문자열로 (유효숫자 8자리, 불필요한 0은 없음)
std::string csvNum(double v);

// 모든 결과 csv의 앞 4열: id,status,error,time_ms. status는 "ok", "fail", "skip"
// (skip은 --awb-eval에서 기준 이득이 없을 때만. error에 이유를 담는다)
// 다른 열은 이 줄 뒤에 emplace_back으로 이어 붙인다
CsvRow baseCsvRow(const std::string& id, const std::string& status,
                  const std::string& error, long long time_ms);

// csv_path 끝에 row 한 줄을 추가한다. 파일이 없거나 비어 있으면 헤더(열 이름)부터 쓰고,
// 있으면 첫 줄이 row의 열 이름과 같은지 확인한다 (다르면 에러 메시지를 출력하고 false)
bool appendCsvRow(const std::string& csv_path, const CsvRow& row);

// --meta-only 결과 한 줄: common(baseCsvRow) 뒤에 path와 메타데이터 열을 붙인다.
// info.ok가 false면 path만 채우고 나머지 값은 비운다
CsvRow metaCsvRow(const CsvRow& common, const std::string& path, const RawInfo& info);

// --compare-libraw 결과 열: common(baseCsvRow) 뒤에 max8_r,g,b, mae8_r,g,b, max16_r,g,b, n_over1_8을 붙인다
CsvRow compareCsvRow(const CsvRow& common, const LibRawCompareStats& stats);

// --awb-eval 결과 값. 값이 없는 칸(nullopt)은 csv에서 빈 값이 된다
struct AwbEvalResult {
    std::optional<Illum> ref;                                   // 기록 이득의 조명 (G = 1 정규화)
    std::array<std::optional<Illum>, AWB_METHODS.size()> est;   // 방법별 추정 조명 (G = 1 정규화), AWB_METHODS 순서
    std::array<std::optional<double>, AWB_METHODS.size()> err;  // 방법별 기준 대비 각도 오차 (도)
    std::optional<int> n_blocks;                                // 전체 블록 수
    std::optional<int> n_sat_blocks;                            // 포화 블록 수
};

// --awb-eval 결과 한 줄: common(baseCsvRow) 뒤에 아래 열을 붙인다. 값이 없으면 빈 값
// ref_r,ref_g,ref_b, gw_r,gw_g,gw_b,gw_err, wp_..., sog_..., ge_..., n_blocks,n_sat_blocks
CsvRow awbEvalCsvRow(const CsvRow& common, const AwbEvalResult& result);

// 저장용: 내부 RGB(0~1, CV_32FC3) → BGR 순서로 바꾼 뒤 8비트로 저장
// OpenCV는 BGR 순서로 저장하므로 저장 직전에만 바꾼다
bool saveRGB8(const std::string& outdir, const std::string& name, const cv::Mat& rgb32f);
