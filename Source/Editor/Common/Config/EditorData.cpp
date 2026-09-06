#include "pch.h"

#include "Editor/Common/Config/EditorData.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/string_splitter.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/EditorUtil.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/XmlSerializer.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorData" );

    bool EditorData::loadFromHostPath( string_view hostRelativePath )
    {
        string rel = hostRelativePath.empty() == false ? string( hostRelativePath ) : EditorConfig::getActive()._editorData;
        if ( rel.empty() )
            rel = string( config::kFileRuntimeEditorData );

        const string projectRoot = EditorUtil::getProjectRootPath();
        string       absPath     = FileUtil::normalizeSeparators( rel );
        const bool   bAbsolute =
            ( absPath.size() >= 2 && absPath[1] == ':' ) || ( absPath.empty() == false && ( absPath[0] == '/' || absPath[0] == '\\' ) );
        if ( projectRoot.empty() == false && bAbsolute == false )
            absPath = FileUtil::joinPath( projectRoot, absPath );

        // REFLECT_BODY() 가 헤더에 StaticType() 을 선언해 둔다 — 레지스트리를 이름으로 뒤질
        // 필요가 없고, Engine 내부 서비스에 접근할 수 없는 모듈에서도 그대로 쓸 수 있다.
        const TypeInfo* pTypeInfo = EditorData::StaticType();
        if ( pTypeInfo == nullptr )
        {
            SW_LOG_WARNING( "EditorData TypeInfo 없음 — 내장 기본값을 씁니다." );
            return false;
        }

        // 파일에 없는 필드는 멤버 초기값이 그대로 남는다 — 실패해도 내장 기본값으로 동작한다.
        if ( XmlSerializer::loadFile( absPath, this, *pTypeInfo ) == false )
        {
            SW_LOG_WARNING( "Using built-in defaults; failed to read %#", absPath );
            return false;
        }

        SW_LOG_INFO( "Loaded from %#", absPath );
        return true;
    }
} // namespace sw::editor
