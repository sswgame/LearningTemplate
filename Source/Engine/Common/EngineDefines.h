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
        /**
         * @brief GPU 리소스 지연 해제 프레임 수 (RHIReleaseQueue 기본 frameLatency).
         * @details 4개 RHI 백엔드(DX11/DX12/Vulkan/OpenGL)가 전부 같은 값을 써야 하는 계약 —
         *          한쪽만 바꾸면 아직 GPU가 참조 중인 리소스를 조기 해제할 위험이 있다.
         */
        inline constexpr uint32 kGpuReleaseFrameLatency = 3;
        /**
         * @brief 게임 스레드가 만든 프레임 패킷이 렌더 스레드에 소비되기까지 큐잉될 수 있는 최대
         *        프레임 수 (RenderThread 패킷 링 깊이).
         * @details 아직 큐잉된(소비되지 않은) 패킷이 참조할 수 있는 자원은 최소 이 프레임 수만큼
         *          해제를 미뤄야 한다 (예: GpuMaterialRetireQueue::kRetireFrameDelay). GPU
         *          더블버퍼링 값인 kMaxFrameCountInFlight와는 별개 개념 — 혼동하지 말 것.
         */
        inline constexpr uint32 kRenderFrameQueueDepth = 3;
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
