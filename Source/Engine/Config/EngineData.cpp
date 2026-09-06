#include "pch.h"

#include "Engine/Config/EngineData.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/XmlSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "EngineData" );

    bool EngineData::loadFromResource( string_view assetRelativePath )
    {
        const string path = assetRelativePath.empty() ? string( path::kEngineData ) : string( assetRelativePath );

        // REFLECT_BODY() 가 헤더에 StaticType() 을 선언해 둔다 — 레지스트리를 이름으로 뒤질
        // 필요가 없고, Engine 내부 서비스에 접근할 수 없는 모듈에서도 그대로 쓸 수 있다.
        const TypeInfo* pTypeInfo = EngineData::StaticType();
        if ( pTypeInfo == nullptr )
        {
            SW_LOG_WARNING( "EngineData TypeInfo 없음 — 내장 기본값을 씁니다." );
            return false;
        }

        // 파일에 없는 필드는 멤버 초기값이 그대로 남는다 — 그래서 실패해도 내장 기본값으로 동작한다.
        if ( XmlSerializer::loadFile( path, this, *pTypeInfo ) == false )
        {
            SW_LOG_WARNING( "Using built-in defaults; failed to read %#", path );
            return false;
        }

        SW_LOG_INFO( "Loaded from %#", path );
        return true;
    }
} // namespace sw
