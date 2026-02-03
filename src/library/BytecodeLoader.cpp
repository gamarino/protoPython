#include <protoPython/BytecodeLoader.h>
#include <algorithm>
#include <string>

namespace protoPython {

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
