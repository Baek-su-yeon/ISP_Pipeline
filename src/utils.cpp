#include "utils.h"

#include <iostream>
#include <filesystem>
#include <vector>
#include <libraw/libraw.h>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

namespace fs = std::filesystem;

bool check (int result) {
    if (result != LIBRAW_SUCCESS) {
        std::cerr << LibRaw::strerror(result) << std::endl;
        return false;
    }

    return true;
}

bool save (const std::string& outdir, const std::string& name, const cv::Mat& img) {
    std::string path = fs::path(outdir) / name;

    if (!cv::imwrite(path, img)) {
        std::cerr << "Failed to save: " << path << std::endl;
        return false;
    }

    return true;
}

bool printDiff (const cv::Mat& img1, const cv::Mat& img2, cv::Mat& diffVis,
                int border, double visScale) {
    if (img1.size() != img2.size() || img1.type() != img2.type()) {
        std::cerr << "printDiff: size/type mismatch: "
                  << img1.cols << "x" << img1.rows << " type " << img1.type() << " vs "
                  << img2.cols << "x" << img2.rows << " type " << img2.type() << std::endl;
        return false;
    }

    int depth = img1.depth();
    if (depth != CV_8U && depth != CV_16U) {
        std::cerr << "printDiff: only CV_8U and CV_16U are supported (type " << img1.type() << ")" << std::endl;
        return false;
    }

    if (border < 0 || 2 * border >= img1.rows || 2 * border >= img1.cols) {
        std::cerr << "printDiff: invalid border " << border << " for "
                  << img1.cols << "x" << img1.rows << std::endl;
        return false;
    }

    cv::Mat fullDiff;
    cv::absdiff(img1, img2, fullDiff);

    // 차이를 눈으로 보기 위한 그림: |차이| × visScale, 255에서 포화 (채널 수는 그대로)
    fullDiff.convertTo(diffVis, CV_8U, visScale);

    // 통계는 바깥 border 화소를 뺀 영역만
    cv::Mat diff = fullDiff(cv::Rect(border, border, img1.cols - 2 * border, img1.rows - 2 * border));
    const long long total = static_cast<long long>(diff.rows) * diff.cols;

    std::vector<cv::Mat> planes;
    cv::split(diff, planes);

    std::vector<double> maxDiff, meanDiff;
    for (const auto& p : planes) {
        double mn, mx;
        cv::minMaxLoc(p, &mn, &mx);
        maxDiff.push_back(mx);
        meanDiff.push_back(cv::mean(p)[0]);
    }

    // 화소별 채널 최대 차이: 어느 채널이든 차이가 있는지, 크기가 얼마인지
    cv::Mat pixMax = planes[0].clone();
    for (size_t k = 1; k < planes.size(); k++) cv::max(pixMax, planes[k], pixMax);

    auto countGE = [&](double t) {
        cv::Mat m;
        cv::compare(pixMax, t, m, cv::CMP_GE);
        return static_cast<long long>(cv::countNonZero(m));
    };

    const long long nonZero = countGE(1);
    long long bins[4];
    const char* names[4];
    if (depth == CV_8U) {
        long long ge2 = countGE(2), ge3 = countGE(3);
        bins[0] = total - nonZero; bins[1] = nonZero - ge2; bins[2] = ge2 - ge3; bins[3] = ge3;
        names[0] = "0"; names[1] = "1"; names[2] = "2"; names[3] = "3+";
    } else {
        long long ge17 = countGE(17), ge257 = countGE(257);
        bins[0] = total - nonZero; bins[1] = nonZero - ge17; bins[2] = ge17 - ge257; bins[3] = ge257;
        names[0] = "0"; names[1] = "1~16"; names[2] = "17~256"; names[3] = "257+";
    }

    std::ios::fmtflags oldFlags = std::cout.flags();
    std::streamsize oldPrec = std::cout.precision(8);

    std::cout << "  diff (" << (depth == CV_8U ? "8" : "16") << "-bit, " << img1.channels() << "ch, border " << border
              << ", compared " << diff.cols << "x" << diff.rows << ")" << std::endl;

    std::cout << "    max diff per channel      :";
    for (double v : maxDiff) std::cout << " " << v;
    std::cout << std::endl;

    std::cout << "    mean abs diff per channel :";
    for (double v : meanDiff) std::cout << " " << v;
    std::cout << std::endl;

    std::cout << "    pixels with any-channel diff: " << nonZero << " / " << total
              << " (" << 100.0 * nonZero / total << "%)" << std::endl;

    std::cout << "    pixels by max channel diff  :";
    for (int i = 0; i < 4; i++) std::cout << " [" << names[i] << "] " << bins[i];
    std::cout << std::endl;

    std::cout.flags(oldFlags);
    std::cout.precision(oldPrec);

    return true;
}

bool saveRGB8 (const std::string& outdir, const std::string& name, const cv::Mat& rgb32f) {
    cv::Mat bgr, img8;
    cv::cvtColor(rgb32f, bgr, cv::COLOR_RGB2BGR);
    bgr.convertTo(img8, CV_8UC3, 255.0);
    return save(outdir, name, img8);
}
