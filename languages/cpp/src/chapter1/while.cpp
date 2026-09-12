#include <iostream>

int main() {
    int count{1};
    while (count <= 10) {
        std::cout << count << std::endl;
        ++count;
    }

    int choice{0};
    do {
        std::cout << "1. 继续, 0. 退出" << std::endl;
        std::cin >> choice;
    } while (choice != 0);
    
    for (int i{1}; i <= 10; ++i) {
        std::cout << i << " ";
    }
    std::cout << std::endl;

    for (int i{1}; i <=20; ++i) {
        if (i % 2 == 0) {
            continue;
        }
        if (i > 10) {
            break;
        }
        std::cout << i << ' ';
    }

    return 0;
}
