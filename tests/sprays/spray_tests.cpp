#ifndef _FORTIFY_SOURCE
#define _FORTIFY_SOURCE 2
#endif
#include <algorithm>
#include <climits>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>
#include <png.h>
using byte = unsigned char;
using uint8 = unsigned char;
using std::min;
namespace fs = std::filesystem;
#define HAVE_PNG 1
#define WIN32 1
#define MAX_PATH 260
#define COPY_ALL_CHARACTERS -1
#define CORRECT_PATH_SEPARATOR '/'
#define DLL_EXT_STRING ".dll"
#define IVTEX_VERSION_STRING "VTex001"
#define _access access
#define stricmp strcasecmp
static void Require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
static void Msg(const char *, ...) {}
template<class T> static void V_swap(T &a, T &b) { std::swap(a, b); }
static bool IsPowerOfTwo(int value) { return value > 0 && !(value & (value - 1)); }
static bool IsPosix() { return false; }
static void Q_strncpy(char *out, const char *in, size_t size) { Require(size > 0, "string capacity"); std::snprintf(out, size, "%s", in); }
static void Q_strncat(char *out, const char *in, size_t size, int) { Q_strncpy(out + std::strlen(out), in, size - std::strlen(out)); }
static int Q_snprintf(char *out, size_t size, const char *format, ...) { va_list args; va_start(args, format); int n = std::vsnprintf(out, size, format, args); va_end(args); return n; }
static void Q_FixSlashes(char *s, char slash = '/') { for (; *s; ++s) if (*s == '/' || *s == '\\') *s = slash; }
static void Q_StripTrailingSlash(char *s) { size_t n = std::strlen(s); if (n && (s[n-1] == '/' || s[n-1] == '\\')) s[n-1] = 0; }
static void Q_AppendSlash(char *s, size_t size) { Q_strncat(s, "/", size, -1); }
template<size_t N> static void V_strcpy_safe(char (&out)[N], const char *in) { Q_strncpy(out, in, N); }
#define V_strcat_safe(out, in, size) Q_strncat(out, in, size, -1)
#define V_StripTrailingSlash Q_StripTrailingSlash
#define V_FixSlashes Q_FixSlashes
#include "types.inc"
class CUtlBuffer {
public:
    enum { SEEK_HEAD, SEEK_CURRENT };
    std::vector<byte> data;
    int get = 0, put = 0;
    CUtlBuffer(int = 0, int capacity = 0) { EnsureCapacity(capacity); }
    void EnsureCapacity(int n) { Require(n >= 0 && n <= 10000000, "allocation limit"); if (size_t(n) > data.size()) data.resize(n); }
    int TellGet() const { return get; }
    int TellPut() const { return put; }
    void *Base() { return data.data(); }
    void *PeekPut() { return data.data() + put; }
    void SeekGet(int origin, int n) { get = (origin == SEEK_HEAD ? 0 : get) + n; }
    void SeekPut(int origin, int n) { put = (origin == SEEK_HEAD ? 0 : put) + n; Require(put >= 0 && size_t(put) <= data.size(), "write cursor"); }
    void Get(void *out, int n) { Require(n >= 0 && n <= put - get, "read bounds"); std::memcpy(out, data.data() + get, n); get += n; }
    void Put(const void *in, int n) { EnsureCapacity(put + n); if (n) std::memcpy(data.data() + put, in, n); put += n; }
    void PutChar(int c) { byte b = byte(c); Put(&b, 1); }
};
static std::vector<byte> Load(const std::string &path) {
    std::ifstream input(path, std::ios::binary); Require(bool(input), "file open");
    return std::vector<byte>(std::istreambuf_iterator<char>(input), {});
}
static void Save(const std::string &path, const std::vector<byte> &bytes) {
    std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()); Require(bool(out), "file write");
}
struct FileSystem {
    std::string game;
    bool failWrite = false;
    bool ReadFile(const char *path, const char *, CUtlBuffer &out) {
        if (!fs::is_regular_file(path)) return false;
        auto bytes = Load(path); out.Put(bytes.data(), bytes.size()); return true;
    }
    bool WriteFile(const char *path, const char *, CUtlBuffer &in) {
        if (failWrite) return false;
        Save(path, std::vector<byte>(in.data.begin(), in.data.begin() + in.put)); return true;
    }
    void CreateDirHierarchy(const char *path, const char *) { fs::create_directories(game + "/" + path); }
};
static FileSystem fileSystem, *g_pFullFileSystem = &fileSystem;
struct Engine { const char *GetGameDirectory() { return fileSystem.game.c_str(); } };
static Engine engineObject, *engine = &engineObject;
static bool copyFail = false;
static int CopyFile(const char *src, const char *dst, bool failIfExists) {
    if (copyFail) return 0;
    std::error_code error;
    return fs::copy_file(src, dst, failIfExists ? fs::copy_options::none : fs::copy_options::overwrite_existing, error) && !error;
}
static void DeleteFile(const char *path) { std::error_code e; fs::remove(path, e); }
enum ImageFormat { IMAGE_FORMAT_RGB888, IMAGE_FORMAT_RGBA8888, IMAGE_FORMAT_BGR888, IMAGE_FORMAT_BGRA8888, IMAGE_FORMAT_I8 };
static void RGBA8888ToBGRA8888(const uint8 *, uint8 *, int);
namespace ImageLoader {
static int SizeInBytes(ImageFormat format) { return format == IMAGE_FORMAT_BGRA8888 ? 4 : 3; }
static bool ConvertImageFormat(byte *src, ImageFormat from, byte *dst, ImageFormat to, int w, int h) {
    Require(from == IMAGE_FORMAT_RGBA8888 && to == IMAGE_FORMAT_BGRA8888, "unexpected pixel conversion");
    RGBA8888ToBGRA8888(src, dst, w * h); return true;
}
}
#include "writer.inc"
static int moduleMode = 0, vtexCalls = 0, unloads = 0;
static std::vector<byte> vtexInput;
struct IVTex {
    int VTex(int argc, char **argv) {
        Require(argc == 4 && std::string(argv[2]) == "-dontusegamedir", "vtex arguments"); ++vtexCalls;
        vtexInput = Load(argv[3]);
        if (moduleMode == 4) return 1;
        if (moduleMode != 5) {
            fs::path output(argv[3]); output.replace_extension(".vtf");
            std::vector<byte> bytes = {'V','T','F',0}; bytes.insert(bytes.end(), vtexInput.begin(), vtexInput.end()); Save(output.string(), bytes);
            output.replace_extension(".txt"); Save(output.string(), {'t','e','s','t'});
        }
        return moduleMode == 6 ? 1 : 0;
    }
};
struct CSysModule {};
static CSysModule module;
static IVTex vtex;
using CreateInterfaceFn = void *(*)(const char *, int *);
static void *Factory(const char *, int *) { return moduleMode == 3 ? nullptr : &vtex; }
static CSysModule *Sys_LoadModule(const char *) { return moduleMode == 1 ? nullptr : &module; }
static CreateInterfaceFn Sys_GetFactory(CSysModule *) { return moduleMode == 2 ? nullptr : Factory; }
static void Sys_UnloadModule(CSysModule *) { ++unloads; }
static ConversionErrorType ImgUtl_ConvertJPEGToTGA(const char *, const char *, bool) { return CE_SOURCE_FILE_FORMAT_NOT_SUPPORTED; }
static ConversionErrorType ImgUtl_ConvertBMPToTGA(const char *, const char *) { return CE_SOURCE_FILE_FORMAT_NOT_SUPPORTED; }
#include "imageutils_under_test.inc"
struct Dialog {
    std::vector<std::string> filters;
    void AddFilter(const char *pattern, const char *, bool) { filters.emplace_back(pattern); }
};
static void CheckFilters() {
    Dialog dialog; auto *m_hImportSprayDialog = &dialog;
#include "filters.inc"
    Require(dialog.filters.front().find("*.png") != std::string::npos, "PNG missing from all images");
    Require(std::find(dialog.filters.begin(), dialog.filters.end(), "*.png") != dialog.filters.end(), "PNG filter missing");
}
#undef HAVE_PNG
#define HAVE_PNG 0
static void CheckFiltersWithoutPNG() {
    Dialog dialog; auto *m_hImportSprayDialog = &dialog;
#include "filters.inc"
    for (const auto &filter : dialog.filters) Require(filter.find("png") == std::string::npos, "PNG exposed without decoder");
}
static bool InvalidRead(int test) {
    CUtlBuffer buffer; byte in[2] = {1,2}, out[4] = {}; buffer.Put(in, 2);
    png_structp reader = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    Require(reader != nullptr, "libpng allocation");
    png_set_read_fn(reader, test == 0 ? nullptr : &buffer, ReadPNGData);
    if (test == 1) buffer.get = -1;
    if (test == 2) buffer.get = 3;
    if (setjmp(png_jmpbuf(reader))) { png_destroy_read_struct(&reader, nullptr, nullptr); return true; }
    ReadPNGData(reader, out, test == 3 ? size_t(-1) : 3);
    png_destroy_read_struct(&reader, nullptr, nullptr); return false;
}
static void Reject(const std::vector<byte> &input) {
    CUtlBuffer buffer; buffer.Put(input.data(), input.size()); int w = 71, h = 82; ConversionErrorType code;
    byte *data = ImgUtl_ReadPNGAsRGBAFromBuffer(buffer, w, h, code);
    Require(!data && code != CE_SUCCESS && w == 71 && h == 82, "invalid PNG accepted or dimensions changed");
}
static void NoTemps(const std::string &directory) {
    for (const auto &entry : fs::directory_iterator(directory)) Require(entry.path().filename().string().rfind("temp", 0) != 0, "temporary file leaked");
}
static std::vector<byte> TGA(int bits = 32, int descriptor = 0x20, int id = 0) {
    std::vector<byte> out(18,0); out[0] = id; out[2] = 2; out[12] = 2; out[14] = 2; out[16] = bits; out[17] = descriptor;
    out.insert(out.end(),id,0xab);
    for (int pixel = 0; pixel < 4; ++pixel) { out.push_back(10+pixel); out.push_back(20+pixel); out.push_back(30+pixel); if (bits == 32) out.push_back(40+pixel); }
    return out;
}
static void CheckTGA(const std::string &dir) {
    const std::string path = dir + "/bounds.tga";
    const auto valid = TGA();
    auto reject = [&](const std::vector<byte> &bytes) {
        Save(path,bytes); int w = 71, h = 82; ConversionErrorType code = CE_SUCCESS; TGAHeader header = {};
        byte *data = ImgUtl_ReadTGAAsRGBA(path.c_str(),w,h,code,header);
        Require(!data && code != CE_SUCCESS && w == 71 && h == 82,"truncated or invalid TGA accepted");
        int calls = vtexCalls;
        Require(ImgUtl_ConvertTGAToVTF(path.c_str(),256,256) != CE_SUCCESS && vtexCalls == calls,"invalid TGA reached VTex");
    };
    for (size_t n = 0; n < valid.size(); ++n) reject({valid.begin(),valid.begin()+n});
    for (int offset : {12,14}) { auto bad = valid; bad[offset] = 0; reject(bad); bad[offset] = 0xff; bad[offset+1] = 0xff; reject(bad); }
    auto bad = valid; bad[12] = bad[13] = bad[14] = bad[15] = 0x7f; reject(bad);
    bad = valid; bad[0] = 255; reject(bad);
    for (int offset : {1,2,16,17}) { bad = valid; bad[offset] = offset == 17 ? 0x10 : 1; reject(bad); }
    for (int bits : {24,32}) for (int descriptor : {0,0x20}) for (int id : {0,7,255}) {
        Save(path,TGA(bits,descriptor,id)); int w = 0, h = 0; ConversionErrorType code; TGAHeader header;
        byte *data = ImgUtl_ReadTGAAsRGBA(path.c_str(),w,h,code,header);
        Require(data && code == CE_SUCCESS && w == 2 && h == 2,"valid TGA rejected");
        for (int pixel = 0; pixel < 4; ++pixel) {
            int src = descriptor == 0 ? (1-pixel/2)*2+pixel%2 : pixel;
            Require(data[pixel*4] == 30+src && data[pixel*4+1] == 20+src && data[pixel*4+2] == 10+src && data[pixel*4+3] == (bits == 32 ? 40+src : 255),"TGA ID, alpha or orientation changed");
        }
        free(data);
    }
    fs::remove(path);
}
int main(int argc, char **argv) {
    Require(argc == 2, "arguments"); const std::string dir = argv[1]; fileSystem.game = dir + "/game";
    CheckFilters(); CheckFiltersWithoutPNG(); CheckTGA(dir);
    for (int test = 0; test < 4; ++test) Require(InvalidRead(test), "invalid callback read accepted");
    std::ifstream names(dir + "/cases.txt"); std::string name; int valid = 0;
    while (std::getline(names, name)) {
        const auto expected = Load(dir + "/" + name + ".rgba");
        int w = 0, h = 0; ConversionErrorType code;
        byte *data = ImgUtl_ReadPNGAsRGBA((dir + "/" + name + ".png").c_str(), w, h, code);
        Require(data && code == CE_SUCCESS && w == 9 && h == 7, "valid PNG dimensions");
        Require(std::equal(expected.begin(), expected.end(), data), "decoded RGBA mismatch"); free(data);
        const auto tga = dir + "/test.tga";
        Require(ImgUtl_ConvertPNGToTGA((dir + "/" + name + ".png").c_str(), tga.c_str()) == CE_SUCCESS, "PNG to TGA failed");
        TGAHeader header; data = ImgUtl_ReadTGAAsRGBA(tga.c_str(), w, h, code, header);
        Require(data && code == CE_SUCCESS && w == 9 && h == 7, "TGA roundtrip dimensions");
        Require(std::equal(expected.begin(), expected.end(), data), "TGA alpha/channel/orientation mismatch"); free(data); fs::remove(tga); ++valid;
    }
    auto input = Load(dir + "/interlaced.png");
    for (size_t n = 0; n < input.size(); ++n) Reject(std::vector<byte>(input.begin(), input.begin() + n));
    input[input.size()/2] ^= 0x80; Reject(input); Reject(Load(dir + "/oversized.png"));
    int w = 0, h = 0; ConversionErrorType code;
    Require(!ImgUtl_ReadPNGAsRGBA((dir + "/missing.png").c_str(), w, h, code) && code == CE_CANT_OPEN_SOURCE_FILE, "missing file accepted");
    const auto source = dir + "/rgba_4.png", final = fileSystem.game + "/materials/vgui/logos/rgba_4.vtf";
    Require(ImgUtl_ConvertToVTFAndDumpVMT(source.c_str(), "/vgui/logos", 256, 256) == CE_SUCCESS, "full import failed");
    auto expected = Load(dir + "/rgba_4.rgba");
    TGAHeader header; byte *data;
    Save(dir + "/captured.tga", vtexInput);
    data = ImgUtl_ReadTGAAsRGBA((dir + "/captured.tga").c_str(), w, h, code, header);
    Require(data && w == 4 && h == 4 && std::equal(expected.begin(), expected.end(), data), "pixels changed before vtex"); free(data);
    Require(fs::exists(final), "destination VTF missing");
    auto vmt = Load(fileSystem.game + "/materials/vgui/logos/rgba_4.vmt");
    std::string material(vmt.begin(), vmt.end());
    Require(material.find("vgui/logos/rgba_4") != std::string::npos && material.find("$translucent") != std::string::npos, "VMT content");
    NoTemps(dir);
    auto modified = Load(dir + "/rgba_4.png");
    fs::copy_file(dir + "/c6_d8.png", source, fs::copy_options::overwrite_existing);
    auto old = Load(final);
    Require(ImgUtl_ConvertToVTFAndDumpVMT(source.c_str(), "/vgui/logos", 256, 256) == CE_SUCCESS && Load(final) != old, "same-name reimport retained old texture");
    Save(source, modified); NoTemps(dir);
    for (int mode = 1; mode <= 6; ++mode) {
        moduleMode = mode;
        Require(ImgUtl_ConvertToVTFAndDumpVMT(source.c_str(), "/vgui/logos", 256, 256) != CE_SUCCESS, "vtex failure reported success"); NoTemps(dir);
    }
    moduleMode = 0; copyFail = true;
    Require(ImgUtl_ConvertToVTFAndDumpVMT(source.c_str(), "/vgui/logos", 256, 256) == CE_ERROR_WRITING_OUTPUT_FILE, "copy failure ignored");
    copyFail = false; NoTemps(dir); fileSystem.failWrite = true;
    Require(ImgUtl_ConvertToVTFAndDumpVMT(source.c_str(), "/vgui/logos", 256, 256) == CE_ERROR_WRITING_OUTPUT_FILE, "TGA write failure ignored");
    fileSystem.failWrite = false; NoTemps(dir);
    Require(ImgUtl_ConvertToVTFAndDumpVMT((dir + "/rgba_512.png").c_str(), "/vgui/logos", 256, 256) == CE_SUCCESS, "large PNG resize failed");
    Save(dir + "/captured.tga", vtexInput);
    data = ImgUtl_ReadTGAAsRGBA((dir + "/captured.tga").c_str(), w, h, code, header);
    Require(data && w == 256 && h == 128, "resize limit");
    Require(data[3] == 0 && data[(w * (h/2) + w - 1)*4 + 3] == 255, "resize lost alpha"); free(data); NoTemps(dir);
    fs::copy_file(dir + "/rgba_4.png", dir + "/UPPER.PNG");
    Require(ImgUtl_ConvertToVTFAndDumpVMT((dir + "/UPPER.PNG").c_str(), "/vgui/logos", 256, 256) == CE_SUCCESS, "uppercase extension failed"); NoTemps(dir);
    Require(ImgUtl_ConvertToVTFAndDumpVMT((dir + "/thin.png").c_str(), "/vgui/logos", 256, 256) == CE_SUCCESS, "thin PNG resize failed");
    Save(dir + "/captured.tga", vtexInput);
    data = ImgUtl_ReadTGAAsRGBA((dir + "/captured.tga").c_str(), w, h, code, header);
    Require(data && w == 256 && h == 1 && data[3] == 0 && data[(w-1)*4+3] == 255, "thin PNG lost image/alpha"); free(data); NoTemps(dir);
    Save(dir + "/temp0.vtf", {'s','a','f','e'}); Save(dir + "/temp0.txt", {'s','a','f','e'});
    Require(ImgUtl_ConvertToVTFAndDumpVMT(source.c_str(), "/vgui/logos", 256, 256) == CE_SUCCESS, "temporary name collision blocked import");
    Require(Load(dir + "/temp0.vtf") == std::vector<byte>({'s','a','f','e'}) && Load(dir + "/temp0.txt") == std::vector<byte>({'s','a','f','e'}), "existing temporary files overwritten/deleted");
    fs::remove(dir + "/temp0.vtf"); fs::remove(dir + "/temp0.txt"); NoTemps(dir);
    fs::remove(fileSystem.game + "/materials/vgui/logos/rgba_4.vmt");
    fs::create_directory(fileSystem.game + "/materials/vgui/logos/rgba_4.vmt");
    Require(ImgUtl_ConvertToVTFAndDumpVMT(source.c_str(), "/vgui/logos", 256, 256) == CE_ERROR_WRITING_OUTPUT_FILE, "VMT open failure ignored");
    fs::remove(fileSystem.game + "/materials/vgui/logos/rgba_4.vmt"); NoTemps(dir);
    Require(ImgUtl_ConvertToVTFAndDumpVMT((dir + "/captured.tga").c_str(), "/vgui/logos", 256, 256) == CE_SUCCESS && fs::exists(dir + "/captured.tga"), "existing TGA route broken");
    const int calls = vtexCalls;
    Require(ImgUtl_ConvertToVTFAndDumpVMT(final.c_str(), "/vgui/logos", 256, 256) == CE_SUCCESS && vtexCalls == calls, "existing VTF route broken");
    Require(ImgUtl_ConvertToVTFAndDumpVMT("test.jpg", "/vgui/logos", 256, 256) == CE_SOURCE_FILE_FORMAT_NOT_SUPPORTED && vtexCalls == calls, "JPEG converter error ignored");
    Require(ImgUtl_ConvertToVTFAndDumpVMT("test.bmp", "/vgui/logos", 256, 256) == CE_SOURCE_FILE_FORMAT_NOT_SUPPORTED && vtexCalls == calls, "BMP converter error ignored");
    Require(ImgUtl_ConvertToVTFAndDumpVMT("unknown.gif", "/vgui/logos", 256, 256) == CE_SOURCE_FILE_FORMAT_NOT_SUPPORTED, "unsupported format accepted");
    std::cout << "PASS spray import: " << valid << " PNG formats, truncation, RGBA/TGA roundtrip, resize, UI filters, mocked VTex and filesystem failures\n";
}
