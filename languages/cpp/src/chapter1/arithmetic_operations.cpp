#include <iostream>

int main(){
    // 算术运算符
    int a{10};
    int b{5};
    std::cout << "a + b: " << a + b << std::endl;
    std::cout << "a - b: " << a - b << std::endl;
    std::cout << "a * b: " << a * b << std::endl;
    std::cout << "a / b: " << a / b << std::endl;
    std::cout << "a % b: " << a % b << std::endl;

    double result = static_cast<double>(a) / b;
    std::cout << "a / b: " << result << " type: " << typeid(result).name() << std::endl;
    return 0;
}