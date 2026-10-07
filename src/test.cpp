#include <vector>
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
    cv::Mat diff;
    cv::absdiff(img1, img2, diff);

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
    
    int patternR = ip.imgdata.color.cblack[4];
    int patternC = ip.imgdata.color.cblack[5];

    int colors[2][2];
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            char nowC = ip.imgdata.idata.cdesc[ip.COLOR(r, c)];
            char nearC = ip.imgdata.idata.cdesc[ip.COLOR(r, 1 - c)];

            if (nowC == 'R') colors[r][c] = 0;
            else if (nowC == 'B') colors[r][c] = 2;
            else colors[r][c] = (nearC == 'R') ? 1 : 3;
        }
    }

    int bases[2][2];
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            bases[r][c] = BL + ip.imgdata.color.cblack[ip.COLOR(r, c)];
        }
    }

    float gains[2][2];
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {

            int colorIdx = ip.COLOR(r, c);

            if (colorIdx == 3) colorIdx = 1;

            gains[r][c] = ip.imgdata.color.cam_mul[colorIdx] / ip.imgdata.color.cam_mul[1];
        }
    }

    cv::Mat normImg = cv::Mat::zeros(realH, realW, CV_32F);
    for (int r = 0; r < realH; r++) {
        for (int c = 0; c < realW; c++) {
            int pixel = (r + top) * rawW + (c + left);
            int base = bases[r % 2][c % 2];

            if (patternR && patternC) {
                int patIdx = (r % patternR) * patternC + (c % patternC);
                base += ip.imgdata.color.cblack[6 + patIdx];
            }

            // float로 바꾼 뒤 빼고 나누기 → 0~1로 자르기
            float v = (static_cast<float>(rawImg[pixel]) - base) / (WL - base);
            normImg.at<float>(r, c) = std::clamp(v, 0.0f, 1.0f);
        }
    }


    cv::Mat WBImg = cv::Mat::zeros(realH, realW, CV_32F);
    for (int r = 0; r < realH; r++) {
        for (int c = 0; c < realW; c++) {
            float wb = normImg.at<float>(r, c) * gains[r % 2][c % 2];
            WBImg.at<float>(r, c) = wb;
        }
    }


    cv::Mat WBClip;
    cv::min(WBImg, 1.0f, WBClip);

    
    cv::Mat Demosaic = cv::Mat::zeros(realH, realW, CV_32FC3); // RGB로 저장 나중에 변환
    for (int r = 1; r < realH - 1; r++) {
        for (int c = 1; c < realW - 1; c++) {
            int color = colors[r % 2][c % 2];

            if (color == 0) {
                // R
                for (int i = 0; i < 3; i++) {
                    if(i == color) Demosaic.at<cv::Vec3f>[i](r, c) = WBClip.at<float>(r, c);
                } 
            }
            else if (color == 1) {
                // G_R

            }
            else if (color == 2) {
                // B
            }
            else {
                // G_B

            }
        }
    }

    return 0;
}