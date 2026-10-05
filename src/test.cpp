#include <iostream>
#include <libraw/libraw.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <filesystem>
#include <string>

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
    if (argc < 2){
        std::cerr << "Usage :" << argv[0] << " <input filename>" << std::endl;
        return 1;
    }

    std::string input = argv[1];
    std::string imgname = fs::path(input).stem().string();
    std::string outdir = fs::path("output/img") / imgname;
    fs::create_directories(outdir);

    LibRaw ip;

    int ret = ip.open_file(input.c_str());
    if (!check(ret)) return 1; 

    ret = ip.unpack();
    if (!check(ret)) return 1; 

    unsigned short* rawImg = ip.imgdata.rawdata.raw_image;
    if (rawImg == nullptr){
        std::cerr << "Raw Img isn't Bayer" << std::endl;
        return 1;
    }

    int rawW = ip.imgdata.sizes.raw_width;
    int rawH = ip.imgdata.sizes.raw_height;
    int realW = ip.imgdata.sizes.width;
    int realH = ip.imgdata.sizes.height;
    int top = ip.imgdata.sizes.top_margin;
    int left = ip.imgdata.sizes.left_margin;
    int WL = ip.imgdata.color.maximum;

    // 비교해보고 싶어서 만든 것
    // 256으로 나눈 나머지만 남아 등고선 무늬가 생김
    cv::Mat raw_noWL(rawH, rawW, CV_8UC1);
    for (int r = 0; r < rawH; r++){
        for(int c = 0; c < rawW; c++){
            int idx = r * rawW + c;
            raw_noWL.at<uchar>(r, c) = rawImg[idx];
        }
    }

    if(!save(outdir, "raw_noWL.png", raw_noWL)) return 1;

    // 마진을 포함한 이미지 출력
    cv::Mat raw(rawH, rawW, CV_8UC1);
    for (int r = 0; r < rawH; r++){
        for(int c = 0; c < rawW; c++){
            int idx = r * rawW + c;
            raw.at<uchar>(r, c) = cv::saturate_cast<uchar>(rawImg[idx] * 255 / WL);
        }
    }

    if(!save(outdir, "raw.png", raw)) return 1;

    // 여백 없이 값 나오고, 그래서 실제 이미지 속성 자세히 보면 w*h 다름
    cv::Mat real(realH, realW, CV_8UC1);
    for (int r = 0; r < realH; r++){
        for (int c = 0; c < realW; c++){
            int idx = (r + top) * rawW + (c + left);
            real.at<uchar>(r, c) = cv::saturate_cast<uchar>(rawImg[idx] * 255 / WL);
        }
    }

    if(!save(outdir, "real.png", real)) return 1;

    // 참고용, for문 없이 원래됨
    // raw_image를 복사하지 않고 Mat으로 감싸기 (여백 포함 전체)
    cv::Mat full16(rawH, rawW, CV_16UC1, rawImg);

    // 곱하기·나누기·잘라내기·여백빼기를 한 번에
    cv::Mat real16 = full16(cv::Rect(left, top, realW, realH));
    cv::Mat real8;
    real16.convertTo(real8, CV_8U, 255.0 / WL);

    if(!save(outdir, "real_usingfunc.png", real8)) return 1;

    cv::Mat diff = printDiff(real, real8);
    if(!save(outdir, "diff_real_vs_func.png", diff)) return 1;

    return 0;
}