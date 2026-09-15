#include <protoPython/Parser.h>
#include <protoPython/CppGenerator.h>
#include "protopyc_paths.h"
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

void printUsage() {
    std::cout << "Usage: protopyc <source_path> [options]\n"
              << "Options:\n"
              << "  --emit-cpp   Generates only the C++ source files and headers.\n"
              << "  --emit-make  Generates C++ files plus a specialized Makefile.\n"
              << "  --build-so   Orchestrates the full compilation pipeline to produce a .so library.\n";
}

void processFile(const fs::path& sourcePath, const fs::path& outRoot) {
    if (sourcePath.extension() != ".py") return;

    std::ifstream file(sourcePath);
    if (!file.is_open()) {
        std::cerr << "Could not open file: " << sourcePath << "\n";
        return;
    }
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    
    protoPython::Parser parser(source);
    auto module = parser.parseModule();
    
    if (parser.hasError()) {
        std::cerr << sourcePath.string() << ":" << parser.getLastErrorLine() << ":" << parser.getLastErrorColumn()
                  << ": " << parser.getLastErrorMsg() << "\n";
        exit(1);
    }

    fs::path outPath = outRoot / sourcePath.filename();
    outPath.replace_extension(".cpp");
    
    std::ofstream outFile(outPath);
    if (!outFile.is_open()) {
        std::cerr << "Could not open output file: " << outPath << "\n";
        return;
    }

    protoPython::CppGenerator generator(outFile);
    if (!generator.generate(module.get(), sourcePath.string())) {
        std::cerr << "Failed to generate C++ code for: " << sourcePath << "\n";
        exit(1);
    }

    std::cout << "Generated: " << outPath.string() << "\n";
}

/** Compiler, include and library directories written into the generated Makefile. */
struct Toolchain {
    std::string cxx;
    std::vector<std::string> includeDirs;
    std::vector<std::string> libraryDirs;
};

static std::vector<std::string> splitPathList(const std::string& list) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(':', start);
        if (end == std::string::npos) end = list.size();
        if (end > start) out.push_back(list.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

static fs::path executableDir(const char* argv0) {
    std::error_code ec;
    fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (ec && argv0) exe = fs::canonical(argv0, ec);
    return ec ? fs::path() : exe.parent_path();
}

/**
 * Resolves the toolchain for the generated Makefile.
 *
 * A protopyc that runs from its build directory uses the build tree it was
 * built in; any other protopyc uses the installation layout, whose relative
 * entries are resolved against the executable's directory. The environment
 * variables PROTOPYC_CXX, PROTOPYC_INCLUDE_DIRS and PROTOPYC_LIBRARY_DIRS
 * (':'-separated) replace the corresponding value.
 */
static Toolchain resolveToolchain(const char* argv0) {
    Toolchain tc;
    tc.cxx = PROTOPYC_DEFAULT_CXX;

    const fs::path exeDir = executableDir(argv0);
    std::error_code ec;
    const bool inBuildTree = !exeDir.empty() && fs::equivalent(exeDir, fs::path(PROTOPYC_BUILD_BINDIR), ec);
    if (inBuildTree) {
        tc.includeDirs = splitPathList(PROTOPYC_BUILD_INCLUDE_DIRS);
        tc.libraryDirs = splitPathList(PROTOPYC_BUILD_LIBRARY_DIRS);
    } else {
        auto resolveInstalled = [&exeDir](const std::string& entry) {
            fs::path p(entry);
            return (p.is_absolute() ? p : (exeDir / p).lexically_normal()).string();
        };
        for (const auto& d : splitPathList(PROTOPYC_INSTALL_INCLUDE_DIRS)) tc.includeDirs.push_back(resolveInstalled(d));
        for (const auto& d : splitPathList(PROTOPYC_INSTALL_LIBRARY_DIRS)) tc.libraryDirs.push_back(resolveInstalled(d));
    }

    if (const char* v = std::getenv("PROTOPYC_CXX"); v && *v) tc.cxx = v;
    if (const char* v = std::getenv("PROTOPYC_INCLUDE_DIRS")) tc.includeDirs = splitPathList(v);
    if (const char* v = std::getenv("PROTOPYC_LIBRARY_DIRS")) tc.libraryDirs = splitPathList(v);
    return tc;
}

static bool hasWhitespace(const std::string& s) {
    return s.find_first_of(" \t\n") != std::string::npos;
}

bool generateMakefile(const fs::path& outRoot, const std::vector<fs::path>& sources, const Toolchain& tc) {
    // make splits words on whitespace, so such paths cannot be written safely.
    for (const auto* list : {&tc.includeDirs, &tc.libraryDirs}) {
        for (const auto& d : *list) {
            if (hasWhitespace(d)) {
                std::cerr << "protopyc: directory contains whitespace, which make cannot handle: \"" << d
                          << "\" (set PROTOPYC_INCLUDE_DIRS / PROTOPYC_LIBRARY_DIRS to a path without spaces)\n";
                return false;
            }
        }
    }
    if (hasWhitespace(tc.cxx)) {
        std::cerr << "protopyc: compiler path contains whitespace: \"" << tc.cxx << "\" (set PROTOPYC_CXX)\n";
        return false;
    }

    fs::path makefilePath = outRoot / "Makefile";
    std::ofstream outFile(makefilePath);
    if (!outFile.is_open()) {
        std::cerr << "Could not open Makefile for writing: " << makefilePath << "\n";
        return false;
    }

    outFile << "# Generated by protopyc. Include and library directories come from the\n"
            << "# protoPython build or installation that ran protopyc.\n\n";
    // CXX from the environment or the make command line wins; make's built-in
    // default (origin "default") is replaced by the compiler protoPython was built with.
    outFile << "ifeq ($(origin CXX),default)\n";
    outFile << "CXX = " << tc.cxx << "\n";
    outFile << "endif\n";
    outFile << "CXXFLAGS = -O3 -fPIC -std=c++20\n";
    outFile << "INCLUDES =";
    for (const auto& d : tc.includeDirs) outFile << " -I" << d;
    outFile << "\n";
    outFile << "LDFLAGS =";
    for (const auto& d : tc.libraryDirs) outFile << " -L" << d << " -Wl,-rpath," << d;
    outFile << "\n";
    outFile << "LIBS = -lprotoPython -lprotoCore\n\n";

    outFile << "SRCS =";
    for (const auto& src : sources) {
        outFile << " " << src.string();
    }
    outFile << "\n";

    outFile << "OBJS = $(SRCS:.cpp=.o)\n";
    outFile << "TARGET = module.so\n\n";

    outFile << "all: $(TARGET)\n\n";
    outFile << "$(TARGET): $(OBJS)\n";
    outFile << "\t$(CXX) -shared $(LDFLAGS) -o $@ $^ $(LIBS)\n\n";
    outFile << "%.o: %.cpp\n";
    outFile << "\t$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<\n\n";
    outFile << "clean:\n";
    outFile << "\trm -f $(OBJS) $(TARGET)\n";

    std::cout << "Generated: " << makefilePath.string() << "\n";
    return true;
}

bool processDirectory(const fs::path& sourceDir, const fs::path& outRoot, bool emitMake, const Toolchain& tc) {
    if (!fs::exists(outRoot)) {
        fs::create_directories(outRoot);
    }

    std::vector<fs::path> generatedSources;
    for (const auto& entry : fs::recursive_directory_iterator(sourceDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".py") {
            fs::path relPath = fs::relative(entry.path(), sourceDir);
            fs::path outPathDir = outRoot / relPath.parent_path();
            if (!fs::exists(outPathDir)) fs::create_directories(outPathDir);
            
            processFile(entry.path(), outPathDir);
            
            fs::path generatedSrc = relPath;
            generatedSrc.replace_extension(".cpp");
            generatedSources.push_back(generatedSrc);
        }
    }

    if (emitMake) {
        return generateMakefile(outRoot, generatedSources, tc);
    }
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    fs::path sourcePath = argv[1];
    bool emitCpp = false;
    bool emitMake = false;
    bool buildSo = false;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--emit-cpp") emitCpp = true;
        else if (arg == "--emit-make") emitMake = true;
        else if (arg == "--build-so") buildSo = true;
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            return 1;
        }
    }

    // Default to emit-cpp if no mode specified
    if (!emitCpp && !emitMake && !buildSo) emitCpp = true;

    if (!fs::exists(sourcePath)) {
        std::cerr << "Source path does not exist: " << sourcePath << "\n";
        return 1;
    }

    if (fs::is_regular_file(sourcePath)) {
        processFile(sourcePath, fs::current_path());
        if (emitMake || buildSo) {
            fs::path src = sourcePath.filename();
            src.replace_extension(".cpp");
            if (!generateMakefile(fs::current_path(), {src}, resolveToolchain(argv[0]))) return 1;
        }
        if (buildSo) {
            std::cout << "Building shared library...\n";
            int ret = std::system("make");
            if (ret != 0) {
                std::cerr << "Build failed with exit code: " << ret << "\n";
                return 1;
            }
        }
    } else if (fs::is_directory(sourcePath)) {
        if (!processDirectory(sourcePath, fs::current_path() / "out", emitMake || buildSo, resolveToolchain(argv[0]))) {
            return 1;
        }
        if (buildSo) {
            std::cout << "Building shared library in ./out ...\n";
            fs::path oldPath = fs::current_path();
            fs::current_path(oldPath / "out");
            int ret = std::system("make");
            fs::current_path(oldPath);
            if (ret != 0) {
                std::cerr << "Build failed with exit code: " << ret << "\n";
                return 1;
            }
        }
    }

    return 0;
}
