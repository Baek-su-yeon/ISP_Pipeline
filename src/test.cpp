#include <iostream>
#include <libraw/libraw.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <filesystem>
#include <string>

bool check (int result) {
    if (result != LIBRAW_SUCCESS) {
        std::cerr << LibRaw::strerror(result) << std::endl;
        return false;
    }
    
    std::cout << "Success" << std::endl;
    return true;
}

int main (int argc, char* argv[]) {
    if (argc < 2){
        std::cerr << "Usage : " << argv[0] << "<input filename>" << std::endl;
        return 1;
    }

    std::string input = argv[1];
    LibRaw ip;

    int ret = ip.open_file(input.c_str());
    if (!check(ret)) return 1; 

    ret = ip.unpack();
    if (!check(ret)) return 1; 

    unsigned short* rawImg = ip.imgdata.rawdata.raw_image;
    if (rawImg == nullptr){
        std::cerr << "Raw Img isn't Bayer" << std::endl;
        return 1;
    }

    int rawW = ip.imgdata.sizes.raw_width;
    int rawH = ip.imgdata.sizes.raw_height;
    int realW = ip.imgdata.sizes.width;
    int realH = ip.imgdata.sizes.height;
    int top = ip.imgdata.sizes.top_margin;
    int left = ip.imgdata.sizes.left_margin;

    // 비교해보고 싶어서 만든 것, 이러면 여백이 있는 경우에는 저장된 사진에 여백까지 나온다는거아님?
    for (int r = 0; r < rawH; r++){
        for(int c = 0; c < rawW; c++){
            int idx = r * rawW + c;
            std::cout << rawImg[idx] << " ";
        }
        std::cout << std::endl;
    }

    // 이렇게하면 여백 없이 값 나오고
    std::cout << std::endl;

    for (int r = 0; r < realH; r++){
        for (int c = 0; c < realW; c++){
            int idx = (r + top) * rawW + (c + left);
            std::cout << rawImg[idx] << " ";
        }
        std::cout << std::endl;
    }

}