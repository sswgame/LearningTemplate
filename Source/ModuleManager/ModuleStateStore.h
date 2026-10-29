/**
 * @file ModuleManager/ModuleStateStore.h
 * @brief `IModuleStateStore` 의 호스트 구현입니다 — 프로세스 동안 이름 붙은 바이트 묶음을 든다.
 */
#pragma once
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "ModuleAPI/Service/IModuleStateStore.h"

namespace sw
{
    /** @brief 맡김 칸입니다. 호스트가 소유하고 호스트 범위에 `IModuleStateStore` 로 건다. */
    class ModuleStateStore final : public IModuleStateStore
    {
    public:
        void               store( string_view key, vector<uint8>&& bytes ) override;
        [[nodiscard]] bool take( string_view key, vector<uint8>& outBytes ) override;

    private:
        map<string, vector<uint8>> _mapStored;
    };
} // namespace sw
