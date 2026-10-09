#include "raw_loader.h"

#include <iostream>
#include "utils.h"

// LibRaw 파일 열기 → 열린 상태 그대로 info에 옮긴다
bool readRawInfo(LibRaw& ip, const std::string& path, RawInfo& info, std::string& error) {
    const int ret = ip.open_file(path.c_str());
    if (ret != LIBRAW_SUCCESS) {
        error = LibRaw::strerror(ret);
        return false;
    }

    const libraw_iparams_t& idata = ip.imgdata.idata;
    const libraw_colordata_t& color = ip.imgdata.color;
    const libraw_image_sizes_t& sizes = ip.imgdata.sizes;

    info.make = idata.make;
    info.model = idata.model;
    info.iso = ip.imgdata.other.iso_speed;

    // filters가 1000 미만이면 2x2 Bayer가 아니다 (0: 없음, 1: Leaf, 9: X-Trans)
    if (idata.filters < 1000) {
        info.pattern = "non-bayer:" + std::to_string(idata.filters);
    } else {
        info.pattern.clear();
        for (int r = 0; r < 2; r++) {
            for (int c = 0; c < 2; c++) info.pattern += idata.cdesc[ip.COLOR(r, c)];
        }
    }

    info.black = color.black;
    for (int i = 0; i < 6; i++) info.cblack[i] = color.cblack[i];
    info.white = color.maximum;
    for (int i = 0; i < 4; i++) info.cam_mul[i] = color.cam_mul[i];

    info.raw_w = sizes.raw_width;
    info.raw_h = sizes.raw_height;
    info.w = sizes.width;
    info.h = sizes.height;
    info.top_margin = sizes.top_margin;
    info.left_margin = sizes.left_margin;

    info.colors = idata.colors;
    info.filters = idata.filters;

    info.ok = true;
    return true;
}

bool loadRaw(LibRaw& ip, const std::string& path, RawMeta& meta, const unsigned short*& rawImg,
             std::string* error) {
    int ret = ip.open_file(path.c_str());
    if (!check(ret)) {
        if (error) *error = LibRaw::strerror(ret);
        return false;
    }

    ret = ip.unpack();
    if (!check(ret)) {
        if (error) *error = LibRaw::strerror(ret);
        return false;
    }

    rawImg = ip.imgdata.rawdata.raw_image;
    if (rawImg == nullptr) {
        std::cerr << "Raw img isn't bayer" << std::endl;
        if (error) *error = "Raw img isn't bayer";
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
