#include <print>
#include <string>

int main() {
    std::string name{"Alice"};
    int score{100};
    // 需要c++23
    std::println("{} 的成绩是 {}",  name, score);
}