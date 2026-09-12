#include <iomanip>
#include <iostream>

int main() {
    constexpr int subject_count{3};
    double total{};

    for(int index{1}; index <= subject_count; ++index) {
        double score{};
        std::cout << "请输入第" << index << "门成绩（0~100）: ";

        if (!(std::cin >> score) || score < 0 || score > 100) {
            std::cout << "成绩输入无效。" << std::endl;
            return 1;
        }
        total += score;
    }

    const double average{total / subject_count};
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "平均分: " << average << std::endl;

    if (average >= 90) {
        std::cout << "等级：优秀" << std::endl;
    } else if (average >= 80) {
        std::cout << "等级：及格" << std::endl;
    } else {
        std::cout << "等级：需要继续努力" << std::endl;
    }

    return 0;
}