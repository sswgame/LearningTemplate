/**
 * @file FrameRenderState.h
 * @brief 한 프레임을 셋업하는 쪽(`FrameRenderer`)과 기록하는 쪽(`FramePassExecutor`)이 함께 보는 프레임 데이터입니다 — GPUScene, 그래프, 뷰, 라이트 · 그림자 · 하늘, 패스 상수 링, 정점 변형, 캔버스 · 입자.
 * @details 셋업(기록 전)이 채우고 기록은 읽는다. 기록 중에 바뀌는 것은 뷰의 시간 기록(`_prevViewProj`)과 링의 커서뿐이고, 둘 다 패스 하나가 자기 뷰 · 슬롯만 만진다.
 *          소유는 렌더러가 하고 패스 실행은 참조로 받는다. 언리얼 `FSceneRenderer` 가 `FViewInfo` 목록과 장면 데이터를 들고, 패스(RDG 람다)가 그것을 잡아 읽는 나눔과 같다.
 */
#pragma once
#include "Core/Container/EnumArray.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/TimeRollover.h"

#include "Engine/Environment/Sky/SkyGradient.h"
#include "Engine/Environment/Surface/SurfaceDeformBinding.h"
#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/Debug/GPUMessage.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Renderer/Canvas/CanvasRenderer.h"
#include "Engine/Renderer/Frame/FrameMeshDeformState.h"
#include "Engine/Renderer/Frame/FramePassContext.h"
#include "Engine/Renderer/Frame/Packet/RenderFrameSettings.h"
#include "Engine/Renderer/Frame/Resource/PassConstantRing.h"
#include "Engine/Renderer/Frame/Resource/PathTextureSet.h"
#include "Engine/Renderer/Frame/Timeline/GPUFrameClock.h"
#include "Engine/Renderer/Frame/Timeline/GPUPassTimeline.h"
#include "Engine/Renderer/Frame/View/FrameViewTarget.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Renderer/Graph/RenderGraph.h"
#include "Engine/Renderer/Graph/RenderGraphAliasPlan.h"
#include "Engine/Renderer/Lighting/GPULightBuffer.h"
#include "Engine/Renderer/Lighting/LensFlareSpriteBuffer.h"
#include "Engine/Renderer/Lighting/Light2DCookieAtlas.h"
#include "Engine/Renderer/Lighting/RealtimeProbeUpdater.h"
#include "Engine/Renderer/Lighting/ShadowCache.h"
#include "Engine/Renderer/Lighting/ShadowSetup.h"
#include "Engine/Renderer/Scene/GPUScene.h"
#include "Engine/Renderer/VFX/VFXGPUMeshReadback.h"
#include "Engine/Renderer/VFX/VFXParticleRenderer.h"
#include "Engine/Visual/Component/DecalComponent.h"

namespace sw
{
    class RenderDebugRegistry;
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
        float4 _skyLight{};           ///< 스카이 라이트의 프로브 버퍼 자리 — x 옥타헤드럴 한 변, y 거칠기 단계 수(0 이면 없음), z 첫 원소 번호(`LightProbeElementBuilder::getSkyLight`)
    };

    /**
     * @struct FrameRenderState
     * @brief 렌더러와 패스 실행이 함께 보는 프레임 데이터입니다. 렌더 스레드만 만집니다.
     * @details 행동(셋업 순서 · 기록)은 두 쪽이 가지고 이 형식은 데이터와 그 데이터에서 바로 나오는 조회만 둔다.
     */
    struct FrameRenderState
    {
        /**
         * @brief 이번 프레임의 렌더 설정 사본입니다(게임 스레드가 뜬 `RenderFramePacket::_settings`). 렌더러 · 패스 실행 · 기록 워커는 전역 변수 대신 이것을 읽는다.
         * @details 패킷을 받을 때(`FrameRenderer::executePacket`) 바뀌고 기록 중에는 그대로라 한 프레임의 패스가 모두 같은 값을 본다. 기동 · 화면 밖 렌더러는 기동 때 받은 사본이다.
         */
        RenderFrameSettings _settings{};
        GPUScene            _gpuScene{};
        /** @brief 파이프라인 XML 의 `_listPass` 로 만든 뷰 그래프입니다. 뷰마다 다시 돈다. 렌더러가 다시 만들고(`loadPipeline`) 패스 실행이 콜백을 묶고 돌립니다. */
        RenderGraph _graph{};
        /** @brief 파이프라인 XML 의 `_listFramePass` 로 만든 프레임 그래프입니다. 프레임에 한 번, 뷰 그래프보다 먼저 돈다(`FramePassExecutor::recordFrameGraph`). */
        RenderGraph _frameGraph{};
        /** @brief 렌더링 런타임 디버그 우편함(프레임 디버거 · 통계 · 그래프 사본 · 버퍼 보기)입니다. 주 렌더러만 호스트가 건다 — 화면 밖 렌더러는 nullptr 이라 기록하지 않는다. */
        RenderDebugRegistry* _pRenderDebug{ nullptr };
        /**
         * @brief 뷰 그래프의 트랜지언트 앨리어싱 계획입니다(레벨 수명이 겹치지 않고 서술이 같은 첨부가 텍스처 하나를 나눈다 — `RenderGraphAliasPlan::make`).
         * @details 콜백을 묶을 때(`FramePassExecutor::bindPassCallbacks`) 세우고 세대를 올린다. 뷰 풀은 세대가 다르면 다시 만든다(`FrameViewResources::updateViewTransients`).
         *          파이프라인을 다시 읽으면 비우고 세대를 올린다 — 새 파이프라인의 첫 프레임은 앨리어싱 없이 만든다.
         */
        RenderGraphAliasPlan _transientAliasPlan{};
        uint32               _transientAliasGeneration{ 0 };
        /// @brief 주 시점입니다. 파이프라인이 선언한 첨부(창 크기로 만들고 구성이 바뀔 때만 다시 만든다), TAA 히스토리, 직렬 리스트, 출력 사각형을 가집니다.
        FrameViewTarget _mainView{};
        /// @brief 추가 뷰들입니다. 카메라 id 순서가 아니라 처음 본 순서로 쌓습니다. 뷰마다 자기 풀과 컬링 슬롯을 가집니다.
        vector<unique_ptr<FrameViewTarget>> _listExtraView{};
        /**
         * @brief 이번 프레임의 뷰들입니다. 행렬, 절두체, 상수 버퍼를 각자 소유합니다.
         * @details 뷰를 얻으면 그 뷰의 값이 함께 따라온다. 주의: 이 값들을 뷰 밖 멤버로 흩어 두면 "이 값은 어느 뷰
         *          것인가" 를 사람이 기억해야 하고, 컬링 상수 버퍼를 뷰끼리 나눠 쓰게 되어 뒤 업로드가 앞 디스패치를 덮어쓴다.
         */
        EnumArray<RenderViewKind, RenderView> _arrView{};
        /**
         * @brief 주 시점 방향광 캐스케이드마다의 그림자 컬링 입력(절두체 · 자기 컬링 상수 버퍼)입니다. 앞 `_cascadeCullCount` 개만 씁니다.
         * @details 컬링 슬롯은 추가 뷰 슬롯 뒤에 붙는다(`_cascadeCullSlotBase` + 캐스케이드 번호). 추가 뷰(CCTV · PiP)는 작은 장면이라 합친 칸 하나를 그대로 쓴다.
         */
        RenderView _arrCascadeCullInput[shaderslot::kShadowCascadeMax]{};
        uint32     _cascadeCullSlotBase{ 0 }; ///< 캐스케이드 컬링 슬롯의 첫 번호
        uint32     _cascadeCullCount{ 0 };    ///< 이번 프레임 캐스케이드 컬링 슬롯 수. 0 이면 합친 칸(`_shadowCullSlot`) 하나로 모든 타일을 거른다
        /**
         * @brief 그림자를 드리우는 점광 · 스폿마다의 그림자 컬링 입력입니다(감사 RN-5 — 예전에는 모든 점광 · 스폿 타일이 방향광 볼륨까지 합친 칸 하나를 그렸다).
         * @details 슬롯은 캐스케이드 슬롯 뒤에 붙는다(`_localShadowCullSlotBase` + 빛 번호). 빛 번호는 `FrameShadowData::_listLocalBound` 의 번호(우선순위 순서)이고
         *          앞 `_localShadowCullCount` 개 빛만 자기 슬롯을 가진다(상한 `RenderConfig::_shadowLocalCullSlotLimit`). 나머지는 합친 칸이다.
         *          목록은 줄이지 않는다 — 슬롯 수가 바뀌면 GPU 장면을 통째로 다시 올리므로 빛이 들고 나도 칸 수를 지킨다.
         */
        vector<RenderView> _listLocalShadowCullInput{};
        uint32             _localShadowCullSlotBase{ 0 }; ///< 점광 · 스폿 컬링 슬롯의 첫 번호
        uint32             _localShadowCullCount{ 0 };    ///< 이번 프레임에 자기 컬링 슬롯을 가진 빛 수
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
        /// @brief 서 있는 점광 · 스폿의 그림자 캐시(규칙 · 캐시 층)입니다. `FrameRenderer::applyFrameShadow` 가 프레임마다 정하고 그림자 패스가 그린다(`_settings._bShadowCache`).
        ShadowCache _shadowCache{};
        /// @brief 이번 프레임 실시간 프로브 갱신 요청입니다(패킷 경로도 여기에 받는다). 기록은 읽기만 합니다.
        vector<RealtimeProbeRequest> _listFrameRealtimeProbe{};
        RealtimeProbeUpdater         _probeUpdater{}; ///< 실시간 프로브 갱신의 버퍼와 컴퓨트 기록(`FrameRenderer::prepareRealtimeProbes` · `FramePassExecutor::dispatchRealtimeProbes`)
        /// @brief 이번 프레임 렌즈 플레어를 단 빛들입니다(패킷 경로가 받는다). 기록은 읽기만 합니다.
        vector<LensFlareRequest> _listFrameLensFlare{};
        /** @brief 이번 프레임의 데칼입니다(패킷 `_listDecal` — 그리는 차례). 셋업(`FrameRenderer::prepareFrameDecals`)이 텍스처를 풀어 `_listFrameDecalDraw` 를 만든다. */
        vector<DecalDrawData> _listFrameDecal{};
        /**
         * @brief 데칼 구조 버퍼(decal.hlsl `g_SwDecals`)의 원소 하나입니다 — 셰이더 `DecalData` 와 같은 배치(176 바이트, 행렬은 행 우선 -Zpr).
         * @details 텍스처 번호는 네이티브 bindless 면 SRV 번호, 에뮬(DX11 · GL · WebGPU)이면 머티리얼 텍스처 칸 순번(0 · 1 — 그리기의 `_arrTextureSRV` 를 그 칸에 건다)이다(MaterialTexture::sampleIndex 와 같은 뜻).
         */
        struct GPUDecal
        {
            float4x4 _world{};        ///< 상자 월드 행렬(행 = 축)
            float4x4 _worldToLocal{}; ///< 월드 → 상자 공간
            float4   _color{};        ///< rgb 알베도 곱, a 불투명도
            float4   _surface{};      ///< x 거칠기, y 금속도, z 노멀 세기, w 각도 페이드 시작(cos)
            uint32   _channels{ 0 };  ///< 덮는 칸(DecalChannels 비트)
            uint32   _albedoTexture{ kInvalidDescriptorIndex };
            uint32   _normalTexture{ kInvalidDescriptorIndex };
            uint32   _padding{};
        };
        static constexpr uint32 kGPUDecalShaderBytes = 176; ///< decal.hlsl DecalData 의 크기(행렬 둘 + float4 셋)
        static_assert( sizeof( GPUDecal ) == kGPUDecalShaderBytes, "GPUDecal must match decal.hlsl DecalData" );
        /**
         * @brief 이번 프레임에 그릴 데칼 하나입니다. 값은 데칼 구조 버퍼(`_decalBuffer`)의 같은 번호 원소이고, Decal 패스가 텍스처가 같은 이웃끼리 인스턴스 그리기 하나로 그린다
         *        (`FramePassExecutor::executeDecalPass` — 네이티브 bindless 는 모두 하나).
         * @details `_arrTextureSRV` 는 에뮬 백엔드가 머티리얼 텍스처 칸에 거는 SRV 이고, 네이티브 bindless 는 검사에만 쓴다.
         */
        struct FrameDecalDraw
        {
            GPUDecal           _element{};
            RHIDescriptorIndex _arrTextureSRV[shaderslot::kMaterialTextureCount] = {
                kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex,
                kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex,
                kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex, kInvalidDescriptorIndex };
        };
        vector<FrameDecalDraw> _listFrameDecalDraw{};
        vector<GPUDecal>       _listDecalUploadScratch{}; ///< 데칼 원소를 이어 올리는 자리(`FrameRenderer::prepareFrameDecals`) — 프레임마다 다시 잡지 않는다
        RHIStructuredBuffer    _decalBuffer{};            ///< 이번 프레임 데칼 원소(decal.hlsl `g_SwDecals`) — 셋업이 기록 전에 한 번 올리고 Decal 패스가 읽는다(객체 빈도 데이터를 패스 상수에서 뺐다)
        LensFlareSpriteBuffer  _lensFlare{};              ///< 주 시점 렌즈 플레어 스프라이트의 버퍼(`FrameRenderer` 가 기록 전에 채우고 `FramePassExecutor::executeLensFlarePass` 가 그린다)
        SkyGradient            _frameSkySource{};         ///< 이번 프레임 하늘의 원본 값입니다(실시간 프로브 갱신이 하늘 SH 를 투영한다). `_bHasFrameSky` 가 0 이면 기본값
        hashed_string          _skyHdriPath{};            ///< 지금 쥔 HDRI 하늘 텍스처의 경로입니다(`FrameRenderer::applyFrameSky`). 비었으면 쥔 것이 없다
        TextureHandle          _skyHdriLoad{};            ///< HDRI 하늘 텍스처의 로드 핸들입니다(참조를 쥔다). 스카이 패스가 준비된 것만 읽는다
        SurfaceDeformBinding   _surfaceDeform{};          ///< 이번 프레임의 표면 변형 층(패킷에서 — 핸들이 참조를 쥔다). 준비된 텍스처는 모든 패스가 건다(`FramePassExecutor::registerSurfaceDeformLayer`), 창은 프레임 시드(`FrameRenderer::updatePassConstants`)
        /** @brief 2D 빛 쿠키 아틀라스입니다(칸 나누기 · 쿠키 텍스처 수명 · 아틀라스 렌더 타깃). */
        Light2DCookieAtlas _cookieAtlas{};
        /** @brief 셋업이 경로로 빌리는 텍스처(컬러 그레이딩 LUT · 블룸 먼지 마스크 · 데칼 · 렌즈 플레어 스프라이트)의 로드 핸들입니다. 셋업이 뷰마다 묻고 프레임 시작에 안 쓰인 것을 놓는다. */
        PathTextureSet _pathTextures{};
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
        /**
         * @brief 프레임 하나의 GPU 시간(Shipping 포함)이고 타임스탬프 계측 스위치의 주인입니다. 자동 렌더 배율이 게임 스레드에서 읽습니다(`getLatest`).
         * @details 읽은 프레임 한 벌을 `_gpuPassTimeline` 도 받는다(쿼리는 한 벌). 타임스탬프 슬롯은 디바이스에 하나라 주 렌더러만 읽는다.
         */
        GPUFrameClock _gpuFrameClock{};
        /**
         * @brief 셰이더 메시지 링(`-gv_gpuMessages`)입니다. 컴퓨트 프리패스(`FramePrepass` — 컬링 · 모프 · 마커)와 그래프의 그래픽스 패스(`FramePassExecutor::bindForDraw`)가 함께 u4 로 쓴다.
         * @details 프리패스가 프레임 앞에서 선택을 올리고(`updateControl`) 앞 프레임들의 기록을 로그로 내며(`poll`), 프리패스 끝에서 되읽기를 요청한다 — 그래픽스 패스의 기록은 다음 프레임의 사본에 실린다(누적 카운터라 잃지 않는다).
         *          꺼져 있으면 비어 있다.
         */
        GPUMessageChannel _gpuMessages{};
        /** @brief GPU 메시 입자의 살아 있는 수 · 주 시점이 그린 수의 되읽기입니다(자동화 감시 값 `VFX.GPUMesh*`). 프레임 끝에 요청하고 프레임 앞에서 도착한 사본을 낸다. */
        VFXGPUMeshReadback _vfxMeshReadback{};
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
        RenderView&       view( RenderViewKind kind ) { return _arrView[kind]; }
        const RenderView& view( RenderViewKind kind ) const { return _arrView[kind]; }
        /** @brief 뷰 @p target 의 컬링 입력입니다. 주 시점은 `_arrView[Main]`, 추가 뷰는 자기 것입니다. */
        RenderView& getCullInputOf( FrameViewTarget& target ) { return &target == &_mainView ? view( RenderViewKind::Main ) : target._cullInput; }
        /**
         * @brief 시간으로 구동하는 컴퓨트가 읽을 시간(초)입니다. 렌더러 시계(`FrameMeshDeformState::_animationTimer`)의 누적 시간을 스냅샷의 주기로 접은 값입니다.
         * @details 셰이더 칸이 float 라 접지 않으면 긴 실행에서 끊긴다(감사 C-12). 이 함수를 부르는 디스패치는 업로드된 스냅샷이 있을 때만 돈다.
         */
        float32 getAnimationTime() const { return TimeRolloverUtil::fold( _deform._animationTimer.getTotalTime(), _gpuScene.getShaderTimeRolloverSeconds() ); }
    };
} // namespace sw
