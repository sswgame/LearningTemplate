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

        if ( pTypeInfo != nullptr && JsonSerializer::loadFile( configPath, &cfg, *pTypeInfo ) )
            SW_LOG_TRACE( "EditorConfig source=file (%#)", configPath.c_str() );
        else
            SW_LOG_WARNING( "EditorConfig missing or deserialize failed — cpp defaults" );

        setActive( cfg );
    }
} // namespace sw::editor
