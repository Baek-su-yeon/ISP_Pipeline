// dng 파일 열기
#include <libraw/libraw.h>
#include <iostream>
#include <string>


int main (int argc, char* argv[]) {
    if (argc < 2) {
        // 입력 경로 제대로 쳤는지 확인
        std::cerr << "Usage: " << argv[0] << " <input file>" << std::endl;
        return 1;
    }

    std::string input = argv[1];

    LibRaw ip; // 객체 생성
    int ret = ip.open_file(input.c_str()); // 파일명 char로 받으니까 변형

    if (ret != LIBRAW_SUCCESS) {
        std::cerr << LibRaw::strerror(ret) << std::endl;
        return 1;
    }

    ret = ip.unpack();
    if(ret != LIBRAW_SUCCESS) {
        std::cerr << LibRaw::strerror(ret) << std::endl;
        return 1;
    }

    std::cout << "### Image idata Info" << std::endl;
    std::cout << "- Camera manufacturer: " << ip.imgdata.idata.make << std::endl;
    std::cout << "- Camera model: " << ip.imgdata.idata.model << std::endl;

    // 수정: filters 숫자 대신 2x2 위치별 색을 글자로 출력
    std::cout << "- Bayer pattern: ";
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            std::cout << ip.imgdata.idata.cdesc[ip.COLOR(r, c)];
        }
    }
    std::cout << std::endl;
    std::cout << "---" << std::endl;

    std::cout << "### Image Size Info" << std::endl;
    std::cout << "- Raw Height: " << ip.imgdata.sizes.raw_height << ", Width: " << ip.imgdata.sizes.raw_width << std::endl;
    // 수정: iheight/iwidth → height/width
    std::cout << "- Real Height: " << ip.imgdata.sizes.height << ", Width: " << ip.imgdata.sizes.width << std::endl;
    std::cout << "- Margin top: " << ip.imgdata.sizes.top_margin << ", left: " << ip.imgdata.sizes.left_margin << std::endl;
    std::cout << "---" << std::endl;

    std::cout << "### Image Color Info" << std::endl;
    std::cout << "- Black level: " << ip.imgdata.color.black << std::endl;

    // 수정: 채널별 추가 Black Level (cblack 앞 4개)
    // 추가 수정: DNG 위치 패턴 Black Level 출력
    std::cout << "- Per-channel black: ";
    for (int i = 0; i < 4; i++) {
        std::cout << ip.imgdata.color.cblack[i] << " ";
    }
    std::cout << std::endl;

    int pattern_end = 6 + ip.imgdata.color.cblack[4] * ip.imgdata.color.cblack[5];
    std::cout << "- Pattern (" << ip.imgdata.color.cblack[4] << " * " << ip.imgdata.color.cblack[5] << "): ";
    for (int i = 6; i < pattern_end; i++)
    {
        std::cout << ip.imgdata.color.cblack[i] << " ";
    }
    std::cout << std::endl;

    std::cout << "- White level: " << ip.imgdata.color.maximum << std::endl;

    // 수정: 배열 주소 대신 원소 4개 출력
    std::cout << "- WB coefficient: ";
    for (int i = 0; i < 4; i++) {
        std::cout << ip.imgdata.color.cam_mul[i] << " ";
    }
    std::cout << std::endl;

    return 0;
}