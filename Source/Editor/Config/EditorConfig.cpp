#include "pch.h"

#include "Editor/Config/EditorConfig.h"

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
} // namespace sw
