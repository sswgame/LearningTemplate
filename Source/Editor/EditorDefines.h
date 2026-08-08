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

		/** @brief 기본(라틴) UI 폰트 후보 — 앞쪽이 우선 */
		inline static constexpr const utf8* kBaseFontCandidates[] = {
			"consola.ttf",
			"Consolas.ttf",
			"DejaVuSansMono.ttf",
			"DejaVuSansMono-Bold.ttf",
			"LiberationMono-Regular.ttf",
			"NotoSansMono-Regular.ttf",
			"UbuntuMono-R.ttf",
			"FreeMono.ttf",
		};
		/** @brief 한글 글리프 머지용 폰트 후보 */
		inline static constexpr const utf8* kKoreanFontCandidates[] = {
			"malgun.ttf",
			"malgunsl.ttf",
			"NanumGothic.ttf",
			"NanumBarunGothic.ttf",
			"NotoSansCJK-Regular.ttc",
			"NotoSansCJKkr-Regular.otf",
			"NotoSansKR-Regular.otf",
			"DroidSansFallbackFull.ttf",
		};

		/** @brief <Project>/Config/Editor — 레이아웃·패널 표시 상태 (유저 로컬, Resource 아님) */
		inline static constexpr const utf8* kConfigFolder		 = "Config";
		inline static constexpr const utf8* kEditorConfigFolder = "Editor";
		inline static constexpr const utf8* kImGuiIniFile			 = "imgui.ini";
		inline static constexpr const utf8* kPanelsIniFile		 = "panels.ini";
		inline static constexpr const utf8* kNodeEditorSettingsFile = "NodeEditor.json";
	} // namespace path
} // namespace sw::editor
