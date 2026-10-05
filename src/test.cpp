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

    // 색 번호 확인
    int colors[2][2];
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            char me = ip.imgdata.idata.cdesc[ip.COLOR(r, c)];
            char nb = ip.imgdata.idata.cdesc[ip.COLOR(r, 1 - c)];   // 같은 행 옆 칸

            if (me == 'R')      colors[r][c] = 0;
            else if (me == 'B') colors[r][c] = 2;
            else                colors[r][c] = (nb == 'R') ? 1 : 3;  // R 행 G : B 행 G
            
            std::cout << colors[r][c] << " ";
        }
        std::cout << std::endl;
    }

    cv::Mat ch[4];
    for (int i = 0; i < 4; i++) ch[i] = cv::Mat::zeros(realH / 2, realW / 2, CV_8UC1);
    
    for (int r = 0; r < realH; r++){
        for (int c = 0; c < realW; c++){
            int idx = (r + top) * rawW + (c + left);
            int color = colors[r % 2][c % 2];

            ch[color].at<uchar>(r / 2, c / 2) = cv::saturate_cast<uchar>(rawImg[idx] * 255 / WL);
        }
    }

    const std::string names[4] = {"R", "G_R", "B", "G_B"};
    for (int i = 0; i < 4; i++) {
        std::string name = names[i] + ".png";
        if(!save(outdir, name, ch[i])) return 1;
    }

    // 0-4: 포화 확인 (8비트 값이 아니라 원래 raw 값 기준)
    cv::Mat satMask = cv::Mat::zeros(realH, realW, CV_8UC1);  // 포화 위치만 흰색
    int satCount[4]  = {0, 0, 0, 0};   // 값 >= WL
    int overCount[4] = {0, 0, 0, 0};   // 값 > WL (White Level을 넘는 값)
    int maxVal = 0;                    // 실제 raw 최댓값

    for (int r = 0; r < realH; r++){
        for (int c = 0; c < realW; c++){
            int idx = (r + top) * rawW + (c + left);
            int color = colors[r % 2][c % 2];
            int v = rawImg[idx];

            ch[color].at<uchar>(r / 2, c / 2) = cv::saturate_cast<uchar>(v * 255 / WL);

            if (v >= WL) {
                satCount[color]++;
                satMask.at<uchar>(r, c) = 255;
            }
            if (v > WL) overCount[color]++;
            if (v > maxVal) maxVal = v;
        }
    }
    
    // 최댓값에 몰린 화소 수 (실제 포화점 확인용)
    int atMax[4] = {0, 0, 0, 0};
    for (int r = 0; r < realH; r++){
        for (int c = 0; c < realW; c++){
            int idx = (r + top) * rawW + (c + left);
            if (rawImg[idx] >= maxVal) atMax[colors[r % 2][c % 2]]++;
        }
    }

    for (int i = 0; i < 4; i++) {
        std::cout << names[i] << " at max (" << maxVal << "): " << atMax[i] << std::endl;
    }

    for (int i = 0; i < 4; i++) {
        if (!save(outdir, names[i] + ".png", ch[i])) return 1;
    }

    // 포화 통계 출력
    int chTotal = (realH / 2) * (realW / 2);   // 채널 하나의 화소 수
    std::cout << "White level: " << WL << ", max raw value: " << maxVal << std::endl;
    for (int i = 0; i < 4; i++) {
        std::cout << names[i] << " saturated: " << satCount[i] << " / " << chTotal
                  << " (" << 100.0 * satCount[i] / chTotal << "%)"
                  << ", over WL: " << overCount[i] << std::endl;
    }

    if (!save(outdir, "sat_mask.png", satMask)) return 1;

    return 0;
}