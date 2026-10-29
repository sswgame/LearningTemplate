/**
 * @file FrameRenderState.h
 * @brief 한 프레임을 셋업하는 쪽(`FrameRenderer`)과 기록하는 쪽(`FramePassExecutor`)이 함께 보는 프레임 데이터입니다 — GPUScene, 그래프, 뷰, 라이트 · 그림자 · 하늘, 패스 상수 링, 정점 변형, 캔버스 · 입자.
 * @details 셋업(기록 전)이 채우고 기록은 읽는다. 기록 중에 바뀌는 것은 뷰의 시간 기록(`_prevViewProj`)과 링의 커서뿐이고, 둘 다 패스 하나가 자기 뷰 · 슬롯만 만진다.
 *          소유는 렌더러가 하고 패스 실행은 참조로 받는다. 언리얼 `FSceneRenderer` 가 `FViewInfo` 목록과 장면 데이터를 들고, 패스(RDG 람다)가 그것을 잡아 읽는 나눔과 같다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/Canvas/CanvasRenderer.h"
#include "Engine/Renderer/Frame/FrameMeshDeformState.h"
#include "Engine/Renderer/Frame/FramePassContext.h"
#include "Engine/Renderer/Frame/Resource/PassConstantRing.h"
#include "Engine/Renderer/Frame/Timeline/GPUPassTimeline.h"
#include "Engine/Renderer/Frame/View/FrameViewTarget.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Renderer/Graph/RenderGraph.h"
#include "Engine/Renderer/Lighting/GPULightBuffer.h"
#include "Engine/Renderer/Lighting/Light2DCookieAtlas.h"
#include "Engine/Renderer/Lighting/RealtimeProbeUpdater.h"
#include "Engine/Renderer/Lighting/ShadowSetup.h"
#include "Engine/Renderer/Scene/GPUScene.h"
#include "Engine/Renderer/VFX/VFXParticleRenderer.h"

namespace sw
{
    class TaskManager;

    /**
     * @brief 이번 프레임의 주 광원 값입니다(`g_KeyLightDirIntensity` · `g_KeyLightColor`). 패킷이 값을 실어 오면 그 값을, 아니면 빛 없음을 씁니다.
     * @details 방향, 색, 세기, 앰비언트는 씬의 주 광원에서 온다. 기본값은 "주 광원 없음" 이다. 세기와 색과 앰비언트가 0 이라 그 씬은 어둡게 그려진다.
     *          방향은 단위 벡터로 둔다. 셰이더가 정규화하기 때문이다(water.hlsl 반사).
     */
    struct FrameLightState
    {
        float4 _dirIntensity{ 0.0f, -1.0f, 0.0f, 0.0f };
        float4 _colorAmbient{ 0.0f, 0.0f, 0.0f, 0.0f };
    };

    /**
     * @brief 이번 프레임의 하늘을 셰이더 상수 모양으로 둔 것입니다(`g_SkyZenith` · `g_SkyHorizon` · `g_SkyGround`, 배치는 `SkyGradient` 머리 주석).
     * @details 씬에 하늘이 없으면 `FrameRenderState::_bHasFrameSky` 가 0 이고 스카이 패스가 그리지 않는다 — 배경은 SceneColor 클리어 색이다(기본 하늘로 대신 칠하지 않는다).
     */
    struct FrameSkyState
    {
        float4 _zenithExponent{};  ///< rgb 천정 색, w 지평선 지수
        float4 _horizon{};         ///< rgb 지평선 색
        float4 _groundIntensity{}; ///< rgb 땅 색, w 세기
        // 대기 산란 모드(`SkyMode::Atmosphere`)와 구름 층의 상수다. 배치는 `SkyAtmosphere` · `SkyClouds` 머리 주석과 sky.hlsl 머리 주석.
        float4 _atmosphereRayleigh{}; ///< rgb 레일리 산란 계수(1/km), w 대기 모드면 1 · 그러데이션이면 0
        float4 _atmosphereMie{};      ///< x 미 산란 계수(1/km), y 미 규모 높이(km), z 미 이방성, w 해 복사휘도 배율(해 색에 곱한다)
        float4 _atmosphereGeometry{}; ///< x 행성 반지름(km), y 대기 두께(km), z 눈 고도(km), w 해 원반 각 반지름(라디안)
        float4 _cloudShape{};         ///< x 덮는 비율, y 층 고도(km), z 노이즈 배율(1/km), w 최대 불투명도
        float4 _cloudWind{};          ///< xy 바람 방향 단위 벡터(xz 평면), z 바람 속도(km/s), w 경계 부드러움
        float4 _cloudColor{};         ///< rgb 구름 색, w 구름을 그리면 1
    };

    /**
     * @struct FrameRenderState
     * @brief 렌더러와 패스 실행이 함께 보는 프레임 데이터입니다. 렌더 스레드만 만집니다.
     * @details 행동(셋업 순서 · 기록)은 두 쪽이 가지고 이 형식은 데이터와 그 데이터에서 바로 나오는 조회만 둔다.
     */
    struct FrameRenderState
    {
        GPUScene _gpuScene{};
        /** @brief 파이프라인 XML 로 만든 렌더 그래프입니다. 렌더러가 다시 만들고(`loadPipeline`) 패스 실행이 콜백을 묶고 돌립니다. */
        RenderGraph _graph{};
        /// @brief 주 시점입니다. 파이프라인이 선언한 첨부(창 크기로 만들고 구성이 바뀔 때만 다시 만든다), TAA 히스토리, 직렬 리스트, 출력 사각형을 가집니다.
        FrameViewTarget _mainView{};
        /// @brief 추가 뷰들입니다. 카메라 id 순서가 아니라 처음 본 순서로 쌓습니다. 뷰마다 자기 풀과 컬링 슬롯을 가집니다.
        vector<unique_ptr<FrameViewTarget>> _listExtraView{};
        /**
         * @brief 이번 프레임의 뷰들입니다. 행렬, 절두체, 상수 버퍼를 각자 소유합니다.
         * @details 뷰를 얻으면 그 뷰의 값이 함께 따라온다. 주의: 이 값들을 뷰 밖 멤버로 흩어 두면 "이 값은 어느 뷰
         *          것인가" 를 사람이 기억해야 하고, 컬링 상수 버퍼를 뷰끼리 나눠 쓰게 되어 뒤 업로드가 앞 디스패치를 덮어쓴다.
         */
        RenderView _arrView[static_cast<uint32>( RenderViewType::Count )]{};
        /**
         * @brief 주 시점 방향광 캐스케이드마다의 그림자 컬링 입력(절두체 · 자기 컬링 상수 버퍼)입니다. 앞 `_cascadeCullCount` 개만 씁니다.
         * @details 컬링 슬롯은 추가 뷰 슬롯 뒤에 붙는다(`_cascadeCullSlotBase` + 캐스케이드 번호). 추가 뷰(CCTV · PiP)는 작은 장면이라 합친 칸 하나를 그대로 쓴다.
         */
        RenderView _arrCascadeCullInput[shaderslot::kShadowCascadeMax]{};
        uint32     _cascadeCullSlotBase{ 0 }; ///< 캐스케이드 컬링 슬롯의 첫 번호
        uint32     _cascadeCullCount{ 0 };    ///< 이번 프레임 캐스케이드 컬링 슬롯 수. 0 이면 합친 칸(`_shadowCullSlot`) 하나로 모든 타일을 거른다
        /**
         * @brief 프레임 단위 패스 상태입니다. 직렬 경로에서 쓰며 병렬 패스의 시드가 됩니다.
         * @details 병렬 기록에서는 패스마다 이것을 복사해 각자의 커맨드 리스트와 상수 버퍼를 붙인다.
         */
        FramePassContext _frameContext{};
        /// @brief 주 시점의 패스 시드입니다. 추가 뷰는 이것에서 출발해 자기 값(뷰 행렬, 플래그, 컬링 슬롯)만 덮어씁니다. 프레임마다 대입하므로 용량이 남습니다.
        FramePassContext _mainSeedScratch{};
        /// @brief 패스와 드로우별 상수 버퍼 슬롯 링입니다. 병렬 기록에서 드로우마다 하나씩 가져갑니다.
        PassConstantRing _passCbRing{};
        /// @brief 정점 변형(인스턴스 애니메이션, 모프, 스킨, VAT) 컴퓨트의 상수 버퍼, 풀, 스크래치, 시계입니다.
        FrameMeshDeformState _deform{};
        /// @brief 씬 라이트 구조 버퍼입니다. RT 가 소유하며 포워드와 디퍼드가 같은 버퍼를 읽습니다.
        GPULightBuffer _lightBuffer{};
        /**
         * @brief 이번 프레임의 그림자 배치(아틀라스 칸 · 주 시점 캐스케이드 · 점광 · 스폿 타일)입니다.
         * @details 패킷 경로는 패킷과 맞바꾸기로 받고, 씬 직접 경로는 여기에 바로 채운다. 기록 중에는 읽기만 한다.
         */
        FrameShadowData _frameShadow{};
        /// @brief 이번 프레임 실시간 프로브 갱신 요청입니다(패킷 경로도 여기에 받는다). 기록은 읽기만 합니다.
        vector<RealtimeProbeRequest> _listFrameRealtimeProbe{};
        RealtimeProbeUpdater         _probeUpdater{};   ///< 실시간 프로브 갱신의 버퍼와 컴퓨트 기록(`FrameRenderer::prepareRealtimeProbes` · `FramePassExecutor::dispatchRealtimeProbes`)
        SkyGradient                  _frameSkySource{}; ///< 이번 프레임 하늘의 원본 값입니다(실시간 프로브 갱신이 하늘 SH 를 투영한다). `_bHasFrameSky` 가 0 이면 기본값
        /** @brief 2D 빛 쿠키 아틀라스입니다(칸 나누기 · 쿠키 텍스처 수명 · 아틀라스 렌더 타깃). */
        Light2DCookieAtlas _cookieAtlas{};
        /// @brief 캔버스(화면 2D) 렌더러입니다. 아틀라스 사본, 사각형 버퍼, 일괄 드로우, 렌더 텍스처 대상(월드 공간 UI)을 렌더 스레드에서 처리합니다.
        CanvasRenderer _canvasRenderer{};
        /// @brief 파티클 렌더러입니다. 입자 버퍼와 입자 텍스처를 렌더 스레드에서 처리하고, 투명 패스가 방출기마다 빌보드를 그립니다.
        VFXParticleRenderer _particleRenderer{};
        /// @brief 그릴 캔버스입니다. 패킷과 `FrameRenderer::setCanvasFrame` 에서 맞바꾸기로 받으며, 기록 중에는 읽기만 합니다.
        CanvasFrameData _canvasFrame{};
        /**
         * @brief 패스별 GPU 시간입니다. 지난 프레임의 타임스탬프를 읽어 프로파일러(`GPU.<패스>`)와 Tracy GPU 타임라인으로 냅니다.
         * @details 프로파일러는 `FrameRenderer::initialize` 가 주입한다. 타임스탬프를 쓸지(`isRecording`)도 여기에 묻는다.
         *          GPU 타임스탬프가 없으면 GPU 비용을 `RT.BeginFrame`(백프레셔) 같은 대리값으로 추측하거나 패스를 하나씩 지워 가며
         *          차이로 구해야 한다. 그런 추측(클리어, 포맷 비용 등)은 쉽게 틀린다.
         */
        GPUPassTimeline _gpuPassTimeline{};
        /// @brief 병렬 기록(`RenderGraph::executeParallel`)이 쓰는 태스크 매니저입니다. 없으면 직렬로 기록합니다.
        TaskManager*     _pTaskManager{ nullptr };
        RHITextureHandle _outputRenderTarget{ 0 }; ///< 주 출력 RT(에디터 게임 뷰). 0 이면 백버퍼
        float4           _clearColor{ 0.12f, 0.15f, 0.18f, 1.0f };
        FrameLightState  _frameLight{}; ///< float4 둘(32바이트)이다.
        FrameSkyState    _frameSky{};   ///< `_bHasFrameSky` 가 0 이면 쓰지 않는다.
        /// @brief 주 출력(백버퍼, 게임 뷰 RT)의 크기입니다. 주 시점의 풀은 이 크기 × 사각형 × 해상도 배율이고, 화면 사각형 뷰와 Present 캡처는 이 크기를 씁니다.
        uint32 _outputWidth{ 0 };
        uint32 _outputHeight{ 0 };
        uint8  _bHasFrameSky{ SW_FALSE }; ///< 이번 프레임의 씬에 하늘이 있다(`_frameSky`). 기록 전에 정하고 패스 콜백은 읽기만 한다

        /** @brief 뷰 하나를 얻습니다. 그 뷰의 행렬, 절두체, 상수 버퍼가 함께 옵니다. */
        RenderView&       view( RenderViewType type ) { return _arrView[static_cast<uint32>( type )]; }
        const RenderView& view( RenderViewType type ) const { return _arrView[static_cast<uint32>( type )]; }
        /** @brief 뷰 @p target 의 컬링 입력입니다. 주 시점은 `_arrView[Main]`, 추가 뷰는 자기 것입니다. */
        RenderView& cullInputOf( FrameViewTarget& target ) { return &target == &_mainView ? view( RenderViewType::Main ) : target._cullInput; }
        /** @brief 시간으로 구동하는 컴퓨트 · 노출이 읽을 절대 시간(초)입니다. 렌더러 시계(`FrameMeshDeformState::_animationTimer`)의 누적 시간입니다. */
        float32 getAnimationTime() const { return _deform._animationTimer.getTotalTime(); }
    };
} // namespace sw
