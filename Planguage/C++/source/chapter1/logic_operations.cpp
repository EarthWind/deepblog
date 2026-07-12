#include <iostream>

int main() {
    // 逻辑运算符
    bool a{true};
    bool b{false};
    std::cout << std::boolalpha << "a && b: " << (a && b) << std::endl;
    std::cout << std::boolalpha << "a || b: " << (a || b) << std::endl;
    std::cout << std::boolalpha << "!(a && b): " << !(a && b) << std::endl;

    return 0;
}