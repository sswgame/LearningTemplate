/**
 * @file FrameViewTarget.h
 * @brief 뷰 하나가 프레임을 넘어 유지하는 렌더 상태(`FrameViewTarget`)입니다. 렌더러와 그 협력 객체(트랜지언트 · 후처리 · 가림)가 뷰를 인자로 받아 씁니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Frame/Resource/TransientAttachmentPool.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Renderer/Lighting/ShadowSetup.h"
#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"

namespace sw
{
    class IRHICommandList;
    class Texture2D;

    /**
     * @brief 뷰 하나가 프레임을 넘어 유지하는 상태입니다. 트랜지언트 풀, 추가 뷰의 컬링 입력, TAA 히스토리, 직렬 커맨드 리스트, 출력 텍스처가 들어 있습니다.
     * @details 주 시점은 `_mainView` 이고(컬링 입력은 `_arrView[Main]`), 추가 뷰는 카메라 id(`_viewID`)로 찾는다. 패스 코드는 지금 그리는 뷰
     *          (`FramePassExecutor::_pActiveView`)의 것만 본다. 뷰가 바뀌면 트랜지언트 이름(SceneColor 등)이 다른 텍스처를 가리키기 때문이다.
     */
    struct FrameViewTarget
    {
        TransientAttachmentPool     _transientPool;
        RenderView                  _cullInput;   ///< 추가 뷰의 컬링 입력(행렬, 절두체, 자기 컬링 상수 버퍼)
        unique_ptr<IRHICommandList> _commandList; ///< 이 뷰를 직렬로 기록하는 리스트(주 시점은 병렬 경로가 아닐 때만 쓴다)
        RenderViewSettings          _settings;
        hashed_string               _outputPath;                ///< 렌더 텍스처 경로
        Texture2D*                  _pOutputTexture{ nullptr }; ///< 빌려 쓰는 렌더 텍스처(`TextureCache`)
        RHITextureHandle            _hostTarget{ 0 };           ///< 호스트 타깃 뷰의 출력(호스트가 소유한다)
        uint64                      _viewID{ 0 };
        RHITextureHandle            _taaHistory{ 0 }; ///< TAA resolve 히스토리(지난 TAAColor 의 복사본)
        RHIDescriptorIndex          _taaHistorySRV{ kInvalidDescriptorIndex };
        /**
         * @brief 깊이 히스토리 — 지난 프레임에 이 뷰가 남긴 장면 깊이의 R32 실수 사본(RT · RW 텍스처)입니다. GPU 입자의 깊이 충돌이 읽는다(`recordDepthHistories`).
         * @details 깊이 텍스처는 RW 텍스처가 될 수 없고 에뮬 백엔드의 컴퓨트는 샘플 텍스처를 못 읽어서, 프레임 처음에 풀스크린으로 옮겨 둔다.
         *          장면 깊이 첨부는 프레임 사이에 그대로 남아 있으므로 옮기는 때는 다음 프레임의 컴퓨트 프리패스다(복사본을 프레임 끝에 따로 만들지 않는다).
         */
        RHITextureHandle   _depthHistory{ 0 };
        RHIDescriptorIndex _depthHistoryUAV{ kInvalidDescriptorIndex };
        float4x4           _depthHistoryViewProj{};           ///< 장면 깊이를 그린 프레임의 뷰-투영(그 프레임 끝에 적는다)
        float3             _depthHistoryEye{};                ///< 그 프레임의 눈 위치
        uint8              _bLastFrameDepthValid{ SW_FALSE }; ///< 장면 깊이 첨부에 이 뷰가 그린 깊이가 남아 있다(풀을 새로 만들면 내린다)
        /**
         * @brief 가림 타일 — 깊이 히스토리를 `FrameRendererUtil::kOcclusionTileSize` px 타일마다 최대 깊이 하나로 줄인 구조 버퍼(float, SRV + UAV)입니다. 컬링의 가림 판정(t2)이 읽는다.
         * @details 깊이 히스토리와 수명이 같다(`ensureOcclusionTiles`). `_bOcclusionTilesThisFrame` 은 이번 프레임에 타일을 지었는지다(`recordOcclusionTiles`).
         */
        RHIStructuredBufferSlot _occlusionTiles;
        uint32                  _occlusionColumns{ 0 };
        uint32                  _occlusionRows{ 0 };
        uint8                   _bOcclusionTilesThisFrame{ SW_FALSE };
        RHIStructuredBufferSlot _transparentRank;           ///< 이 뷰의 투명 순번 테이블(정렬 디스패치 t2). GPU 정렬 백엔드에서 쓴다.
        vector<uint32>          _listTransparentBatchOrder; ///< 이번 프레임 이 뷰의 투명 배치 순서(비면 주 순서를 쓴다)
        /// @brief 이번 프레임 이 뷰의 입자 그리기 순서와 자리(`_listTransparentBatchOrder` 안의 자리)입니다. `_bHasParticleOrder` 가 0 이면 주 시점의 것을 쓴다.
        vector<GPUTransparentParticleSlot> _listParticleSlot;
        uint8                              _bHasParticleOrder{ SW_FALSE }; ///< `_listParticleSlot` 이 이 뷰의 배치 순서와 짝인지
        vector<uint32>                     _listInstanceSlot;              ///< 지금 `_instanceSlotStream` 에 들어 있는 내용(같으면 다시 만들지 않는다)
        RHIBufferHandle                    _instanceSlotStream{ 0 };       ///< 이 뷰의 인스턴스 슬롯 스트림(꼬리만 뷰 순서). 컬링이 없는 백엔드(DX11)에서 쓴다.
        uint32                             _transparentTailBase{ 0 };      ///< 순번 테이블 0번의 인스턴스 번호
        /**
         * @brief 이 뷰의 방향광 그림자(셰이더 상수 · 캐스케이드 타일 · 컬링 절두체)입니다. 주 시점은 프레임의 것, 추가 뷰는 자기 카메라에 맞춘 것이다.
         * @details 추가 뷰가 주 시점 볼륨을 쓰면 다른 곳을 보는 CCTV · 분할 화면은 그림자가 빠진다(언리얼의 뷰별 그림자 뷰).
         */
        ViewShadowData _shadow;
        /// @brief 추가 뷰의 그림자 패스 컬링 입력(행렬 · 절두체 · 자기 컬링 상수 버퍼)입니다. 주 시점은 `_arrView[Shadow]` 를 씁니다.
        RenderView _shadowCullInput;
        uint32     _cullSlot{ 0 };
        uint32     _shadowCullSlot{ static_cast<uint32>( RenderViewType::Shadow ) }; ///< 그림자 패스가 쓰는 컬링 슬롯(주 시점은 1, 추가 뷰는 자기 슬롯)
        uint32     _outputWidth{ 0 };                                                ///< 출력 크기(렌더 텍스처, 화면 사각형)
        uint32     _outputHeight{ 0 };
        uint32     _shadowMapResolution{ 0 };                 ///< 풀이 가진 그림자 맵 한 변(0이면 아직 없음). 품질이 바뀌면 풀을 다시 만든다.
        uint32     _lightGridIndex{ invalid_index::kUint32 }; ///< 이번 프레임 이 뷰의 라이트 그리드 번호(`GPULightBuffer::getGridBase`). 없으면 셰이더는 목록 전체를 돈다
        /// @brief 지난번 이 뷰를 그린 뷰-투영입니다(모션 블러의 `g_PrevViewProj`). 기록을 버리는 프레임(`_bHistoryReset`)은 이번 것으로 둔다(속도 0).
        float4x4 _prevViewProj{};
        /**
         * @brief 자동 노출의 적응 EV 기록 둘(1x1 R32 실수 RT)과 그 SRV 입니다. 프레임마다 번갈아 한쪽을 읽고 다른 쪽(`_autoExposureWrite`)에 씁니다.
         * @details 풀과 수명이 같다 — `updateViewTransients` 가 만들고 `releaseViewTransients` 가 놓는다. 새로 만든 기록은 값이 없으므로 그 프레임은 기록을 버린다.
         */
        RHITextureHandle   _arrAutoExposure[2]{};
        RHIDescriptorIndex _arrAutoExposureSRV[2]{ kInvalidDescriptorIndex, kInvalidDescriptorIndex };
        /// @brief 이번 프레임의 측정 상수(최소 EV, 최대 EV, 밝아질 때 · 어두워질 때 목표로 다가가는 비율)입니다. 셋업(`prepareAutoExposure`)이 정하고 톤맵 패스는 읽기만 합니다.
        float4  _autoExposureParams{};
        float32 _autoExposureClock{ 0.0f }; ///< 지난번 이 뷰의 노출을 측정한 렌더러 시각(초, `getAnimationTime`)
        /**
         * @brief FSR 1 의 중간 RT(EASU 출력 = Present 뷰포트 크기, RGBA8)와 그 SRV 입니다. 이 뷰가 FSR 로 키울 때만 있다(`prepareUpscale`).
         * @details 크기는 Present 뷰포트(화면 사각형 · 자기 출력)다 — 풀(배율 크기)과 다르고 출력이 바뀌면 다시 만든다.
         */
        RHITextureHandle     _upscaleIntermediate{ 0 };
        RHIDescriptorIndex   _upscaleIntermediateSRV{ kInvalidDescriptorIndex };
        uint32               _upscaleWidth{ 0 };
        uint32               _upscaleHeight{ 0 };
        RenderViewOutputKind _outputKind{ RenderViewOutputKind::ScreenRect };
        uint8                _bRenderThisFrame{ SW_FALSE };
        uint8                _bSeenThisFrame{ SW_FALSE };
        uint8                _bHasTransparentRank{ SW_FALSE }; ///< 이번 프레임에 `_transparentRank` 로 정렬한다.
        uint8                _bUsesViewSlotStream{ SW_FALSE }; ///< 이번 프레임에 `_instanceSlotStream` 으로 그린다.
        /**
         * @brief 지금 풀로 이 뷰를 한 번 이상 그렸다 — 시간 누적 기록(TAA 히스토리 · `_prevViewProj` · 자동 노출 EV)에 지난 프레임 값이 있다.
         * @details 풀을 새로 만들면(`updateViewTransients`) 기록도 새것이라 내린다.
         */
        uint8 _bHasHistory{ SW_FALSE };
        /**
         * @brief 이번 프레임은 시간 누적 기록을 버린다 — 처음 그리는 프레임이거나 컷(`RenderViewSettings::_bCut`)이다(언리얼 `bCameraCut`).
         * @details `applyViewProjection` 이 프레임마다 한 번 정하고 TAA(이번 원본만) · 모션 블러(지난 뷰-투영 = 이번 것) · 자동 노출(측정값을 그대로)이 모두 이것 하나를 본다.
         *          기능마다 컷을 따로 읽으면 한 기능만 컷을 빠뜨린다. TAA 는 이것을 포함한 `_bTAAHistoryReset` 을 본다.
         */
        uint8 _bHistoryReset{ SW_TRUE };
        /**
         * @brief 이번 프레임은 TAA 기록만 더 버린다 — `_bHistoryReset` 이거나 지난 프레임이 TAA 기록을 쓰지 않았다(TAA 가 빠진 프레임, `_bTAAHistoryWritten`).
         * @details 빠진 TAA(후처리를 끈 뷰 · 품질 · 읽는 쪽 없음)는 출력이 원본을 가리키거나 없고 기록을 쓰지 않는다. 다시 도는 첫 프레임의 기록은 그 앞 화면이라 섞으면 그동안 움직인 것의 가장자리에 옛 그림이 남는다.
         *          `beginViewHistory` 가 `_bHistoryReset` 과 같은 자리에서 정한다(기능마다 따로 정하지 않는다).
         */
        uint8 _bTAAHistoryReset{ SW_TRUE };
        uint8 _bTAAHistoryWritten{ SW_FALSE };     ///< 이번 프레임의 TAA 가 돌아 기록을 쓴다 — 패스 고르기(`applyViewPassSelection`)의 결과 하나다. 다음 프레임의 `beginViewHistory` 가 읽는다
        uint8 _autoExposureWrite{ 0 };             ///< 이번 프레임에 쓰는 자동 노출 기록 칸(0 · 1). 톤맵은 이 칸을 읽는다
        uint8 _bAutoExposureThisFrame{ SW_FALSE }; ///< 이번 프레임에 이 뷰의 노출을 측정한다(`prepareAutoExposure`)
        uint8 _bAutoExposureMeasured{ SW_FALSE };  ///< 지금 자동 노출 기록에 측정한 값이 있다(기록을 새로 만들면 내린다 — 첫 측정은 지난 칸을 섞지 않는다)
        uint8 _bUpscaleThisFrame{ SW_FALSE };      ///< 이번 프레임에 Present 가 FSR 1(EASU + RCAS)로 키운다(`prepareUpscale`)
    };
} // namespace sw
