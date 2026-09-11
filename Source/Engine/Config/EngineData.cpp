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

        // 파일에 없는 필드는 멤버 초기값이 그대로 남는다. loadFile 이 false 여도 그 앞까지 읽은 값은 이미
        // 들어가 있다 — 리소스 경로라 "없음" 과 "일부 실패" 를 여기서 가르지 못하므로 둘 다 사실대로 적는다.
        if ( XmlSerializer::loadFile( path, this, *pTypeInfo ) == false )
        {
            SW_LOG_WARNING( "EngineData 를 읽지 못했거나 일부 필드가 어긋났습니다 — 읽힌 값은 쓰고 나머지는 내장 기본값입니다: %#", path );
            return false;
        }

        SW_LOG_INFO( "Loaded from %#", path );
        return true;
    }
} // namespace sw
