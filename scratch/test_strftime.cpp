#include <iostream>
#include <ctime>
int main() {
    time_t t = std::time(nullptr);
    struct tm* tm_ptr = std::localtime(&t);
    char buf[1024];
    size_t res = std::strftime(buf, sizeof(buf), "%Y-%m-%d", tm_ptr);
    std::cout << "res=" << res << " buf='" << buf << "'" << std::endl;
    return 0;
}
