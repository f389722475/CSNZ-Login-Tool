#pragma once
#include "../../include/platform.hpp"
#include "../../include/game.hpp"
namespace giga_family {
using Address=std::uintptr_t;
struct Build {WORD machine;DWORD imageSize;};
constexpr Build ServerBuild{IMAGE_FILE_MACHINE_I386,38477824};
// Shared validateBuild has already verified relocated code and vtable slots.
inline bool sameBuild(const IMAGE_NT_HEADERS32& nt,const Build& b){return nt.Signature==IMAGE_NT_SIGNATURE&&nt.FileHeader.Machine==b.machine&&nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC&&nt.OptionalHeader.SizeOfImage==b.imageSize;}
}
