/**
 * @file EditorGlobalVariableCommands.h
 * @brief 전역 변수 프리셋 파일 IO 커맨드
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw::editor
{
	/**
	 * @class EditorGlobalVariableCommands
	 * @brief Global Variables 패널의 프리셋 저장/로드/목록을 ImGui 없이 수행합니다.
	 */
	class EditorGlobalVariableCommands
	{
	public:
		/** @brief 현재 전역 변수 값을 프리셋 XML로 저장합니다. */
		static bool savePreset( const string& filePath, const string& presetName );
		/** @brief 프리셋 XML을 읽어 전역 변수에 적용합니다. */
		static bool loadPreset( const string& filePath );
		/** @brief .gvpreset.xml 파일 경로 목록을 채웁니다. */
		static bool collectPresetFiles( vector<string>& outList );
		/** @brief 프리셋 폴더 절대 경로를 반환합니다. */
		static string getPresetFolderPath();
		/** @brief 컴포넌트 프리셋(.preset.xml) 폴더를 스캔합니다. */
		static bool collectComponentPresetFiles( vector<string>& outList );
		/** @brief 컴포넌트 프리셋 폴더 절대 경로를 반환합니다. */
		static string getComponentPresetFolderPath();
	};
} // namespace sw::editor
