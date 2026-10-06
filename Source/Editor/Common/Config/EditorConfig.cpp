#include "pch.h"

#include "Editor/Common/Config/EditorConfig.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/EditorUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Serialization/Format/JsonSerializer.h"

#include "sw/config/ConfigConstants.h"

namespace sw::editor
{
    SW_LOG_CALLER( "Editor" );

    namespace
    {
        EditorConfig s_activeEditorConfig{};
    } // namespace

    void EditorConfig::setActive( const EditorConfig& config )
    {
        s_activeEditorConfig = config;
    }

    const EditorConfig& EditorConfig::getActive()
    {
        return s_activeEditorConfig;
    }

    void EditorConfig::loadFromHost()
    {
        EditorConfig config{};
        const string configPath = EditorUtil::resolveProjectRelativePath( config::kFileRuntimeEditorConfig );

        // 틀린 파일은 칸 하나도 쓰지 않는다 — 읽은 데까지만 쓰면 무엇이 기본값인지 아무도 모른다. 오류는 키 이름과 함께 readConfigFile 이 남긴다.
        const ConfigReadResult result = ConfigManager::readConfigFile( config, configPath );
        if ( result == ConfigReadResult::Loaded )
            SW_LOG_TRACE( "EditorConfig source=file (%#)", configPath.c_str() );
        else if ( result == ConfigReadResult::Missing )
            SW_LOG_INFO( "EditorConfig 파일이 없어 내장 기본값을 씁니다: %#", configPath.c_str() );
        else
            config = EditorConfig{};

        setActive( config );
    }

    void EditorConfig::saveToHost()
    {
        const TypeInfo* pTypeInfo  = EditorConfig::StaticType();
        const string    configPath = EditorUtil::resolveProjectRelativePath( config::kFileRuntimeEditorConfig );

        FileUtil::ensureParentDirectoryExists( configPath );
        if ( pTypeInfo != nullptr && JsonSerializer::saveFile( configPath, &s_activeEditorConfig, *pTypeInfo ) )
            SW_LOG_TRACE( "EditorConfig saved to file (%#)", configPath.c_str() );
        else
            SW_LOG_WARNING( "Failed to save EditorConfig to %#", configPath.c_str() );
    }
} // namespace sw::editor
