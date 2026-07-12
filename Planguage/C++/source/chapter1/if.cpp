#include <iostream>

int main() {
    int score{100};
    if (score >= 90) {
        std::cout << "A" << std::endl;
    } else if (score >= 80) {
        std::cout << "B" << std::endl;
    } else {
        std::cout << "C" << std::endl;
    }

    if (int remainder{score % 10}; remainder == 0) {
        std::cout << "10的倍数" << std::endl;
    } else {
        std::cout << "不是10的倍数" << std::endl;
    }
}
