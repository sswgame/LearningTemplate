/**
 * @file EngineDefines.h
 * @brief 엔진 상수 (렌더 큐) + 팩 폴더 이름. 에셋 경로는 EngineData / enginedata.xml.
 */
#pragma once
#include "Core/Common/Defines.h"

namespace sw
{
	namespace constant
	{
		/** @brief CPU-GPU 동기화를 위한 렌더링 프레임 최대 큐 크기 (이중 버퍼링 기준) */
		inline constexpr uint32 kMaxFrameCountInFlight = 2;
	} // namespace constant

	/**
	 * @brief Resource 아래 팩 폴더 이름
	 * @details XML보다 먼저 리소스 루트를 찾기 위해 컴파일 상수로 둡니다.
	 *          파이프라인·셰이더·InputMap 경로는 EngineData.
	 */
	namespace path
	{
		inline static constexpr auto kResourceFolder = "Resource";
		inline static constexpr auto kEnginePack	 = "engine";
		inline static constexpr auto kCommonPack	 = "common";
		inline static constexpr auto kGamePack		 = "game";
		inline static constexpr auto kEditorPack	 = "editor";

		inline static constexpr auto kShaderFolder		 = "shaders";
		inline static constexpr auto kTextureFolder		 = "textures";
		inline static constexpr auto kMapsFolder		 = "maps";
		inline static constexpr auto kPrefabsFolder		 = "prefabs";
		inline static constexpr auto kDataFolder		 = "data";
		inline static constexpr auto kLocalizationFolder = "localization";
		inline static constexpr auto kPresetsFolder		 = "presets";
		inline static constexpr auto kGlobalVarsFolder	 = "globalvars";

		/** @brief 엔진 셸 부트스트랩 XML (Resource 상대). */
		inline static constexpr auto kEngineData = "engine/data/enginedata.xml";
	} // namespace path
} // namespace sw
