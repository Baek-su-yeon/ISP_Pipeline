#include "awb.h"

namespace {

// 블록 안 화소 하나라도 이 값 이상이면 포화 블록
constexpr float SAT_THRESHOLD = 0.98f;

// LibRaw 색 번호
constexpr int CH_R = 0;
constexpr int CH_G = 1;
constexpr int CH_B = 2;
constexpr int CH_G2 = 3;

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