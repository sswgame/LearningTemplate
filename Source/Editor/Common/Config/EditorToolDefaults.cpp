#include "pch.h"

#include "Editor/Common/Config/EditorToolDefaults.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/String/string_splitter.h"

#include "Editor/Common/EditorUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorToolDefaults" );

    bool EditorToolDefaults::loadFromHostPath( string_view hostRelativePath )
    {
        // 경로의 정본은 `Scripts/common/Constants.py` 하나다. 경로를 바꾸는 칸을 `EditorConfig` 에 두지 말 것 — 그 파일은
        // **테마를 저장할 때마다 기계가 다시 쓴다.**
        string rel = string( hostRelativePath );
        if ( rel.empty() )
            rel = string( config::kFileRuntimeEditorToolDefaults );

        const string absPath = EditorUtil::resolveProjectRelativePath( rel );

        // 따로 읽고 성공할 때만 덮는다 — 틀린 파일의 앞부분 값이 섞이면 무엇이 기본값인지 아무도 모른다. 오류는 키 이름과 함께 readConfigFile 이 남긴다.
        EditorToolDefaults     loaded{};
        const ConfigReadResult result = ConfigManager::readConfigFile( loaded, absPath );
        if ( result == ConfigReadResult::Missing )
        {
            SW_LOG_INFO( "editortooldefaults 파일이 없어 내장 기본값을 씁니다: %#", absPath );
            return false;
        }
        if ( result == ConfigReadResult::Invalid )
            return false;
        *this = std::move( loaded );
        SW_LOG_INFO( "Loaded from %#", absPath );
        return true;
    }
} // namespace sw::editor
