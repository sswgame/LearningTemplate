/**
 * @file ZoneModuleOrigin.h
 * @brief 지역 볼륨의 모듈 한 줄이 어디서 왔는지(물려받음 · 바꿈 · 통째로 바꿈 · 더함 · 뺌)입니다 — 인스펙터 구역(`ZoneVolumeInspector`)이 글로 붙인다. ImGui 를 모릅니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "GameFramework/Runtime/Gameplay/Zone/ZoneProfile.h"

namespace sw::editor
{
    /** @brief 모듈 줄 하나의 출처입니다. */
    enum class ZoneModuleOrigin : uint8
    {
        Inherited = 0, ///< 프리셋 그대로
        Changed,       ///< 이 자리가 칸 몇 개를 바꿨다
        Replaced,      ///< 이 자리가 통째로 바꿨다
        Added,         ///< 이 자리가 더했다
        Removed        ///< 이 자리가 뺐다
    };

    /** @brief 모듈 줄의 출처를 가르는 규칙입니다. */
    struct ZoneModuleOriginUtil
    {
        /** @brief 이 자리의 덮어쓰기 가운데 이름이 @p name 인 항목입니다. 없으면 nullptr 입니다. */
        static const ZoneModuleDef* findOverride( const vector<ZoneModuleDef>& listOverride, const hashed_string& name )
        {
            for ( const ZoneModuleDef& over : listOverride )
            {
                if ( over._name == name )
                    return &over;
            }
            return nullptr;
        }

        /** @brief 덮어쓰기 @p pOverride(없으면 nullptr)와 프리셋에 그 모듈이 있는지로 출처를 정합니다. */
        static ZoneModuleOrigin classify( const ZoneModuleDef* pOverride, bool bInPreset )
        {
            if ( pOverride == nullptr )
                return ZoneModuleOrigin::Inherited;
            if ( bInPreset == false )
                return ZoneModuleOrigin::Added;
            switch ( pOverride->_op )
            {
                case ZoneOverrideOp::Merge:
                    return ZoneModuleOrigin::Changed;
                case ZoneOverrideOp::Replace:
                    return ZoneModuleOrigin::Replaced;
                case ZoneOverrideOp::Remove:
                    return ZoneModuleOrigin::Removed;
            }
            return ZoneModuleOrigin::Changed;
        }
    };
} // namespace sw::editor
