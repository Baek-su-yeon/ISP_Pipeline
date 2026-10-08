#include <iostream>
#include <libraw/libraw.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
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

// 이웃 8칸을 색별로 모아 평균. G_R·G_B는 G 하나로 합친다.
// 반환: [0] R, [1] G, [2] B
cv::Vec3f OutlineIgnoreDemosaic(int r, int c, const int (&colors)[2][2], const cv::Mat& img) {
    const int dr[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    const int dc[8] = {0, -1, -1, -1, 0, 1, 1, 1};

    int own = colors[r % 2][c % 2];
    if (own == 3) own = 1;

    float sum[3] = {0, 0, 0};
    int count[3] = {0, 0, 0};

    // 지금은 테두리를 버리므로 rr, cc가 밖으로 나가지 않음
    for (int i = 0; i < 8; i++) {
        int rr = r + dr[i];
        int cc = c + dc[i];
        int ch = colors[rr % 2][cc % 2];
        if (ch == 3) ch = 1;
        if (ch == own) continue;   // 자기 색은 자기 값만 씀 (G 위치의 대각선 G 제외)

        // 이동하는 인덱스 검증용
        // std::cout << i + 1 <<"번째 Demosaic coor r: " << rr << ", c:" << cc << ", color:" << ch << std::endl;

        sum[ch] += img.at<float>(rr, cc);
        count[ch]++;
    }

    sum[own] = img.at<float>(r, c);
    count[own] = 1;

    return cv::Vec3f(sum[0] / count[0], sum[1] / count[1], sum[2] / count[2]);
}

cv::Vec3f ReflectionDemosaic(int r, int c, const int (&colors)[2][2], const cv::Mat& img) {
    const int dr[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    const int dc[8] = {0, -1, -1, -1, 0, 1, 1, 1};

    int own = colors[r % 2][c % 2];
    if (own == 3) own = 1;

    float sum[3] = {0, 0, 0};
    int count[3] = {0, 0, 0};

    // 반사로 바뀌었으므로 배열 밖에 나가는 것 주의
    for (int i = 0; i < 8; i++) {
        int rr = r + dr[i];
        int cc = c + dc[i];
        
        // if (rr < 0) rr *= (-1);
        // else if (img.rows <= rr) rr -= 2;
        
        // if (cc < 0) cc *= (-1);
        // else if (img.cols <= cc) cc -= 2;

        // 잘못된 좌표 변환
        if (rr < 0 || img.rows <= rr) rr *= (-1);
        if (cc < 0 || img.cols <= cc) cc *= (-1);

        int ch = colors[rr % 2][cc % 2];
        if (ch == 3) ch = 1;
        if (ch == own) continue;   // 자기 색은 자기 값만 씀 (G 위치의 대각선 G 제외)

        // 이동하는 인덱스 검증용
        // std::cout << i + 1 <<"번째 Demosaic coor r: " << rr << ", c:" << cc << ", color:" << ch << std::endl;

        sum[ch] += img.at<float>(rr, cc);
        count[ch]++;
    }

    sum[own] = img.at<float>(r, c);
    count[own] = 1;

    return cv::Vec3f(sum[0] / count[0], sum[1] / count[1], sum[2] / count[2]);
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
    
    cv::Mat Demosaic = cv::Mat::zeros(realH, realW, CV_32FC3); // [0] R, [1] G, [2] B
    for (int r = 0; r < realH; r++) {
        for (int c = 0; c < realW; c++) {
            Demosaic.at<cv::Vec3f>(r, c) = ReflectionDemosaic(r, c, colors, WBClip);
        }
    }
    
    // 저장용: OpenCV는 BGR 순서로 저장하므로 바꾼 뒤 8비트로
    cv::Mat demBGR, dem8;
    cv::cvtColor(Demosaic, demBGR, cv::COLOR_RGB2BGR);
    demBGR.convertTo(dem8, CV_8UC3, 255.0);
    if (!save(outdir, "incorrect_reflection_demosaic.png", dem8)) return 1;
    // if (!save(outdir, "reflection_demosaic.png", dem8)) return 1;
    
    // 완성된 이미지 및 코드 검증용
    // std::cout << "Demosaic mean: " << cv::mean(Demosaic) << std::endl;

    // Demosaic.at<cv::Vec3f>(0, 0) = ReflectionDemosaic(0, 0, colors, WBClip);
    // Demosaic.at<cv::Vec3f>(realH - 1, realW - 1) = ReflectionDemosaic(realH - 1, realW - 1, colors, WBClip);
    
    
    // cv::Mat ch[4];
    // for (int i = 0; i < 4; i++) ch[i] = cv::Mat::zeros(realH / 2, realW / 2, CV_32F);
    
    // for (int r = 0; r < realH; r++)
    //     for (int c = 0; c < realW; c++)
    //         ch[colors[r % 2][c % 2]].at<float>(r / 2, c / 2) = WBClip.at<float>(r, c);

    // for (int i = 0; i < 4; i ++) std::cout << "WBClip ch " << i << ": " << cv::mean(ch[i]) << " "; 

    return 0;
}