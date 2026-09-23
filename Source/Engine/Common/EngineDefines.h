/**
 * @file EngineDefines.h
 * @brief 엔진 기본 상수(뷰포트 크기 · 공간 셀 · 기본 언어)와 팩 · 폴더 이름입니다. 에셋 경로는 EngineData(enginedata.xml)가 정합니다.
 */
#pragma once
#include "Core/Common/Defines.h"

namespace sw
{
    namespace constant
    {
        // 렌더링 프레임 상수(kMaxFrameCountInFlight / kGpuReleaseFrameLatency /
        // kRenderFrameQueueDepth)는 RHITypes.h 의 constant 블록에 있다. 백엔드 간 계약 상수들과
        // 같은 자리에 모아 두는 편이 "한쪽만 바꾸면 깨진다" 를 알아보기 쉽다.

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
     * @brief Resource 아래 팩 · 폴더 이름입니다.
     * @details XML 보다 먼저 리소스 루트를 찾아야 하므로 컴파일 상수로 둡니다.
     *          파이프라인 · 셰이더 · InputMap 경로는 EngineData 가 정합니다.
     */
    namespace path
    {
        // `inline static` 이 아니라 `inline` 이다. 네임스페이스 스코프에서 `static` 은 내부 연결을
        // 주므로 `inline` 이 하는 일이 없어지고, TU 마다 사본이 하나씩 생긴다. 바로 위 `constant`
        // 블록은 같은 뜻을 `inline constexpr` 로 적고 있었다. 한 파일에 두 철자를 두지 않는다.

        /** @brief 리소스 루트 폴더 이름입니다. */
        inline constexpr const utf8* kResourceFolder = "Resource";
        /** @brief 엔진 기본 제공 에셋 팩입니다. */
        inline constexpr const utf8* kEnginePack = "engine";
        /** @brief 게임 공용 에셋 팩입니다. */
        inline constexpr const utf8* kCommonPack = "common";
        /** @brief 활성 게임 에셋 팩입니다. */
        inline constexpr const utf8* kGamePack = "game";
        /** @brief 에디터 전용 에셋 팩입니다. */
        inline constexpr const utf8* kEditorPack = "editor";

        /** @brief 셰이더 폴더 이름입니다. */
        inline constexpr const utf8* kShaderFolder = "shaders";
        /** @brief 텍스처 폴더 이름입니다. */
        inline constexpr const utf8* kTextureFolder = "textures";
        /** @brief 씬(맵) 폴더 이름입니다. */
        inline constexpr const utf8* kMapsFolder = "maps";
        /** @brief 프리팹 폴더 이름입니다. */
        inline constexpr const utf8* kPrefabsFolder = "prefabs";
        /** @brief 데이터 테이블 폴더 이름입니다. */
        inline constexpr const utf8* kDataFolder = "data";
        /** @brief 현지화 폴더 이름입니다. */
        inline constexpr const utf8* kLocalizationFolder = "localization";
        /** @brief 프리셋 폴더 이름입니다. */
        inline constexpr const utf8* kPresetsFolder = "presets";
        /** @brief 전역 변수 저장 폴더 이름입니다. */
        inline constexpr const utf8* kGlobalVarsFolder = "globalvars";

        /** @brief 엔진 셸 부트스트랩 XML 입니다(Resource 기준 상대 경로). */
        inline constexpr const utf8* kEngineData = "engine/data/enginedata.xml";
        /** @brief 에셋 메타 파일 확장자입니다. */
        inline constexpr const utf8* kMetaExtension = ".meta";
    } // namespace path
} // namespace sw
