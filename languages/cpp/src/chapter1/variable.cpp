#include <iostream>
#include <cstdint>
#include <limits>

int main() {
    int age{10};
    double height{1.75};
    char grade{'A'};
    bool is_student{true};
    long long id{12345678901234567890LL};
    std::cout << "Age(int): " << age << ", size: " << sizeof(age) << std::endl;
    std::cout << "Height(double): " << height << ", size: " << sizeof(height) << std::endl;
    std::cout << "Grade(char): " << grade << ", size: " << sizeof(grade) << std::endl;
    std::cout << "Is Student(bool): " << is_student << ", size: " << sizeof(is_student) << std::endl;
    std::cout << "ID(long long): " << id << ", size: " << sizeof(id) << std::endl;
    

    std::int32_t score{100};
    std::uint64_t file_size{4096};
    std::cout << "Score(int32_t): " << score << ", size: " << sizeof(score) << std::endl;
    std::cout << "File Size(uint64_t): " << file_size << ", size: " << sizeof(file_size) << std::endl;
    
    std::cout << "int min: " << std::numeric_limits<int>::min() << std::endl;
    std::cout << "int max: " << std::numeric_limits<int>::max() << std::endl;
    std::cout << "uint32_t min: " << std::numeric_limits<uint32_t>::min() << std::endl;
    std::cout << "uint32_t max: " << std::numeric_limits<uint32_t>::max() << std::endl;
    

    const double pi{3.1415926};
    const int days_per_week{7};
    std::cout << "pi: " << pi << std::endl;
    std::cout << "days_per_week: " << days_per_week << std::endl;

    constexpr int seconds_per_minute{60};
    constexpr int seconds_per_hour{60 * seconds_per_minute};
    std::cout << "seconds_per_minute: " << seconds_per_minute << std::endl;
    std::cout << "seconds_per_hour: " << seconds_per_hour << std::endl;
    
    return 0;
}