#pragma once
#include "platform.h"

namespace giga_family {
// Current test mp.dll only. ID and class identity are inseparable: accepting an
// ID with another variant's vtable would apply offsets to an unproven object.
struct FamilySpec {
    unsigned id,itemId;
    Address wrapperVtable,finalVtable;
    const char* wrapperRtti;
    const char* finalRtti;
    const char* model;
    const char* modelName;
    const char* leaf1;
    const char* leaf2;
};
inline constexpr FamilySpec Family[] = {
    {725,9637,0x181b6b0,0x181ba30,".?AV?$CSimpleWpn@VCBeamGun@@@@",".?AVCBeamGun@@",
     "models/ef_beamgun_wingman.mdl","ef_beamgun_wingman.mdl","beamgun_wingman01","beamgun_wingman02"},
    {726,9638,0x181c950,0x181cd84,".?AV?$CSimpleWpn@VCBeamGunLe@@@@",".?AVCBeamGunLe@@",
     "models/ef_beamgunle_wingman.mdl","ef_beamgunle_wingman.mdl","beamgunle_wingman01","beamgunle_wingman02"},
};
inline const FamilySpec* identifyFamily(unsigned id,Address vtable,Address mp) noexcept {
    for(const auto& spec:Family)
        if(csnz::selected(id)&&id==spec.id&&(vtable==mp+spec.wrapperVtable||vtable==mp+spec.finalVtable))return &spec;
    return nullptr;
}
struct VtableSlotGuard { unsigned slot;Address rva; };
inline constexpr VtableSlotGuard FamilySlots[] = {
    {78,0x81eed0},{111,0xba1570},{145,0xba18b0},{146,0xba1990},
    {147,0xba18c0},{167,0xba0a40},{168,0xba0ff0},{169,0xba0d00},
    {170,0xba0df0},{199,0xba0f10},{202,0xba1830},{203,0xba1870},
};
}
