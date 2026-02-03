#ifndef PROTOPYTHON_BYTECODELOADER_H
#define PROTOPYTHON_BYTECODELOADER_H

#include <string>

namespace protoPython {

/**
 * @brief Stub for future bytecode loading. Validates that a module/script path
 *        could be used for bytecode loading.
 * @param path Path to .py file or module name.
 * @return true if the path appears valid for bytecode loading.
 */
bool bytecodeLoaderStub(const std::string& path);

} // namespace protoPython

#endif
