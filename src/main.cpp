#include <iostream>
#include <filesystem>
#include <string>
#include <libraw/libraw.h>
#include <opencv2/core.hpp>

#include "raw_loader.h"
#include "black_level.h"
#include "white_balance.h"
#include "demosaic.h"
#include "gamma.h"
#include "verify.h"
#include "utils.h"

namespace fs = std::filesystem;

// 검증용 콘솔 출력 켜기/끄기 (기본: 켜짐)
constexpr bool kVerify = true;

int main (int argc, char* argv[]) {

    if (argc < 2) {
        std::cerr << "Usage :" << argv[0] << " <input filename>" << std::endl;
        return 1;
    }

    std::string input = argv[1];
    std::string outdir = fs::path("output/img") / fs::path(input).stem().string();
    fs::create_directories(outdir);

    // rawImg는 ip가 살아 있는 동안만 유효하므로 main 끝까지 유지
    LibRaw ip;
    RawMeta meta;
    const unsigned short* rawImg = nullptr;
    if (!loadRaw(ip, input, meta, rawImg)) return 1;

    // 1단계: Black Level 보정·정규화 (CV_32F, 0~1)
    cv::Mat normImg = NormalizeBlackLevel(rawImg, meta);

    // 2단계: White Balance. wb.img(WBImg)는 자르기 전, wb.clip(WBClip)은 1에서 자른 것
    // 실험 3(WBImg로 Demosaic부터 다시)은 DemosaicReflection(wb.img, ...)로 하면 된다
    WBResult wb = WhiteBalance(normImg, meta.gains);

    // 3단계: Demosaic (CV_32FC3, 내부는 RGB: [0] R, [1] G, [2] B)
    cv::Mat reflectiondemosaic = DemosaicReflection(wb.clip, meta.colors);
    if (!saveRGB8(outdir, "reflection_demosaic.png", reflectiondemosaic)) return 1;

    // 나중에 CCM 단계는 여기(Demosaic과 감마 사이)에 cv::Mat → cv::Mat 형태로 끼워 넣는다

    // 4단계: 감마
    if (kVerify) PrintGammaExpected();

    cv::Mat gamExpImg = ApplyGamma(reflectiondemosaic, GammaExp);
    cv::Mat gamsRGBImg = ApplyGamma(reflectiondemosaic, GammaSRGB);

    if (!saveRGB8(outdir, "gamExpImg.png", gamExpImg)) return 1;
    if (!saveRGB8(outdir, "gamsRGBImg.png", gamsRGBImg)) return 1;

    if (kVerify) PrintGammaRegions(reflectiondemosaic, gamExpImg, gamsRGBImg);

    return 0;
}
