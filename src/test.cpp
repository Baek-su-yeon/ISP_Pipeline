#include <iostream>
#include <libraw/libraw.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <filesystem>
#include <string>
#include <algorithm>

namespace fs = std::filesystem;

bool check (int result) {
    if (result != LIBRAW_SUCCESS) {
        std::cerr << LibRaw::strerror(result) << std::endl;
        return false;
    }
    
    return true;
}

bool save (std::string outdir, std::string name, const cv::Mat& img) {
    std::string path = fs::path(outdir) / name;

    if (!cv::imwrite(path, img)) {
        std::cerr << "Failed to save: " << path << std::endl;
        return false;
    }

    return true;
}

cv::Mat printDiff (const cv::Mat& img1, const cv::Mat& img2) {
    // 비교용: 직접 구현(real)과 OpenCV convertTo(real8)의 차이
    cv::Mat diff;
    cv::absdiff(img1, img2, diff);   // 화소별 |real - real8|

    double minDiff, maxDiff;
    cv::minMaxLoc(diff, &minDiff, &maxDiff);
    int diffCount = cv::countNonZero(diff);

    std::cout << "Max diff: " << maxDiff << std::endl;
    std::cout << "Different pixels: " << diffCount << " / " << diff.total()
              << " (" << 100.0 * diffCount / diff.total() << "%)" << std::endl;

    // 차이를 눈으로 보기 위한 그림 (차이 1 → 255로 확대, 다른 화소만 흰색)
    return diff *255;
}

int main (int argc, char* argv[]) {
    
    if (argc < 2) {
        std::cerr << "Usage :" << argv[0] << " <input filename>" << std::endl;
        return 1;
    }

    std::string input = argv[1];
    std::string outdir = fs::path("output/img") / fs::path(input).stem().string();
    fs::create_directories(outdir);
    
    LibRaw ip;
    int ret = ip.open_file(input.c_str());
    if (!check(ret)) return 1;

    ret = ip.unpack();
    if (!check(ret)) return 1;

    unsigned short* rawImg = ip.imgdata.rawdata.raw_image;
    if (rawImg == nullptr) {
        std::cerr << "Raw img isn't bayer" << std::endl;
        return 1;
    }

    int rawW = ip.imgdata.sizes.raw_width;
    int realW = ip.imgdata.sizes.width;
    int realH = ip.imgdata.sizes.height;
    int top = ip.imgdata.sizes.top_margin;
    int left = ip.imgdata.sizes.left_margin;
    int WL = ip.imgdata.color.maximum;
    int BL = ip.imgdata.color.black;
    
    unsigned patternR = ip.imgdata.color.cblack[4];
    unsigned patternC = ip.imgdata.color.cblack[5];
    std::cout << "Black pattern size: " << patternR << " x " << patternC << std::endl;

    int bases[2][2];
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            bases[r][c] = BL + ip.imgdata.color.cblack[ip.COLOR(r, c)];
        }
    }

    cv::Mat normImg = cv::Mat::zeros(realH, realW, CV_32F);
    for (int r = 0; r < realH; r++) {
        for (int c = 0; c < realW; c++) {
            int pixel = (r + top) * rawW + (c + left);
            int base = bases[r % 2][c % 2];

            if (patternR && patternC) {
                int idx = (r % patternR) * patternC + (c % patternC);
                base += ip.imgdata.color.cblack[6 + idx];
            }

            // float로 바꾼 뒤 빼고 나누기 → 0~1로 자르기
            float v = (static_cast<float>(rawImg[pixel]) - base) / (WL - base);
            normImg.at<float>(r, c) = std::clamp(v, 0.0f, 1.0f);
        }
    }

    double minVal, maxVal;
    cv::minMaxLoc(normImg, &minVal, &maxVal);
    std::cout << "min: " << minVal << ", max: " << maxVal << std::endl;

    cv::Mat normImg8;
    normImg.convertTo(normImg8, CV_8UC1, 255.0);
    if(!save(outdir, "normImg.png", normImg8)) return 1;

    // 0-2 흑백 이미지와 비교 (파일 이름은 0-2에서 저장한 이름으로 바꾸기)
    std::string refPath = fs::path(outdir) / "real.png";
    cv::Mat ref = cv::imread(refPath, cv::IMREAD_GRAYSCALE);
    if (ref.empty()) {
        std::cerr << "Failed to read: " << refPath << std::endl;
        return 1;
    }
    if (ref.size() != normImg8.size()) {
        std::cerr << "Size mismatch: " << ref.size() << " vs " << normImg8.size() << std::endl;
        return 1;
    }

    cv::Mat diffImg = printDiff(normImg8, ref);
    if (!save(outdir, "diff_norm_vs_raw.png", diffImg)) return 1;

    cv::Mat mask = normImg == 1.0;
    int non_zero = cv::countNonZero(mask);
    std::cout << "Pixels at 1.0: "<< non_zero <<std::endl;

    return 0;
}