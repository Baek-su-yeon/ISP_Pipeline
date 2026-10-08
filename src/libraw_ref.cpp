#include "libraw_ref.h"

#include <iostream>
#include <iomanip>
#include <filesystem>
#include <libraw/libraw.h>
#include <opencv2/imgproc.hpp>
#include "utils.h"

namespace fs = std::filesystem;

LibRawRefParams LibRawRefA1() {
    LibRawRefParams p;
    p.matchConditions = true;
    p.gamm0 = 1.0;
    p.gamm1 = 1.0;
    p.outputBps = 16;
    return p;
}

LibRawRefParams LibRawRefA2() {
    LibRawRefParams p = LibRawRefA1();
    p.gamm0 = 1.0 / 2.4;
    p.gamm1 = 12.92;
    p.outputBps = 8;
    return p;
}

LibRawRefParams LibRawRefB() {
    LibRawRefParams p;
    p.matchConditions = false;
    return p;
}

static LibRawValues ReadValues(const LibRaw& ip) {
    LibRawValues v;
    for (int i = 0; i < 4; i++) {
        v.camMul[i] = ip.imgdata.color.cam_mul[i];
        v.preMul[i] = ip.imgdata.color.pre_mul[i];
        v.cblack[i] = ip.imgdata.color.cblack[i];
    }
    v.black = ip.imgdata.color.black;
    v.maximum = ip.imgdata.color.maximum;
    return v;
}

bool MakeLibRawRef(const std::string& path, const LibRawRefParams& params, LibRawRef& out) {
    LibRaw ip;
    if (!check(ip.open_file(path.c_str()))) return false;
    if (!check(ip.unpack())) return false;

    out.afterUnpack = ReadValues(ip);

    libraw_output_params_t& O = ip.imgdata.params;
    if (params.matchConditions) {
        O.output_color = 0;          // CCM 없음 (raw color)
        O.no_auto_bright = 1;
        O.gamm[0] = params.gamm0;
        O.gamm[1] = params.gamm1;
        O.output_bps = params.outputBps;
        O.use_camera_wb = 1;         // as-shot cam_mul
        O.user_qual = 0;             // linear 보간
        O.highlight = 0;             // clip
        O.adjust_maximum_thr = 0;    // 최댓값 자동 조정 끄기
        O.user_flip = 0;             // 회전 안 함
        O.half_size = 0;
        O.four_color_rgb = 0;
        O.med_passes = 0;
    } else {
        O.user_flip = 0;             // 크기 맞추기 위해서만
    }

    if (!check(ip.dcraw_process())) return false;

    out.afterProcess = ReadValues(ip);

    int errc = 0;
    libraw_processed_image_t* img = ip.dcraw_make_mem_image(&errc);
    if (img == nullptr) {
        check(errc);
        std::cerr << "dcraw_make_mem_image failed" << std::endl;
        return false;
    }

    bool ok = true;
    if (img->type != LIBRAW_IMAGE_BITMAP || img->colors != 3 || (img->bits != 8 && img->bits != 16)) {
        std::cerr << "Unexpected LibRaw image: type " << img->type << ", colors " << img->colors
                  << ", bits " << img->bits << std::endl;
        ok = false;
    } else {
        // 결과는 RGB 순서. LibRaw 메모리를 해제하기 전에 복사한다
        int type = (img->bits == 16) ? CV_16UC3 : CV_8UC3;
        out.rgb = cv::Mat(img->height, img->width, type, img->data).clone();
    }

    LibRaw::dcraw_clear_mem(img);
    return ok;
}

// 저장 시 RGB → BGR (8·16비트 모두)
static bool SaveRGBAny(const std::string& dir, const std::string& name, const cv::Mat& rgb) {
    cv::Mat bgr;
    cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
    return save(dir, name, bgr);
}

static void PrintValues(const char* label, const LibRawValues& v) {
    std::cout << "  " << label << std::endl;
    std::cout << "    cam_mul : " << v.camMul[0] << " " << v.camMul[1] << " " << v.camMul[2] << " " << v.camMul[3] << std::endl;
    std::cout << "    pre_mul : " << v.preMul[0] << " " << v.preMul[1] << " " << v.preMul[2] << " " << v.preMul[3] << std::endl;
    std::cout << "    black   : " << v.black << " (cblack[0..3]: " << v.cblack[0] << " " << v.cblack[1] << " "
              << v.cblack[2] << " " << v.cblack[3] << ")" << std::endl;
    std::cout << "    maximum : " << v.maximum << std::endl;
}

static void PrintMyValues(const RawMeta& meta) {
    std::cout << "My values (RawMeta)" << std::endl;
    std::cout << "  BL: " << meta.BL << ", WL: " << meta.WL << std::endl;
    std::cout << "  bases (r, c): " << meta.bases[0][0] << " " << meta.bases[0][1] << " / "
              << meta.bases[1][0] << " " << meta.bases[1][1] << std::endl;
    std::cout << "  gains (r, c): " << meta.gains[0][0] << " " << meta.gains[0][1] << " / "
              << meta.gains[1][0] << " " << meta.gains[1][1] << std::endl;
    std::cout << "  colors (r, c): " << meta.colors[0][0] << " " << meta.colors[0][1] << " / "
              << meta.colors[1][0] << " " << meta.colors[1][1] << std::endl;
}

bool CompareWithLibRaw(const std::string& input, const std::string& outdir, const RawMeta& meta,
                       const cv::Mat& myLinear, const cv::Mat& myGamsRGB) {
    const std::string libDir = (fs::path(outdir) / "libraw").string();
    fs::create_directories(libDir);

    // 내 결과를 LibRaw와 같은 형식으로
    cv::Mat my16, my8;
    myLinear.convertTo(my16, CV_16UC3, 65535.0);   // × 65535, 반올림
    myGamsRGB.convertTo(my8, CV_8UC3, 255.0);      // 저장할 때와 같은 변환

    std::cout << "=== Compare with LibRaw (" << input << ") ===" << std::endl;
    std::cout << "My size: " << meta.realW << " x " << meta.realH << " (W x H)" << std::endl;
    PrintMyValues(meta);

    struct Item {
        const char* tag;
        const char* title;
        LibRawRefParams params;
        const char* imgName;
        const char* diffName;
        const cv::Mat* mine;
        const char* mineDesc;
        double visScale;
    };

    const Item items[] = {
        {"A1", "A1 linear 16-bit (conditions matched)", LibRawRefA1(),
         "libraw_A1_linear16.png", "diff_A1.png", &my16, "my Demosaic (float x 65535, rounded)", 16.0},
        {"A2", "A2 sRGB 8-bit (conditions matched)", LibRawRefA2(),
         "libraw_A2_srgb8.png", "diff_A2.png", &my8, "my gamsRGB (8-bit)", 64.0},
        {"B", "B LibRaw defaults (user_flip = 0 only), reference only", LibRawRefB(),
         "libraw_B_default.png", "diff_B.png", &my8, "my gamsRGB (8-bit)", 64.0},
    };

    std::ios::fmtflags oldFlags = std::cout.flags();
    std::streamsize oldPrec = std::cout.precision(8);

    for (const auto& it : items) {
        std::cout << "--- " << it.title << " ---" << std::endl;

        LibRawRef ref;
        if (!MakeLibRawRef(input, it.params, ref)) return false;

        PrintValues("LibRaw values after unpack", ref.afterUnpack);
        PrintValues("LibRaw values after dcraw_process (used)", ref.afterProcess);

        std::cout << "  LibRaw size: " << ref.rgb.cols << " x " << ref.rgb.rows << " (W x H), "
                  << (ref.rgb.depth() == CV_16U ? 16 : 8) << "-bit" << std::endl;

        if (!SaveRGBAny(libDir, it.imgName, ref.rgb)) return false;

        if (ref.rgb.rows != meta.realH || ref.rgb.cols != meta.realW) {
            std::cout << "  size mismatch: LibRaw " << ref.rgb.cols << " x " << ref.rgb.rows
                      << " vs mine " << meta.realW << " x " << meta.realH << " -> not compared" << std::endl;
            continue;
        }

        std::cout << "  compare: LibRaw " << it.tag << " vs " << it.mineDesc << std::endl;

        cv::Mat vis;
        for (int border : {2, 0}) {
            if (!printDiff(ref.rgb, *it.mine, vis, border, it.visScale)) return false;
        }

        if (!save(libDir, it.diffName, vis)) return false;
    }

    std::cout.flags(oldFlags);
    std::cout.precision(oldPrec);

    return true;
}
