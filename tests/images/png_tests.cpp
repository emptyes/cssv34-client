#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
using byte = unsigned char;
using uint32 = uint32_t;
#define Q_memcpy std::memcpy
#define Q_memset std::memset
#define Assert(value) assert(value)
static int warnings = 0;
static void Warning(const char *, ...) { ++warnings; }
static void Require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
class CUtlBuffer {
public:
    enum { SEEK_HEAD };
    std::vector<byte> data;
    size_t largestRequest = 0;
    void EnsureCapacity(int count) {
        largestRequest = std::max(largestRequest, size_t(count));
        Require(count >= 0 && count < 1000000, "unexpected output allocation");
        data.resize(count);
    }
    void SeekPut(int, int count) { Require(count >= 0 && size_t(count) <= data.size(), "output bounds"); }
    void *Base() { return data.data(); }
};
#include "png_under_test.inc"
static std::vector<byte> Load(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    Require(bool(input), "fixture open");
    return std::vector<byte>(std::istreambuf_iterator<char>(input), {});
}
static bool Selected(const std::string &selected, const char *name) { return selected == "all" || selected == name; }
static bool RejectsInvalidRead(int test) {
    byte input[2] = {1, 2}, output[4] = {};
    PngDataStream_t stream(input, 2);
    png_structp reader = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, PNGErrorFunction, PNGWarningFunction);
    Require(reader != nullptr, "reader allocation");
    png_set_read_fn(reader, test == 0 ? nullptr : &stream, ReadPNGData);
    if (test == 2) stream.m_iPNGData = 3;
    if (test == 3) stream.m_iPNGData = 1;
    if (setjmp(png_jmpbuf(reader))) {
        png_destroy_read_struct(&reader, nullptr, nullptr); return true;
    }
    ReadPNGData(reader, output, test == 3 ? size_t(-1) : 3);
    png_destroy_read_struct(&reader, nullptr, nullptr);
    return false;
}
int main(int argc, char **argv) {
    Require(argc == 3, "arguments");
    const std::string directory = argv[1], selected = argv[2];
    size_t cases = 0;
    if (Selected(selected, "signature")) {
        uint32 width = 99, height = 77; int w = 99, h = 77; CUtlBuffer output;
        Require(!GetPNGDimensions(nullptr, 8, width, height), "null dimensions accepted");
        Require(!ConvertPNGToRGBA(nullptr, 8, output, w, h), "null image accepted");
        for (int count = 0; count < 8; ++count) {
            std::vector<byte> shortData(count, 0);
            Require(!GetPNGDimensions(shortData.data(), count, width, height), "short dimensions accepted");
            Require(!ConvertPNGToRGBA(shortData.data(), count, output, w, h), "short image accepted"); ++cases;
        }
        const byte signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
        Require(!GetPNGDimensions(signature, -1, width, height), "negative input length");
        Require(!ConvertPNGToRGBA(signature, -1, output, w, h), "negative conversion length"); ++cases;
    }
    if (Selected(selected, "reader")) {
        for (int test = 0; test < 4; ++test) {
            Require(RejectsInvalidRead(test), "truncated read did not signal libpng error"); ++cases;
        }
    }
    std::ifstream names(directory + "/cases.txt"); std::string name;
    if (Selected(selected, "valid")) while (std::getline(names, name)) {
        const auto input = Load(directory + "/" + name + ".png"), expected = Load(directory + "/" + name + ".rgba");
        uint32 width = 0, height = 0;
        Require(GetPNGDimensions(input.data(), input.size(), width, height) && width == 9 && height == 7, "PNG dimensions");
        CUtlBuffer output; int w = 0, h = 0;
        Require(ConvertPNGToRGBA(input.data(), input.size(), output, w, h), "valid PNG rejected");
        Require(w == 9 && h == 7 && output.data == expected, "RGBA mismatch"); ++cases;
    }
    const auto input = Load(directory + "/interlaced.png");
    if (Selected(selected, "truncated")) {
        for (size_t count = 8; count < input.size(); ++count) {
            CUtlBuffer output; int w = 0, h = 0;
            Require(!ConvertPNGToRGBA(input.data(), count, output, w, h), "truncated PNG accepted"); ++cases;
        }
        auto damaged = input; damaged[damaged.size() / 2] ^= 0x80;
        CUtlBuffer output; int w = 0, h = 0;
        Require(!ConvertPNGToRGBA(damaged.data(), damaged.size(), output, w, h), "bad CRC accepted"); ++cases;
    }
    if (Selected(selected, "limits")) {
        const auto oversized = Load(directory + "/oversized.png");
        CUtlBuffer output; int w = 0, h = 0;
        Require(!ConvertPNGToRGBA(oversized.data(), oversized.size(), output, w, h), "oversized PNG accepted");
        Require(output.largestRequest == 0, "oversized PNG allocated output"); ++cases;
    }
    std::cout << "PASS PNG loader cases: " << cases << '\n';
}
