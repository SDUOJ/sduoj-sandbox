#include <iostream>

int main()
{
    long first, second;
    if (!(std::cin >> first >> second)) return 2;
    std::cout << first + second << "\n";
    std::cerr << "diagnostic\n";
}
