#include "white_balance.h"

WBResult WhiteBalance(const cv::Mat& normImg, const float (&gains)[2][2]) {
    int realH = normImg.rows;
    int realW = normImg.cols;

    cv::Mat WBImg = cv::Mat::zeros(realH, realW, CV_32F);
    for (int r = 0; r < realH; r++) {
        for (int c = 0; c < realW; c++) {
            float wb = normImg.at<float>(r, c) * gains[r % 2][c % 2];
            WBImg.at<float>(r, c) = wb;
        }
    }

    // 결과는 새 Mat에 담아 자르기 전 WBImg를 그대로 남긴다
    cv::Mat WBClip;
    cv::min(WBImg, 1.0f, WBClip);

    return {WBImg, WBClip};
}
