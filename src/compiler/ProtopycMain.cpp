#include <protoPython/Parser.h>
#include <protoPython/CppGenerator.h>
#include <iostream>
#include <fstream>
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

void generateMakefile(const fs::path& outRoot, const std::vector<fs::path>& sources) {
    fs::path makefilePath = outRoot / "Makefile";
    std::ofstream outFile(makefilePath);
    if (!outFile.is_open()) {
        std::cerr << "Could not open Makefile for writing: " << makefilePath << "\n";
        return;
    }

    outFile << "CXX = g++\n";
    outFile << "CXXFLAGS = -O3 -fPIC -std=c++20\n";
    outFile << "INCLUDES = -I/home/gamarino/Documentos/proyectos/protoPython/include -I/home/gamarino/Documentos/proyectos/protoCore/headers -I/home/gamarino/Documentos/proyectos/protoCore/include\n";
    outFile << "LDFLAGS = -L/home/gamarino/Documentos/proyectos/protoPython/build/src/library -L/home/gamarino/Documentos/proyectos/protoPython/build/protoCore\n";
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
}

void processDirectory(const fs::path& sourceDir, const fs::path& outRoot, bool emitMake) {
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
        generateMakefile(outRoot, generatedSources);
    }
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
            generateMakefile(fs::current_path(), {src});
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
        processDirectory(sourcePath, fs::current_path() / "out", emitMake || buildSo);
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
