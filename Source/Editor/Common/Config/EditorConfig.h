/**
 * @file EditorConfig.h
 * @brief Host JSON 에디터 설정 (경로·레이아웃·툴 파일명). XML 시드는 EditorData.
 */
#pragma once
#include "Core/Container/string.h"

#include "Engine/Config/IConfig.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw::editor
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
		string _dialogueGraphDataFile{ "DialogueGraphData.json" };

		PROPERTY()
		string _spriteClipFile{ "SpriteClip.json" };

		static void				   setActive( const EditorConfig& config );
		static const EditorConfig& getActive();
		/** @brief Host JSON을 읽어 active 설정을 채웁니다. 파일이 없으면 cpp 기본값입니다. */
		static void loadFromHost();
	};
} // namespace sw::editor
