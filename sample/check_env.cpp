#include <libraw/libraw.h>
#include <opencv2/core.hpp>
#include <iostream>

int main() {
    std::cout << "LibRaw: " << LibRaw::version() << std::endl;
    std::cout << "OpenCV: " << CV_VERSION << std::endl;
}
