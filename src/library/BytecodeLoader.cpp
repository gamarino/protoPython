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

} // namespace protoPython
