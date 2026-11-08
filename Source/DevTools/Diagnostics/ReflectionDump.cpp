#include "pch.h"

#include "DevTools/Diagnostics/ReflectionDump.h"

#include "Core/Container/StringUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"
#include "Core/String/hashed_string.h"
#include "Core/String/string_splitter.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    /**
     * @brief `-gv_dumpReflection=CameraComponent,CameraRole`: 첫 프레임에 그 타입 · enum 의 등록 내용을 로그로 남깁니다(`TypeRegistry::describeType`).
     * @details 첫 프레임이라 게임 · 에디터 모듈의 타입까지 등록된 뒤다. 한 번 찍고 비운다.
     */
    SW_TEST_GLOBAL_VARIABLE( sw::string, gv_dumpReflection, "", "첫 프레임에 이 이름들(쉼표로 여럿)의 리플렉션 등록 내용을 로그로 남긴다 — 타입 · enum (비우면 사용 안 함)" );

    void ReflectionDump::runPending()
    {
        if ( gv_dumpReflection.empty() )
            return;
        const TypeRegistry&   registry = engine::getSubsystem<TypeRegistry>();
        const string_splitter parts( string_view{ gv_dumpReflection.c_str(), gv_dumpReflection.size() }, { "," } );
        for ( const string_view part : parts.getSplitList() )
        {
            const string_view name = StringUtil::trim( part );
            if ( name.empty() )
                continue;
            const hashed_string key( name );
            const string        text = ( registry.findType( key ) == nullptr && registry.findEnum( key ) != nullptr ) ? registry.describeEnum( key ) : registry.describeType( key );
            SW_LOG_INFO_ID( "Engine.ReflectionDump", "[gv_dumpReflection]\n%#", text.c_str() );
        }
        gv_dumpReflection = string{}; // 한 번만
    }
} // namespace sw
