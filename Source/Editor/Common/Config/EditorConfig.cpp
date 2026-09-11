#include "pch.h"

#include "Editor/Common/Config/EditorConfig.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/EditorUtil.h"

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
        EditorConfig    cfg{};
        const TypeInfo* pTypeInfo   = EditorConfig::StaticType();
        const string    projectRoot = EditorUtil::getProjectRootPath();
        string          configPath  = FileUtil::normalizeSeparators( config::kFileRuntimeEditorConfig );
        const bool      bAbsolute   = ( configPath.size() >= 2 && configPath[1] == ':' ) ||
                               ( configPath.empty() == false && ( configPath[0] == '/' || configPath[0] == '\\' ) );
        if ( projectRoot.empty() == false && bAbsolute == false )
            configPath = FileUtil::joinPath( projectRoot, configPath );

        // loadFile 은 필드 하나만 어긋나도 false 를 돌려주지만, 그 앞까지 읽은 값은 cfg 에 **이미 들어가 있다.**
        // 그래서 "기본값을 쓴다" 는 파일이 없을 때만 참이다 — 두 경우를 갈라서 말한다.
        if ( pTypeInfo != nullptr && JsonSerializer::loadFile( configPath, &cfg, *pTypeInfo ) )
            SW_LOG_TRACE( "EditorConfig source=file (%#)", configPath.c_str() );
        else if ( FileUtil::fileExists( configPath ) == false )
            SW_LOG_INFO( "EditorConfig 파일이 없어 내장 기본값을 씁니다: %#", configPath.c_str() );
        else
            SW_LOG_WARNING( "EditorConfig 의 일부 필드를 읽지 못했습니다(키 오타·형식) — 읽힌 값은 쓰고 나머지는 기본값입니다. 파일을 확인하세요: %#", configPath.c_str() );

        setActive( cfg );
    }

    void EditorConfig::saveToHost()
    {
        const TypeInfo* pTypeInfo   = EditorConfig::StaticType();
        const string    projectRoot = EditorUtil::getProjectRootPath();
        string          configPath  = FileUtil::normalizeSeparators( config::kFileRuntimeEditorConfig );
        const bool      bAbsolute   = ( configPath.size() >= 2 && configPath[1] == ':' ) ||
                               ( configPath.empty() == false && ( configPath[0] == '/' || configPath[0] == '\\' ) );
        if ( projectRoot.empty() == false && bAbsolute == false )
            configPath = FileUtil::joinPath( projectRoot, configPath );

        if ( pTypeInfo != nullptr && JsonSerializer::saveFile( configPath, &s_activeEditorConfig, *pTypeInfo ) )
            SW_LOG_TRACE( "EditorConfig saved to file (%#)", configPath.c_str() );
        else
            SW_LOG_WARNING( "Failed to save EditorConfig to %#", configPath.c_str() );
    }
} // namespace sw::editor
