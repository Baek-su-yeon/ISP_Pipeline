#include "demosaic.h"

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

        if (rr < 0) rr *= (-1);
        else if (img.rows <= rr) rr -= 2;

        if (cc < 0) cc *= (-1);
        else if (img.cols <= cc) cc -= 2;

        int ch = colors[rr % 2][cc % 2];
        if (ch == 3) ch = 1;
        if (ch == own) continue;   // 자기 색은 자기 값만 씀 (G 위치의 대각선 G 제외)

        sum[ch] += img.at<float>(rr, cc);
        count[ch]++;
    }

    sum[own] = img.at<float>(r, c);
    count[own] = 1;

    return cv::Vec3f(sum[0] / count[0], sum[1] / count[1], sum[2] / count[2]);
}

cv::Mat DemosaicReflection(const cv::Mat& bayer, const int (&colors)[2][2]) {
    cv::Mat reflectiondemosaic = cv::Mat::zeros(bayer.rows, bayer.cols, CV_32FC3); // [0] R, [1] G, [2] B
    for (int r = 0; r < bayer.rows; r++) {
        for (int c = 0; c < bayer.cols; c++) {
            reflectiondemosaic.at<cv::Vec3f>(r, c) = ReflectionDemosaic(r, c, colors, bayer);
        }
    }

    return reflectiondemosaic;
}
