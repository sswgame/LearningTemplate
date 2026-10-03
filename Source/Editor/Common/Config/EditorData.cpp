#include "pch.h"

#include "Editor/Common/Config/EditorData.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/string_splitter.h"

#include "Editor/Common/EditorUtil.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/JsonSerializer.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorData" );

    bool EditorData::loadFromHostPath( string_view hostRelativePath )
    {
        // 경로의 정본은 `Scripts/common/Constants.py` 하나다. 경로를 바꾸는 칸을 `EditorConfig` 에 두지 말 것 — 그 파일은
        // **테마를 저장할 때마다 기계가 다시 쓴다.**
        string rel = string( hostRelativePath );
        if ( rel.empty() )
            rel = string( config::kFileRuntimeEditorData );

        const string absPath = EditorUtil::resolveProjectRelativePath( rel );

        // REFLECT_BODY() 가 헤더에 StaticType() 을 선언해 둔다. 그래서 레지스트리를 이름으로 뒤질 필요가 없고, Engine 내부
        // 서비스에 접근할 수 없는 모듈에서도 그대로 쓸 수 있다.
        const TypeInfo* pTypeInfo = EditorData::StaticType();
        if ( pTypeInfo == nullptr )
        {
            SW_LOG_WARNING( "EditorData TypeInfo 없음 — 내장 기본값을 씁니다." );
            return false;
        }

        // 파일에 없는 필드는 멤버 초기값이 그대로 남는다. 다만 loadFile 이 false 를 반환해도 그 앞까지 읽은 값은 **이미
        // 들어가 있다.** "기본값" 은 파일이 없을 때만 맞는 말이라 두 경우를 나눠 로그에 남긴다.
        if ( JsonSerializer::loadFile( absPath, this, *pTypeInfo ) == false )
        {
            if ( FileUtil::fileExists( absPath ) == false )
                SW_LOG_INFO( "editordata 파일이 없어 내장 기본값을 씁니다: %#", absPath );
            else
                SW_LOG_WARNING( "editordata 의 일부 필드를 읽지 못했습니다(키 오타·형식) — 읽힌 값은 쓰고 나머지는 기본값입니다. 파일을 확인하세요: %#", absPath );
            return false;
        }

        SW_LOG_INFO( "Loaded from %#", absPath );
        return true;
    }
} // namespace sw::editor
