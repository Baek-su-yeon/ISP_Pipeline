#include "experiments.h"

#include <iostream>
#include <utility>
#include <vector>

void SwapRBColors(int (&colors)[2][2]) {
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            if (colors[r][c] == 0) colors[r][c] = 2;
            else if (colors[r][c] == 2) colors[r][c] = 0;
            else if (colors[r][c] == 1) colors[r][c] = 3;
            else if (colors[r][c] == 3) colors[r][c] = 1;
        }
    }
}

void SwapRB(RawMeta& meta) {
    // R 위치와 B 위치를 찾는다 (2×2 안에 하나씩)
    int rr = -1, rc = -1, br = -1, bc = -1;
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            if (meta.colors[r][c] == 0) { rr = r; rc = c; }
            if (meta.colors[r][c] == 2) { br = r; bc = c; }
        }
    }

    if (rr >= 0 && br >= 0) {
        std::swap(meta.bases[rr][rc], meta.bases[br][bc]);
        std::swap(meta.gains[rr][rc], meta.gains[br][bc]);
    }

    SwapRBColors(meta.colors);
}

void ZeroBlack(RawMeta& meta) {
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            meta.bases[r][c] = 0;
        }
    }

    // 위치 패턴 값도 더하지 않는다
    meta.patternR = 0;
    meta.patternC = 0;
    meta.cblackPattern.clear();
}

cv::Mat QuantizeLinear(const cv::Mat& linear) {
    cv::Mat q8, q;
    linear.convertTo(q8, CV_8UC3, 255.0);
    q8.convertTo(q, CV_32FC3, 1.0 / 255.0);
    return q;
}

void PrintChannelMean(const char* label, const cv::Mat& img3) {
    cv::Scalar m = cv::mean(img3);
    std::streamsize old = std::cout.precision(10);
    std::cout << label << " (R, G, B): " << m[0] << ", " << m[1] << ", " << m[2] << std::endl;
    std::cout.precision(old);
}

void PrintChannelMax(const char* label, const cv::Mat& img3) {
    std::vector<cv::Mat> ch;
    cv::split(img3, ch);

    double mx[3];
    for (int k = 0; k < 3; k++) {
        double mn;
        cv::minMaxLoc(ch[k], &mn, &mx[k]);
    }

    std::streamsize old = std::cout.precision(10);
    std::cout << label << " (R, G, B): " << mx[0] << ", " << mx[1] << ", " << mx[2] << std::endl;
    std::cout.precision(old);
}

void PrintSwapCheck(const cv::Mat& swapped, const cv::Mat& normal) {
    cv::Scalar ms = cv::mean(swapped);
    cv::Scalar mn = cv::mean(normal);

    // swapped의 R·G·B가 normal의 B·G·R과 같아야 한다
    const int pair[3] = {2, 1, 0};

    std::vector<cv::Mat> sc, nc;
    cv::split(swapped, sc);
    cv::split(normal, nc);

    bool exact = true;
    double maxDiff[3];
    for (int k = 0; k < 3; k++) {
        if (ms[k] != mn[pair[k]]) exact = false;

        cv::Mat diff;
        cv::absdiff(sc[k], nc[pair[k]], diff);
        double mnv;
        cv::minMaxLoc(diff, &mnv, &maxDiff[k]);
    }

    std::streamsize old = std::cout.precision(10);
    std::cout << "swap check | normal  Demosaic mean (R, G, B): " << mn[0] << ", " << mn[1] << ", " << mn[2] << std::endl;
    std::cout << "swap check | swapped Demosaic mean (R, G, B): " << ms[0] << ", " << ms[1] << ", " << ms[2] << std::endl;
    std::cout << "swap check | swapped R/G/B == normal B/G/R, means exactly equal: " << (exact ? "yes" : "no")
              << " | max pixel diff (R-B, G-G, B-R): " << maxDiff[0] << ", " << maxDiff[1] << ", " << maxDiff[2] << std::endl;
    std::cout.precision(old);
}

void PrintDistinctCounts(const char* label, const cv::Mat& linearIn, const cv::Mat& gammaImg, double maxIn) {
    cv::Mat gamma8;
    gammaImg.convertTo(gamma8, CV_8UC3, 255.0);

    long long pixels[3] = {0, 0, 0};
    int distinct[3] = {0, 0, 0};

    for (int k = 0; k < 3; k++) {
        bool seen[256] = {};
        for (int r = 0; r < linearIn.rows; r++) {
            for (int c = 0; c < linearIn.cols; c++) {
                float x = linearIn.at<cv::Vec3f>(r, c)[k];
                if (x < 0.0f || x > maxIn) continue;

                pixels[k]++;
                unsigned char v = gamma8.at<cv::Vec3b>(r, c)[k];
                if (!seen[v]) { seen[v] = true; distinct[k]++; }
            }
        }
    }

    std::cout << label << " | distinct 8-bit values (R, G, B): " << distinct[0] << ", " << distinct[1] << ", " << distinct[2]
              << " | pixels in range (R, G, B): " << pixels[0] << ", " << pixels[1] << ", " << pixels[2] << std::endl;
}
