#include "utils.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <filesystem>
#include <sstream>
#include <vector>
#include <libraw/libraw.h>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include "raw_loader.h"

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

std::string photoId(const std::string& path) {
    const fs::path p(path);
    const std::string name = p.filename().string();
    const size_t dash = name.find('-');
    return dash == std::string::npos ? p.stem().string() : name.substr(0, dash);
}

std::string csvNum(double v) {
    std::ostringstream os;
    os << std::setprecision(8) << v;
    return os.str();
}

CsvRow baseCsvRow(const std::string& id, const std::string& status,
                  const std::string& error, long long time_ms) {
    return {{"id", id}, {"status", status}, {"error", error}, {"time_ms", std::to_string(time_ms)}};
}

// 쉼표·따옴표·줄바꿈이 있으면 큰따옴표로 감싼다 (안의 큰따옴표는 두 번)
static std::string csvField(const std::string& s) {
    if (s.find_first_of(",\"\r\n") == std::string::npos) return s;

    std::string out = "\"";
    for (char ch : s) {
        if (ch == '"') out += '"';
        out += ch;
    }
    return out + "\"";
}

static std::string csvLine(const CsvRow& row, bool names) {
    std::string line;
    for (const auto& col : row) {
        if (!line.empty()) line += ',';
        line += csvField(names ? col.first : col.second);
    }
    return line;
}

bool appendCsvRow(const std::string& csv_path, const CsvRow& row) {
    const std::string header = csvLine(row, true);

    bool need_header = true;
    if (fs::exists(csv_path) && fs::file_size(csv_path) > 0) {
        std::ifstream in(csv_path);
        std::string first;
        std::getline(in, first);
        if (!first.empty() && first.back() == '\r') first.pop_back();
        if (first != header) {
            std::cerr << "csv header mismatch: " << csv_path << std::endl;
            return false;
        }
        need_header = false;
    }

    const fs::path parent = fs::path(csv_path).parent_path();
    if (!parent.empty()) fs::create_directories(parent);

    std::ofstream out(csv_path, std::ios::app);
    if (!out) {
        std::cerr << "Failed to open csv: " << csv_path << std::endl;
        return false;
    }

    // 헤더와 한 줄을 한 번에 쓴다 (중간에 끊겨도 반쪽 줄이 남지 않게)
    out << (need_header ? header + "\n" : "") << csvLine(row, false) << "\n";
    out.flush();
    if (!out) {
        std::cerr << "Failed to write csv: " << csv_path << std::endl;
        return false;
    }

    return true;
}

CsvRow metaCsvRow(const CsvRow& common, const std::string& path, const RawInfo& info) {
    CsvRow row = common;
    row.emplace_back("path", path);

    // 읽기에 실패한 줄은 값을 비운다 (열은 항상 같다)
    auto add = [&](const char* name, const std::string& value) {
        row.emplace_back(name, info.ok ? value : "");
    };

    add("make", info.make);
    add("model", info.model);
    add("iso", csvNum(info.iso));
    add("pattern", info.pattern);
    add("black", std::to_string(info.black));
    for (int i = 0; i < 4; i++) add(("cblack" + std::to_string(i)).c_str(), std::to_string(info.cblack[i]));
    add("cblack_h", std::to_string(info.cblack[4]));
    add("cblack_w", std::to_string(info.cblack[5]));
    add("white", std::to_string(info.white));
    add("cam_mul_r", csvNum(info.cam_mul[0]));
    add("cam_mul_g", csvNum(info.cam_mul[1]));
    add("cam_mul_b", csvNum(info.cam_mul[2]));
    add("cam_mul_g2", csvNum(info.cam_mul[3]));
    add("raw_w", std::to_string(info.raw_w));
    add("raw_h", std::to_string(info.raw_h));
    add("w", std::to_string(info.w));
    add("h", std::to_string(info.h));
    add("top_margin", std::to_string(info.top_margin));
    add("left_margin", std::to_string(info.left_margin));
    add("colors", std::to_string(info.colors));
    add("filters", std::to_string(info.filters));

    return row;
}

bool saveRGB8 (const std::string& outdir, const std::string& name, const cv::Mat& rgb32f) {
    cv::Mat bgr, img8;
    cv::cvtColor(rgb32f, bgr, cv::COLOR_RGB2BGR);
    bgr.convertTo(img8, CV_8UC3, 255.0);
    return save(outdir, name, img8);
}
