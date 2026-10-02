#pragma once
#include "profile.hpp"
namespace aw {
inline void validateModule(HMODULE mod, const Json &profile, const char *key) {
    auto b = reinterpret_cast<unsigned char *>(mod);
    need(b != nullptr, "Module is not loaded");
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(b);
    need(dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0 && dos->e_lfanew < 4096,
         "Invalid DOS image");
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS *>(b + dos->e_lfanew);
    auto p = profile.at(key);
    need(nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == p.at("machine") &&
             nt->FileHeader.TimeDateStamp == p.at("timestamp") &&
             nt->OptionalHeader.SizeOfImage == p.at("image_size"),
         "Unsupported build; no hooks installed");
    need(fs::file_size(imagePath(mod)) == p.at("file_size").get<uintmax_t>(), "Unsupported file size");
    for (auto &[name, e] : profile.at("entries").items()) {
        auto h = e.at("head").get<std::string>();
        auto mask = e.value("mask", std::string(h.size(), 'f'));
        size_t r = e.at("rva");
        need(r + h.size() / 2 < nt->OptionalHeader.SizeOfImage, "Invalid RVA");
        for (size_t i = 0; i < h.size() / 2; i++) {
            int value = std::stoi(h.substr(i * 2, 2), nullptr, 16),
                m = std::stoi(mask.substr(i * 2, 2), nullptr, 16);
            need((b[r + i] & m) == (value & m),
                 "Signature mismatch: " + name + " (stop the legacy service or restart this process)");
        }
    }
}
} // namespace aw
