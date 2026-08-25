#pragma once
#include "Engine/Config/IConfig.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "Core/Container/string.h"

namespace sw
{
	/**
	 * @brief Dev/에디터 호스트 설정 (Shipping 미포함)
	 * @details editordata·유저 레이아웃 경로. 맵/아틀라스 등 시드는 Config/Editor/editordata.xml.
	 */
	REFLECT()
	struct EditorConfig : IConfig
	{
		REFLECT_BODY();

		PROPERTY()
		string _editorData{ "Config/Editor/editordata.xml" };

		PROPERTY()
		string _configFolder{ "Config" };

		PROPERTY()
		string _editorConfigFolder{ "Editor" };

		PROPERTY()
		string _imguiIniFile{ "imgui.ini" };

		PROPERTY()
		string _windowsIniFile{ "windows.ini" };

		PROPERTY()
		string _animationGraphSettingsFile{ "AnimationGraph.json" };

		PROPERTY()
		string _animationGraphDataFile{ "AnimationGraphData.json" };

		PROPERTY()
		string _spriteClipFile{ "SpriteClip.json" };

		static void				   setActive( const EditorConfig& config );
		static const EditorConfig& getActive();
	};
} // namespace sw
