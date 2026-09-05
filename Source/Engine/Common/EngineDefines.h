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
        // 렌더링 프레임 상수(kMaxFrameCountInFlight / kGpuReleaseFrameLatency /
        // kRenderFrameQueueDepth)는 RHITypes.h 의 constant 블록에 있다 — 백엔드 간 계약 상수들과
        // 같은 자리에 모아 두는 편이 "한쪽만 바꾸면 깨진다"를 알아보기 쉽다.

        /** @brief 기본 뷰포트 너비입니다. */
        inline constexpr float32 kDefaultViewportWidth = 1280.0f;
        /** @brief 기본 뷰포트 높이입니다. */
        inline constexpr float32 kDefaultViewportHeight = 720.0f;
        /** @brief 기본 공간 분할/물리 셀 크기입니다. */
        inline constexpr float32 kDefaultSpatialCellSize = 64.0f;
        /** @brief 기본 언어 코드입니다. */
        inline constexpr const utf8* kDefaultLanguage = "en_US";
    } // namespace constant

    /**
     * @brief Resource 아래 팩 폴더 이름
     * @details XML보다 먼저 리소스 루트를 찾기 위해 컴파일 상수로 둡니다.
     *          파이프라인·셰이더·InputMap 경로는 EngineData.
     */
    namespace path
    {
        inline static constexpr auto kResourceFolder = "Resource";
        inline static constexpr auto kEnginePack     = "engine";
        inline static constexpr auto kCommonPack     = "common";
        inline static constexpr auto kGamePack       = "game";
        inline static constexpr auto kEditorPack     = "editor";

        inline static constexpr auto kShaderFolder       = "shaders";
        inline static constexpr auto kTextureFolder      = "textures";
        inline static constexpr auto kMapsFolder         = "maps";
        inline static constexpr auto kPrefabsFolder      = "prefabs";
        inline static constexpr auto kDataFolder         = "data";
        inline static constexpr auto kLocalizationFolder = "localization";
        inline static constexpr auto kPresetsFolder      = "presets";
        inline static constexpr auto kGlobalVarsFolder   = "globalvars";

        /** @brief 엔진 셸 부트스트랩 XML (Resource 상대). */
        inline static constexpr auto kEngineData = "engine/data/enginedata.xml";
        /** @brief 에셋 메타 파일 확장자입니다. */
        inline static constexpr auto kMetaExtension = ".meta";
    } // namespace path
} // namespace sw
