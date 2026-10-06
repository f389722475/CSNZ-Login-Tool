#pragma once
#include "../../include/platform.hpp"
#include "../../include/game.hpp"
namespace giga_family {
using Address=std::uintptr_t;
struct Build {WORD machine;DWORD timestamp,imageSize;};
constexpr Build ServerBuild{IMAGE_FILE_MACHINE_I386,1783989892,38477824};
inline bool sameBuild(const IMAGE_NT_HEADERS32& nt,const Build& b){return nt.Signature==IMAGE_NT_SIGNATURE&&nt.FileHeader.Machine==b.machine&&nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR32_MAGIC&&nt.FileHeader.TimeDateStamp==b.timestamp&&nt.OptionalHeader.SizeOfImage==b.imageSize;}
}
