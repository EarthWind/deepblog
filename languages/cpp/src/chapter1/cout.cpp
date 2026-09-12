#include <iostream>
#include <iomanip>

int main() {
    int age{10};
    std::cout << "Age: " << age << std::endl;

    double price{12.3456};
    std::cout << std::fixed << std::setprecision(2) << price << std::endl;
    std::cout << std::setw(10) << std::right << price << std::endl;
    std::cout << std::setw(10) << std::left << price << std::endl;
    return 0;
}