#include <chrono>
#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <libraw/libraw.h>
#include <opencv2/core.hpp>

#include "raw_loader.h"
#include "black_level.h"
#include "white_balance.h"
#include "demosaic.h"
#include "gamma.h"
#include "verify.h"
#include "utils.h"
#include "options.h"
#include "experiments.h"
#include "libraw_ref.h"

namespace fs = std::filesystem;

// 검증용 콘솔 출력 켜기/끄기 (기본: 켜짐)
constexpr bool kVerify = true;

// 밴딩 실험에서 8비트 값 개수를 세는 입력 구간 (0 ~ kBandMax)
constexpr double kBandMax = 0.05;

using Clock = std::chrono::steady_clock;

static long long elapsedMs(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

// 일반 실행의 끝: --csv가 있으면 id,status,error,time_ms 한 줄을 기록하고 종료 코드를 돌려준다.
// error가 비어 있으면 성공(ok), 아니면 fail. 원인은 실패한 쪽에서 이미 std::cerr에 출력했다
static int finishRun(const Options& opt, const std::string& input, Clock::time_point start,
                     const std::string& error) {
    const bool ok = error.empty();

    if (!opt.csvPath.empty()) {
        const CsvRow row = baseCsvRow(photoId(input), ok ? "ok" : "fail", error, elapsedMs(start));
        if (!appendCsvRow(opt.csvPath, row)) return 1;
    }

    return ok ? 0 : 1;
}

// --meta-only: open_file만 하고 메타데이터를 csv 한 줄로 기록. 열기에 실패해도 fail 줄을 남긴다
static int runMetaOnly(const Options& opt, const std::string& input) {
    const auto start = Clock::now();

    LibRaw ip;
    RawInfo info;
    std::string error;
    const bool ok = readRawInfo(ip, input, info, error);

    const long long time_ms = elapsedMs(start);

    if (!ok) std::cerr << error << std::endl;

    const CsvRow row = metaCsvRow(baseCsvRow(photoId(input), ok ? "ok" : "fail", error, time_ms), input, info);
    if (!appendCsvRow(opt.csvPath, row)) return 1;

    return ok ? 0 : 1;
}

int main (int argc, char* argv[]) {

    Options opt;
    std::string input;
    switch (ParseOptions(argc, argv, opt, input)) {
        case ParseResult::Help:
            PrintUsage(std::cout, argv[0]);
            return 0;
        case ParseResult::Error:
            PrintUsage(std::cerr, argv[0]);
            return 1;
        case ParseResult::Ok:
            break;
    }

    if (opt.metaOnly) return runMetaOnly(opt, input);

    const auto start = Clock::now();

    // 켜진 옵션 목록
    std::cout << "Options:";
    const std::vector<std::string> enabled = EnabledOptions(opt);
    if (!enabled.empty()) {
        std::string sep = " ";
        for (const auto& name : enabled) { std::cout << sep << "--" << name; sep = ", "; }
    } else {
        std::cout << " (none)";
    }
    std::cout << std::endl;

    // 옵션이 있으면 기본 결과를 덮어쓰지 않도록 exp_<옵션들> 폴더에 저장
    fs::path outPath = fs::path("output/img") / fs::path(input).stem().string();
    if (opt.anyExperiment()) outPath /= ExpDirName(opt);
    std::string outdir = outPath.string();
    if (!opt.noSave) fs::create_directories(outdir);

    // rawImg는 ip가 살아 있는 동안만 유효하므로 main 끝까지 유지
    LibRaw ip;
    RawMeta meta;
    const unsigned short* rawImg = nullptr;
    std::string load_error;
    if (!loadRaw(ip, input, meta, rawImg, &load_error)) return finishRun(opt, input, start, load_error);

    // 실험 1-a: 메타데이터를 R↔B로 잘못 읽은 상황 (colors, bases, gains가 모두 바뀜)
    if (opt.swapRB) SwapRB(meta);

    // 1단계: Black Level 보정·정규화 (CV_32F, 0~1)
    // 실험 2: base를 0으로 본 복사본을 넘긴다
    RawMeta blackMeta = meta;
    if (opt.noBlack) ZeroBlack(blackMeta);
    cv::Mat normImg = NormalizeBlackLevel(rawImg, blackMeta);

    // 2단계: White Balance. wb.img(WBImg)는 자르기 전, wb.clip(WBClip)은 1에서 자른 것
    WBResult wb = WhiteBalance(normImg, meta.gains);

    // 3단계: Demosaic (CV_32FC3, 내부는 RGB: [0] R, [1] G, [2] B)
    // 실험 1-b: Demosaic에만 R↔B 바꾼 패턴을 넘긴다 (Black Level, WB는 원래 패턴)
    int demosaicColors[2][2];
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) demosaicColors[r][c] = meta.colors[r][c];
    }
    if (opt.swapRBDemosaic) SwapRBColors(demosaicColors);

    // 실험 3: 자르지 않은 WBImg를 입력으로
    const cv::Mat& demosaicIn = opt.wbNoclip ? wb.img : wb.clip;

    cv::Mat reflectiondemosaic = DemosaicReflection(demosaicIn, demosaicColors);
    if (opt.swapRBDemosaic) PrintSwapCheck(reflectiondemosaic, DemosaicReflection(demosaicIn, meta.colors));

    PrintChannelMean("Demosaic mean", reflectiondemosaic);
    if (opt.wbNoclip) PrintChannelMax("Demosaic max", reflectiondemosaic);

    if (!opt.noSave && !saveRGB8(outdir, "reflection_demosaic.png", reflectiondemosaic)) {
        return finishRun(opt, input, start, "Failed to save reflection_demosaic.png");
    }

    // 나중에 CCM 단계는 여기(Demosaic과 감마 사이)에 cv::Mat → cv::Mat 형태로 끼워 넣는다

    // 밴딩 실험: 선형 결과를 8비트로 양자화했다가 되돌려 감마에 넣는다
    cv::Mat gammaIn = opt.quantizeLinear ? QuantizeLinear(reflectiondemosaic) : reflectiondemosaic;

    // 4단계: 감마 (1을 넘는 입력도 식을 그대로 적용)
    if (kVerify) PrintGammaExpected();

    cv::Mat gamExpImg = ApplyGamma(gammaIn, GammaExp);
    cv::Mat gamsRGBImg = ApplyGamma(gammaIn, GammaSRGB);

    if (!opt.noSave && !saveRGB8(outdir, "gamExpImg.png", gamExpImg)) {
        return finishRun(opt, input, start, "Failed to save gamExpImg.png");
    }
    if (!opt.noSave && !saveRGB8(outdir, "gamsRGBImg.png", gamsRGBImg)) {
        return finishRun(opt, input, start, "Failed to save gamsRGBImg.png");
    }

    if (kVerify) PrintGammaRegions(gammaIn, gamExpImg, gamsRGBImg);

    PrintChannelMean("gamsRGB mean", gamsRGBImg);

    // 밴딩 실험: 입력 0~kBandMax 구간의 감마 결과 8비트 값 개수 (양자화 없는 경로와 비교)
    // 구간은 양자화 전 Demosaic 값으로 고른다 (두 경로에서 같은 화소 집합)
    if (opt.quantizeLinear) {
        cv::Mat gamExpFloat = ApplyGamma(reflectiondemosaic, GammaExp);
        cv::Mat gamsRGBFloat = ApplyGamma(reflectiondemosaic, GammaSRGB);

        std::cout << "Distinct 8-bit values for input 0 ~ " << kBandMax << std::endl;
        PrintDistinctCounts("sRGB float path    ", reflectiondemosaic, gamsRGBFloat, kBandMax);
        PrintDistinctCounts("sRGB quantized path", reflectiondemosaic, gamsRGBImg, kBandMax);
        PrintDistinctCounts("Exp  float path    ", reflectiondemosaic, gamExpFloat, kBandMax);
        PrintDistinctCounts("Exp  quantized path", reflectiondemosaic, gamExpImg, kBandMax);
    }

    // 검증: LibRaw 기준 이미지와 비교 (내 파이프라인은 평소대로 돌린 뒤)
    if (opt.compareLibraw) {
        if (!CompareWithLibRaw(input, outPath.string(), meta, reflectiondemosaic, gamsRGBImg)) {
            return finishRun(opt, input, start, "LibRaw comparison failed");
        }
    }

    return finishRun(opt, input, start, "");
}
