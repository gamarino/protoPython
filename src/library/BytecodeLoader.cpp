#include <protoPython/BytecodeLoader.h>
#include <algorithm>
#include <fstream>
#include <string>

namespace protoPython {

bool pycMarshalHeaderStub(const std::string& path) {
    if (path.empty() || (path.size() < 4 || path.compare(path.size() - 4, 4, ".pyc") != 0))
        return false;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char buf[16];
    f.read(buf, 16);
    return f.gcount() >= 16;
}

bool bytecodeLoaderStub(const std::string& path) {
    if (path.empty()) return false;
    if (path.size() >= 3 && path.compare(path.size() - 3, 3, ".py") == 0)
        return true;
    return path.find('.') == std::string::npos || path.find('/') != std::string::npos;
}

bool bytecodeParseStub(const std::string& path) {
    if (path.empty()) return false;
    if (path.size() >= 3 && path.compare(path.size() - 3, 3, ".py") == 0)
        return true;
    if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".pyc") == 0)
        return true;
    return false;
}

} // namespace protoPython
