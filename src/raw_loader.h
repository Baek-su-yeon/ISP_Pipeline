#pragma once

#include <string>
#include <vector>
#include <libraw/libraw.h>

// 각 단계가 쓰는 raw 메타데이터 모음
struct RawMeta {
    // 크기와 여백
    int rawW = 0;      // raw_image 한 줄 길이 (여백 포함)
    int realW = 0;     // 유효 영역 크기
    int realH = 0;
    int top = 0;       // 유효 영역 시작 위치
    int left = 0;

    // Black Level / White Level
    int WL = 0;        // White Level (포화점)
    int BL = 0;        // 공통 Black Level

    // cblack 위치 패턴: 크기가 둘 다 0이 아닐 때만 유효
    int patternR = 0;  // 패턴 행 수 (cblack[4])
    int patternC = 0;  // 패턴 열 수 (cblack[5])
    std::vector<unsigned> cblackPattern;   // cblack[6]부터, 행 우선으로 펼친 patternR * patternC 개

    // 2×2 Bayer 위치별 표
    int colors[2][2] = {};    // 내 채널 번호 (0 R, 1 G_R, 2 B, 3 G_B)
    int bases[2][2] = {};     // 기준값 (공통값 + 색별 cblack), raw 단위
    float gains[2][2] = {};   // WB 이득 (G = 1 기준)
};

// --meta-only가 기록하는 메타데이터. open_file만 한 상태(unpack 전)의 LibRaw 값 그대로
struct RawInfo {
    bool ok = false;          // false면 아래 값은 모두 비어 있다 (읽기 실패)

    std::string make, model;
    float iso = 0;            // imgdata.other.iso_speed
    std::string pattern;      // 2x2 위치의 색 글자 (예: BGGR). Bayer가 아니면 "non-bayer:<filters>"

    unsigned black = 0;       // imgdata.color.black
    unsigned cblack[6] = {};  // imgdata.color.cblack[0..5] (0~3 채널별, 4·5 위치 패턴 크기 h·w)
    unsigned white = 0;       // imgdata.color.maximum
    float cam_mul[4] = {};    // R, G, B, G2

    int raw_w = 0, raw_h = 0;
    int w = 0, h = 0;
    int top_margin = 0, left_margin = 0;

    int colors = 0;
    unsigned filters = 0;
};

// open_file만 하고(unpack 없이) 메타데이터를 info에 채운다.
// 실패하면 error에 LibRaw::strerror 메시지를 담고 false (info.ok는 false 그대로)
bool readRawInfo(LibRaw& ip, const std::string& path, RawInfo& info, std::string& error);

// LibRaw로 파일을 열고 unpack한 뒤 메타데이터를 meta에 채운다.
// rawImg는 LibRaw 객체(ip)가 살아 있는 동안만 유효하므로, 호출한 쪽에서 ip를 끝까지 유지해야 한다.
bool loadRaw(LibRaw& ip, const std::string& path, RawMeta& meta, const unsigned short*& rawImg);
