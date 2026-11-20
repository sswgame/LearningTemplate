#include "pch.h"

#include "Editor/Panels/Inspector/Model/InspectorNameSource.h"

#include "Core/Log/LogOnce.h"

#include "Engine/Physics/Scene/PhysicsNameValidation.h"

namespace sw::editor
{
    namespace
    {
        struct InspectorNameSourceInternal
        {
            /** @brief 출처 하나 — 메타 값과 이름을 모으는 함수입니다. */
            struct Entry
            {
                const utf8* _pName;
                void ( *_pCollect )( vector<hashed_string>& outListName );
            };

            // 엔진 DLL 에서 들인 함수의 주소는 상수식이 아니다(dllimport) — constexpr 이 아니라 inline const 다.
            static inline const Entry kArrEntry[] = {
                {"CollisionProfile", &PhysicsNameValidation::collectProfileNames},
                {"CollisionChannel", &PhysicsNameValidation::collectChannelNames},
            };
        };
    } // namespace

    bool InspectorNameSource::collectNames( const hashed_string& source, vector<hashed_string>& outListName )
    {
        outListName.clear();
        for ( const InspectorNameSourceInternal::Entry& entry : InspectorNameSourceInternal::kArrEntry )
        {
            if ( source.view() == entry._pName )
            {
                entry._pCollect( outListName );
                return true;
            }
        }
        SW_LOG_ERROR_ONCE_FOR_ID( "Inspector.UnknownNameSource", source.view(), "PROPERTY Meta NameSource '%#' is not a known name source - the field is shown as text", source.c_str() );
        return false;
    }
} // namespace sw::editor
