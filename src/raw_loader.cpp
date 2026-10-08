#include "raw_loader.h"

#include <iostream>
#include "utils.h"

bool loadRaw(LibRaw& ip, const std::string& path, RawMeta& meta, const unsigned short*& rawImg) {
    int ret = ip.open_file(path.c_str());
    if (!check(ret)) return false;

    ret = ip.unpack();
    if (!check(ret)) return false;

    rawImg = ip.imgdata.rawdata.raw_image;
    if (rawImg == nullptr) {
        std::cerr << "Raw img isn't bayer" << std::endl;
        return false;
    }

    meta.rawW = ip.imgdata.sizes.raw_width;
    meta.realW = ip.imgdata.sizes.width;
    meta.realH = ip.imgdata.sizes.height;
    meta.top = ip.imgdata.sizes.top_margin;
    meta.left = ip.imgdata.sizes.left_margin;
    meta.WL = ip.imgdata.color.maximum;
    meta.BL = ip.imgdata.color.black;

    meta.patternR = ip.imgdata.color.cblack[4];
    meta.patternC = ip.imgdata.color.cblack[5];

    // 위치 패턴 값은 cblack[6]부터 펼쳐져 있음
    if (meta.patternR && meta.patternC) {
        int n = meta.patternR * meta.patternC;
        meta.cblackPattern.assign(ip.imgdata.color.cblack + 6, ip.imgdata.color.cblack + 6 + n);
    }

    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            char nowC = ip.imgdata.idata.cdesc[ip.COLOR(r, c)];
            char nearC = ip.imgdata.idata.cdesc[ip.COLOR(r, 1 - c)];

            if (nowC == 'R') meta.colors[r][c] = 0;
            else if (nowC == 'B') meta.colors[r][c] = 2;
            else meta.colors[r][c] = (nearC == 'R') ? 1 : 3;
        }
    }

    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            meta.bases[r][c] = meta.BL + ip.imgdata.color.cblack[ip.COLOR(r, c)];
        }
    }

    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {

            int colorIdx = ip.COLOR(r, c);

            if (colorIdx == 3) colorIdx = 1;

            meta.gains[r][c] = ip.imgdata.color.cam_mul[colorIdx] / ip.imgdata.color.cam_mul[1];
        }
    }

    return true;
}
