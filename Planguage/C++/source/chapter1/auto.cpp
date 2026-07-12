#include <iostream>

int main() {
    // 自动类型推导
    auto count{10};
    auto price{10.5};
    auto letter{'A'};
    auto result = 10;

    std::cout << "Count: " << count << " type: " << typeid(count).name() << std::endl;
    std::cout << "Price: " << price << " type: " << typeid(price).name() << std::endl;
    std::cout << "Letter: " << letter << " type: " << typeid(letter).name() << std::endl;  
    std::cout << "Result: " << result << " type: " << typeid(result).name() << std::endl;

    // 块作用域
    int outter{1};
    {
        int inner{2};
        std::cout << "Outter + Inner: " << outter + inner << std::endl;
    }
    // std::cout << "Inner: " << inner << std::endl;
    return 0;
}
