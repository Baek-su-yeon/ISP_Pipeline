#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include <libraw/libraw.h>
#include <opencv2/core.hpp>

#include "raw_loader.h"
#include "awb.h"
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
// --compare-libraw면 비교 지표 열이 같은 줄에 붙는다 (fail이면 열은 같게 두고 값만 비운다)
static int finishRun(const Options& opt, const std::string& input, Clock::time_point start,
                     const std::string& error, const LibRawCompareStats& compare_stats) {
    const bool ok = error.empty();

    if (!opt.csvPath.empty()) {
        CsvRow row = baseCsvRow(photoId(input), ok ? "ok" : "fail", error, elapsedMs(start));

        if (opt.compareLibraw) {
            const size_t n_common = row.size();
            row = compareCsvRow(row, compare_stats);
            if (!ok) {
                for (size_t i = n_common; i < row.size(); i++) row[i].second.clear();
            }
        }

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

// 콘솔용 고정 소수점 문자열
static std::string fixedNum(double v, int precision) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(precision) << v;
    return os.str();
}

// 기록 이득(meta.gains)에서 R 위치·B 위치 값을 읽어 Gain{gain_r, 1, gain_b}로 만든다 (G = 1 기준)
// R 또는 B 위치를 찾지 못하면 그 이득은 0 (기준 조명 판정에서 걸러진다)
static Gain recordedGain(const RawMeta& meta) {
    Gain gain{0.0, 1.0, 0.0};
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            if (meta.colors[r][c] == 0) gain.r = meta.gains[r][c];
            else if (meta.colors[r][c] == 2) gain.b = meta.gains[r][c];
        }
    }
    return gain;
}

// 기록 이득이 가리키는 기준 조명 (G = 1 정규화). R·B 이득이 유한하고 0 초과일 때만 있다
// illumFromGain은 NaN·inf를 거르지 않으므로 여기서 먼저 확인한다
static std::optional<Illum> referenceIllum(const Gain& gain) {
    if (!std::isfinite(gain.r) || !std::isfinite(gain.b) || gain.r <= 0.0 || gain.b <= 0.0) {
        return std::nullopt;
    }
    const std::optional<Illum> illum = illumFromGain(gain);
    if (!illum) return std::nullopt;
    return normalizeToG(*illum);
}

// --awb-eval 콘솔 출력: 기준과 방법별 조명(R, 1, B)·각도 오차
static void printAwbEval(const std::string& id, const AwbEvalResult& result) {
    auto illumText = [](const std::optional<Illum>& e) {
        if (!e) return std::string("R -       G -       B -");
        return "R " + fixedNum(e->r, 4) + "  G " + fixedNum(e->g, 4) + "  B " + fixedNum(e->b, 4);
    };

    std::cout << "AWB eval " << id << " (blocks " << result.n_blocks.value_or(0)
              << ", saturated " << result.n_sat_blocks.value_or(0) << ")" << std::endl;
    std::cout << "  ref  " << illumText(result.ref) << std::endl;
    for (size_t i = 0; i < AWB_METHODS.size(); i++) {
        std::cout << "  " << std::left << std::setw(4) << awbMethodName(AWB_METHODS[i]) << std::right
                  << illumText(result.est[i]);
        if (result.err[i]) std::cout << "  err " << fixedNum(*result.err[i], 3) << " deg";
        std::cout << std::endl;
    }
}

// --awb-eval의 끝: --csv가 있으면 한 줄을 기록하고 종료 코드를 돌려준다 (fail만 1, skip은 0)
static int finishAwbEval(const Options& opt, const std::string& input, Clock::time_point start,
                         const std::string& status, const std::string& error,
                         const AwbEvalResult& result) {
    const long long time_ms = elapsedMs(start);

    if (!opt.csvPath.empty()) {
        const CsvRow row = awbEvalCsvRow(baseCsvRow(photoId(input), status, error, time_ms), result);
        if (!appendCsvRow(opt.csvPath, row)) return 1;
    }

    return status == "fail" ? 1 : 0;
}

// --awb-eval: 정규화 → 절반 크기 통계 → 4종 추정 → 기록 이득 대비 각도 오차를 csv 한 줄로 기록
// WB 이후는 돌리지 않고 png·출력 폴더도 만들지 않는다
static int runAwbEval(const Options& opt, const std::string& input) {
    const auto start = Clock::now();

    LibRaw ip;
    RawMeta meta;
    const unsigned short* rawImg = nullptr;
    std::string error;
    if (!loadRaw(ip, input, meta, rawImg, &error)) {
        return finishAwbEval(opt, input, start, "fail", error, AwbEvalResult{});
    }

    const cv::Mat normImg = NormalizeBlackLevel(rawImg, meta);
    const HalfStats hs = computeHalfStats(normImg, meta.colors);
    if (hs.img.empty()) {
        error = "half stats failed";
        std::cerr << error << std::endl;
        return finishAwbEval(opt, input, start, "fail", error, AwbEvalResult{});
    }

    AwbEvalResult result;
    result.n_blocks = hs.n_blocks;
    result.n_sat_blocks = hs.n_sat_blocks;
    result.ref = referenceIllum(recordedGain(meta));

    // 방법 하나가 실패해도 나머지는 계속한다 (그 방법의 열만 빈 값)
    for (size_t i = 0; i < AWB_METHODS.size(); i++) {
        const std::optional<Illum> est = estimateIllum(hs, AWB_METHODS[i]);
        if (!est) continue;

        result.est[i] = normalizeToG(*est);
        if (result.est[i] && result.ref) result.err[i] = angularError(*result.est[i], *result.ref);
    }

    printAwbEval(photoId(input), result);

    // 기준이 없으면 추정 조명만 남기고 skip (카메라가 WB 이득을 기록하지 않은 사진)
    if (!result.ref) {
        std::cout << "skip: no_cam_mul (recorded R/B gain is not a positive finite number)" << std::endl;
        return finishAwbEval(opt, input, start, "skip", "no_cam_mul", result);
    }

    return finishAwbEval(opt, input, start, "ok", "", result);
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
    LibRawCompareStats compare_stats;   // --compare-libraw --csv일 때 채워진다

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

    // --awb-eval은 출력 폴더를 만들기 전에 분기한다 (png도 폴더도 만들지 않는다)
    if (opt.awbEval) return runAwbEval(opt, input);

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
    if (!loadRaw(ip, input, meta, rawImg, &load_error)) {
        return finishRun(opt, input, start, load_error, compare_stats);
    }

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
        return finishRun(opt, input, start, "Failed to save reflection_demosaic.png", compare_stats);
    }

    // 나중에 CCM 단계는 여기(Demosaic과 감마 사이)에 cv::Mat → cv::Mat 형태로 끼워 넣는다

    // 밴딩 실험: 선형 결과를 8비트로 양자화했다가 되돌려 감마에 넣는다
    cv::Mat gammaIn = opt.quantizeLinear ? QuantizeLinear(reflectiondemosaic) : reflectiondemosaic;

    // 4단계: 감마 (1을 넘는 입력도 식을 그대로 적용)
    if (kVerify) PrintGammaExpected();

    cv::Mat gamExpImg = ApplyGamma(gammaIn, GammaExp);
    cv::Mat gamsRGBImg = ApplyGamma(gammaIn, GammaSRGB);

    if (!opt.noSave && !saveRGB8(outdir, "gamExpImg.png", gamExpImg)) {
        return finishRun(opt, input, start, "Failed to save gamExpImg.png", compare_stats);
    }
    if (!opt.noSave && !saveRGB8(outdir, "gamsRGBImg.png", gamsRGBImg)) {
        return finishRun(opt, input, start, "Failed to save gamsRGBImg.png", compare_stats);
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
        // 지표는 csv에 기록할 때만 받는다 (csv가 없으면 크기가 달라도 기존처럼 비교만 건너뜀)
        LibRawCompareStats* stats = opt.csvPath.empty() ? nullptr : &compare_stats;
        if (!CompareWithLibRaw(input, outPath.string(), meta, reflectiondemosaic, gamsRGBImg,
                               !opt.noSave, stats)) {
            return finishRun(opt, input, start, "LibRaw comparison failed", compare_stats);
        }
    }

    return finishRun(opt, input, start, "", compare_stats);
}
