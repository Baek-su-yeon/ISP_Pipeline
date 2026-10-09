#include "awb.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstddef>
#include <vector>

namespace {

// 블록 안 화소 하나라도 이 값 이상이면 포화 블록
constexpr float SAT_THRESHOLD = 0.98f;

// LibRaw 색 번호
constexpr int CH_R = 0;
constexpr int CH_G = 1;
constexpr int CH_B = 2;
constexpr int CH_G2 = 3;
constexpr double RAD_TO_DEG = 180.0 / 3.14159265358979323846;
constexpr double WP_PERCENTILE = 0.99;
constexpr double SOG_P = 6.0;
constexpr double GE_P = 6.0;

int toRgbChannel(int color)
{
    return color == CH_G2 ? CH_G : color;
}

// 2x2 패턴에 R 1개, G 2개, B 1개가 있는지 확인
bool isBayerPattern(const int (&colors)[2][2])
{
    int count[3] = {0, 0, 0};
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            const int c = toRgbChannel(colors[dy][dx]);
            if (c < CH_R || c > CH_B) return false;
            ++count[c];
        }
    }
    return count[CH_R] == 1 && count[CH_G] == 2 && count[CH_B] == 1;
}

bool isValidStats(const HalfStats& hs)
{
    return !hs.img.empty() && hs.img.type() == CV_32FC3 &&
           hs.mask.type() == CV_8UC1 && hs.mask.size() == hs.img.size();
}

}  // namespace

HalfStats computeHalfStats(const cv::Mat& norm, const int (&colors)[2][2])
{
    HalfStats hs;
    if (norm.empty() || norm.type() != CV_32FC1) return hs;
    if (!isBayerPattern(colors)) return hs;

    // 홀수 크기면 마지막 행·열은 블록을 이루지 못해 버린다
    const int half_h = norm.rows / 2;
    const int half_w = norm.cols / 2;

    hs.img = cv::Mat(half_h, half_w, CV_32FC3, cv::Scalar::all(0));
    hs.mask = cv::Mat(half_h, half_w, CV_8UC1, cv::Scalar(0));
    hs.n_blocks = half_h * half_w;

    for (int by = 0; by < half_h; ++by) {
        const float* rows[2] = {norm.ptr<float>(2 * by), norm.ptr<float>(2 * by + 1)};
        cv::Vec3f* out = hs.img.ptr<cv::Vec3f>(by);
        uchar* valid = hs.mask.ptr<uchar>(by);

        for (int bx = 0; bx < half_w; ++bx) {
            float sum[3] = {0.0f, 0.0f, 0.0f};
            int cnt[3] = {0, 0, 0};
            bool saturated = false;

            // 블록 시작이 짝수 좌표라 (2*by + dy) % 2 == dy
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const float v = rows[dy][2 * bx + dx];
                    if (v >= SAT_THRESHOLD) saturated = true;
                    const int c = toRgbChannel(colors[dy][dx]);
                    sum[c] += v;
                    ++cnt[c];
                }
            }

            // 포화 블록도 값은 채워 두고 마스크로만 제외
            out[bx] = cv::Vec3f(sum[CH_R] / cnt[CH_R],
                                sum[CH_G] / cnt[CH_G],
                                sum[CH_B] / cnt[CH_B]);

            if (saturated) {
                ++hs.n_sat_blocks;
            } else {
                valid[bx] = 1;
            }
        }
    }
    return hs;
}

std::optional<Illum> illumFromGain(const Gain& gain)
{
    if (gain.r <= 0.0 || gain.g <= 0.0 || gain.b <= 0.0) return std::nullopt;
    return Illum{1.0 / gain.r, 1.0 / gain.g, 1.0 / gain.b};
}

std::optional<Gain> illumToGain(const Illum& illum)
{
    if (illum.r <= 0.0 || illum.g <= 0.0 || illum.b <= 0.0) return std::nullopt;
    return Gain{illum.g / illum.r, 1.0, illum.g / illum.b};
}

std::optional<Illum> normalizeToG(const Illum& illum)
{
    if (illum.g <= 0.0) return std::nullopt;
    return Illum{illum.r / illum.g, 1.0, illum.b / illum.g};
}

double angularError(const Illum& est, const Illum& ref)
{
    const double dot = est.r * ref.r + est.g * ref.g + est.b * ref.b;
    const double norm_est = std::sqrt(est.r * est.r + est.g * est.g + est.b * est.b);
    const double norm_ref = std::sqrt(ref.r * ref.r + ref.g * ref.g + ref.b * ref.b);
    if (norm_est <= 0.0 || norm_ref <= 0.0) return std::numeric_limits<double>::quiet_NaN();

    // 반올림으로 1을 살짝 넘으면 acos가 NaN이 되므로 자른다
    const double cos_theta = std::clamp(dot / (norm_est * norm_ref), -1.0, 1.0);
    return std::acos(cos_theta) * RAD_TO_DEG;
}

std::optional<Illum> estimateGrayWorld(const HalfStats& hs)
{
    if (!isValidStats(hs)) return std::nullopt;

    double sum[3] = {0.0, 0.0, 0.0};
    long long n = 0;
    for (int y = 0; y < hs.img.rows; ++y) {
        const cv::Vec3f* px = hs.img.ptr<cv::Vec3f>(y);
        const uchar* valid = hs.mask.ptr<uchar>(y);
        for (int x = 0; x < hs.img.cols; ++x) {
            if (!valid[x]) continue;
            for (int c = 0; c < 3; ++c) sum[c] += px[x][c];
            ++n;
        }
    }
    if (n == 0) return std::nullopt;
    return Illum{sum[0] / n, sum[1] / n, sum[2] / n};
}

std::optional<Illum> estimateWhitePatch(const HalfStats& hs, double percentile)
{
    if (!isValidStats(hs)) return std::nullopt;
    if (percentile < 0.0 || percentile > 1.0) return std::nullopt;

    std::vector<float> vals[3];
    const std::size_t n_valid = static_cast<std::size_t>(hs.n_blocks - hs.n_sat_blocks);
    for (auto& v : vals) v.reserve(n_valid);

    for (int y = 0; y < hs.img.rows; ++y) {
        const cv::Vec3f* px = hs.img.ptr<cv::Vec3f>(y);
        const uchar* valid = hs.mask.ptr<uchar>(y);
        for (int x = 0; x < hs.img.cols; ++x) {
            if (!valid[x]) continue;
            for (int c = 0; c < 3; ++c) vals[c].push_back(px[x][c]);
        }
    }
    if (vals[0].empty()) return std::nullopt;

    // 전체 정렬 대신 k번째 값만 제자리에 놓는다 (평균 O(n))
    const std::size_t k = static_cast<std::size_t>(percentile * (vals[0].size() - 1));
    double e[3];
    for (int c = 0; c < 3; ++c) {
        std::nth_element(vals[c].begin(), vals[c].begin() + k, vals[c].end());
        e[c] = vals[c][k];
    }
    return Illum{e[0], e[1], e[2]};
}

std::optional<Illum> estimateShadesOfGray(const HalfStats& hs, double p)
{
    if (!isValidStats(hs)) return std::nullopt;
    if (p <= 0.0) return std::nullopt;

    double sum[3] = {0.0, 0.0, 0.0};
    long long n = 0;
    for (int y = 0; y < hs.img.rows; ++y) {
        const cv::Vec3f* px = hs.img.ptr<cv::Vec3f>(y);
        const uchar* valid = hs.mask.ptr<uchar>(y);
        for (int x = 0; x < hs.img.cols; ++x) {
            if (!valid[x]) continue;
            for (int c = 0; c < 3; ++c) sum[c] += std::pow(static_cast<double>(px[x][c]), p);
            ++n;
        }
    }
    if (n == 0) return std::nullopt;
    return Illum{std::pow(sum[0] / n, 1.0 / p),
                 std::pow(sum[1] / n, 1.0 / p),
                 std::pow(sum[2] / n, 1.0 / p)};
}

std::optional<Illum> estimateGrayEdge(const HalfStats& hs, double p)
{
    if (!isValidStats(hs)) return std::nullopt;
    if (p <= 0.0) return std::nullopt;

    const int h = hs.img.rows;
    const int w = hs.img.cols;

    // 1) 3x3 평균. 9칸이 모두 유효할 때만 결과를 유효로 표시
    cv::Mat blur(h, w, CV_32FC3, cv::Scalar::all(0));
    cv::Mat blur_ok(h, w, CV_8UC1, cv::Scalar(0));
    for (int y = 1; y < h - 1; ++y) {
        cv::Vec3f* out = blur.ptr<cv::Vec3f>(y);
        uchar* ok_out = blur_ok.ptr<uchar>(y);
        for (int x = 1; x < w - 1; ++x) {
            cv::Vec3f s(0.0f, 0.0f, 0.0f);
            bool ok = true;
            for (int dy = -1; dy <= 1 && ok; ++dy) {
                const cv::Vec3f* px = hs.img.ptr<cv::Vec3f>(y + dy);
                const uchar* valid = hs.mask.ptr<uchar>(y + dy);
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!valid[x + dx]) { ok = false; break; }
                    s += px[x + dx];
                }
            }
            if (!ok) continue;
            out[x] = s / 9.0f;
            ok_out[x] = 1;
        }
    }

    // 2) 가로·세로 [-1, 0, 1] 차분. 참조하는 4칸이 모두 유효할 때만 사용
    double sum[3] = {0.0, 0.0, 0.0};
    long long n = 0;
    for (int y = 1; y < h - 1; ++y) {
        const cv::Vec3f* up = blur.ptr<cv::Vec3f>(y - 1);
        const cv::Vec3f* mid = blur.ptr<cv::Vec3f>(y);
        const cv::Vec3f* down = blur.ptr<cv::Vec3f>(y + 1);
        const uchar* ok_up = blur_ok.ptr<uchar>(y - 1);
        const uchar* ok_mid = blur_ok.ptr<uchar>(y);
        const uchar* ok_down = blur_ok.ptr<uchar>(y + 1);
        for (int x = 1; x < w - 1; ++x) {
            if (!ok_mid[x - 1] || !ok_mid[x + 1] || !ok_up[x] || !ok_down[x]) continue;
            for (int c = 0; c < 3; ++c) {
                const double dx = static_cast<double>(mid[x + 1][c]) - mid[x - 1][c];
                const double dy = static_cast<double>(down[x][c]) - up[x][c];
                sum[c] += std::pow(std::sqrt(dx * dx + dy * dy), p);
            }
            ++n;
        }
    }
    if (n == 0) return std::nullopt;
    return Illum{std::pow(sum[0] / n, 1.0 / p),
                 std::pow(sum[1] / n, 1.0 / p),
                 std::pow(sum[2] / n, 1.0 / p)};
}

std::optional<Illum> estimateIllum(const HalfStats& hs, AwbMethod method)
{
    switch (method) {
    case AwbMethod::GW:  return estimateGrayWorld(hs);
    case AwbMethod::WP:  return estimateWhitePatch(hs, WP_PERCENTILE);
    case AwbMethod::SOG: return estimateShadesOfGray(hs, SOG_P);
    case AwbMethod::GE:  return estimateGrayEdge(hs, GE_P);
    }
    return std::nullopt;
}