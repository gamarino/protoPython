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

/**
 * @brief Stub for parsing .py source or marshal format. Validates path format.
 * @param path Path to .py or .pyc file.
 * @return true if the path appears parseable.
 */
bool bytecodeParseStub(const std::string& path);

/**
 * @brief Stub for loading .pyc marshal header (magic, flags, timestamp/hash).
 *        Does not parse marshalled code; only validates file exists and header structure.
 * @param path Path to .pyc file.
 * @return true if the file exists and has a valid-looking .pyc header (>= 16 bytes).
 */
bool pycMarshalHeaderStub(const std::string& path);

} // namespace protoPython

#endif
