#include "pch.h"

#include "Editor/Common/Config/EditorConfig.h"

#include "Editor/Common/EditorUtil.h"

#include "Core/File/FileUtil.h"

#include "Engine/Serialization/Format/JsonSerializer.h"

#include "sw/config/ConfigConstants.h"

namespace sw
{
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
		EditorConfig	cfg{};
		string			jsonStr;
		const TypeInfo* pTypeInfo	= EditorConfig::StaticType();
		const string	projectRoot = EditorUtil::getProjectRootPath();
		string			configPath	= FileUtil::normalizeSeparators( config::kFileRuntimeEditorConfig );
		const bool		bAbsolute	= ( configPath.size() >= 2 && configPath[1] == ':' ) ||
							   ( configPath.empty() == false && ( configPath[0] == '/' || configPath[0] == '\\' ) );
		if ( projectRoot.empty() == false && bAbsolute == false )
			configPath = FileUtil::joinPath( projectRoot, configPath );

		if ( pTypeInfo != nullptr && FileUtil::readTextFile( configPath.c_str(), jsonStr ) )
		{
			if ( JsonSerializer::deserialize( &cfg, *pTypeInfo, jsonStr ) )
				SW_LOG_INFO( "[Editor] EditorConfig source=file (%#)", configPath.c_str() );
			else
				SW_LOG_WARNING( "[Editor] EditorConfig deserialize failed — cpp defaults" );
		}
		else
			SW_LOG_WARNING( "[Editor] EditorConfig missing or reflection unavailable — cpp defaults" );

		setActive( cfg );
	}
} // namespace sw
