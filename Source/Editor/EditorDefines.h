#pragma once
/**
 * @file EditorDefines.h
 * @brief EditorModule 전용 상수/경로 정의 (모듈 핫리로드 시 함께 갱신)
 */
#include "Core/Common/Types.h"

namespace sw::editor
{
	namespace constant
	{
		/** @brief 에디터 ImGui 기본 폰트 크기 (픽셀) */
		inline static constexpr float32 kFontSize = 16.0f;
	} // namespace constant

	namespace path
	{
		/** @brief Resource 하위 에디터 리소스 폴더명 */
		inline static constexpr const utf8* kEditorFolder = "Editor";
		/** @brief 에디터 폰트 하위 폴더명 (시스템 Fonts 폴더명과 동일) */
		inline static constexpr const utf8* kFontsFolder	  = "Fonts";
		inline static constexpr const utf8* kConsolasFontFile = "consola.ttf";
		inline static constexpr const utf8* kKoreanUiFontFile = "malgun.ttf";
	} // namespace path
} // namespace sw::editor
