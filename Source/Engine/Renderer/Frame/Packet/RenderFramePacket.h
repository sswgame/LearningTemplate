/**
 * @file RenderFramePacket.h
 * @brief 게임 스레드 → 렌더 스레드로 넘기는 프레임 제출 데이터입니다.
 */
#pragma once
#include "Core/Path/AbsolutePath.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Environment/Fog/FogSettings.h"
#include "Engine/Environment/Sky/SkyGradient.h"
#include "Engine/Environment/Surface/SurfaceDeformBinding.h"
#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/Graphics/Material/MaterialGlobals.h"
#include "Engine/Graphics/View/SimulationOnlyFrameQueue.h"
#include "Engine/Renderer/Frame/Packet/RenderFrameSettings.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Renderer/Lighting/GPULightBuffer.h"
#include "Engine/Renderer/Lighting/LensFlareSpriteBuffer.h"
#include "Engine/Renderer/Lighting/ShadowSetup.h"
#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"
#include "Engine/Visual/Component/DecalComponent.h"

namespace sw
{
    /**
     * @struct RenderFramePacketDeltas
     * @brief 패킷의 일회성 차분 · 알림입니다(지난 패킷 뒤에 생긴 것 — 다음 패킷이 다시 싣지 않는다).
     * @details 패킷은 그리지 않고 끝날 수 있다(시점 없음 · 컨텍스트 실패 · 그래프 준비 전 · 시뮬레이션 전용). 그래서 렌더 스레드가 받는 즉시 이 묶음을 **통째로** 옮겨
     *          자기 대기 묶음(`RenderThread::_pendingDeltas`)에 쌓고, 쓰는 쪽은 그 대기 묶음에서 꺼낸다 — 칸마다 패킷 머리에서 손으로 받던 규칙을 형식이 대신한다.
     *          새 차분은 여기에 칸을 더한다. `appendFrom` · `clear` 가 칸을 빠뜨리면 아래 크기 검사가 컴파일에서 멈춘다. 언리얼 `ENQUEUE_RENDER_COMMAND` 가
     *          차분을 버릴 수 없는 명령으로 보내는 것과 같은 자리다.
     */
    struct RenderFramePacketDeltas
    {
        /// @brief 지난 패킷 뒤 바뀐 글리프 아틀라스 구간입니다(`GlyphAtlas::takeUploads` 한 번이 묶음 하나 — 이어 붙여도 묶음 번호 순서가 남는다).
        vector<GlyphAtlasUpload> _listAtlasUpload;
        /// @brief 업로드를 낸 아틀라스입니다. 렌더러 거울이 묶음을 놓쳤을 때 `GlyphAtlas::requestFullUpload`(원자 표시) 하나만 부른다 — 다른 멤버는 렌더 스레드에서 읽지 않는다.
        GlyphAtlas* _pAtlasSource{ nullptr };
        /**
         * @brief 지난 패킷 뒤에 사라진 미리보기 씬의 키입니다. 렌더 스레드가 그 씬의 렌더러를 내린다(`OffscreenSceneRenderers::retire`).
         * @details 렌더러의 수명은 씬의 수명이다 — 창을 가리거나 닫아 이번 프레임에 그리지 않을 뿐인 씬의 렌더러(파이프라인 · 그래프 · 트랜지언트)는 남긴다(언리얼 `FScene`).
         */
        vector<uint32> _listRetiredSceneKey;

        /** @brief @p inoutSource 의 칸을 모두 뒤에 이어 붙이고 @p inoutSource 를 비웁니다(저장소는 남긴다). */
        void appendFrom( RenderFramePacketDeltas& inoutSource )
        {
            _listAtlasUpload.insert( _listAtlasUpload.end(), std::make_move_iterator( inoutSource._listAtlasUpload.begin() ),
                                     std::make_move_iterator( inoutSource._listAtlasUpload.end() ) );
            if ( inoutSource._pAtlasSource != nullptr )
                _pAtlasSource = inoutSource._pAtlasSource;
            _listRetiredSceneKey.insert( _listRetiredSceneKey.end(), inoutSource._listRetiredSceneKey.begin(), inoutSource._listRetiredSceneKey.end() );
            inoutSource.clear();
        }
        /** @brief 비웁니다(용량은 남깁니다). */
        void clear()
        {
            _listAtlasUpload.clear();
            _pAtlasSource = nullptr;
            _listRetiredSceneKey.clear();
        }
    };
    // 칸을 더하면 이 합도 바꾼다 — 그 전에 `appendFrom` · `clear` 에 그 칸을 더했는지 본다(빠뜨리면 그리지 않은 패킷에서 차분이 사라진다).
    static_assert( sizeof( RenderFramePacketDeltas ) == sizeof( vector<GlyphAtlasUpload> ) + sizeof( GlyphAtlas* ) + sizeof( vector<uint32> ),
                   "RenderFramePacketDeltas 에 칸을 더했으면 appendFrom · clear 에도 더한다" );

    /// @brief 게임 스레드 → 렌더 스레드로 넘기는 한 프레임 스냅샷입니다.
    struct RenderFramePacket
    {
        GPUSceneSnapshot _gpuScene; ///< GT 가 만든 것 모두. 소유권도 함께 실린다(GPUSceneSnapshot 참고)
        /** @brief 이 프레임의 렌더 설정 사본입니다 — 게임 스레드가 패킷을 짓기 시작할 때 뜬다(`RenderPacketBuilder::beginPacket`). 렌더 쪽은 전역 변수 대신 이것을 읽는다. 화면 밖 패킷은 주 패킷의 것을 받는다. */
        RenderFrameSettings _settings;
        float4              _clearColor;
        /** @brief 주 시점의 눈 위치입니다. `_bHasViewProj` 가 0 이면 값이 없다(0 벡터) — 기본 카메라 위치로 채우지 않는다(그 패킷은 그리지 않는다). */
        float3   _cameraPos;
        float4x4 _viewProj;
        /**
         * @brief 이 프레임의 그림자 배치입니다. 아틀라스 슬롯, 주 시점의 방향광 캐스케이드(`_mainView`), 점광과 스폿 타일 및 그 라이트 버퍼 원소를 담습니다.
         * @details 주 시점 그림자는 그림자 빛이 없어도 유효합니다(그림자 없는 뷰). 씬을 그리지 않은 프레임만 비어 있고 렌더러가 그림자 없는 뷰로 둡니다. 렌더러가 맞바꾸기로 받습니다(저장소가 돕니다).
         */
        FrameShadowData _shadow;
        /** @brief xyz = 빛이 나아가는 방향, w = 세기입니다. */
        float4 _lightDirIntensity;
        /** @brief rgb = 빛 색, a = 환경광입니다. */
        float4 _lightColorAmbient;
        /**
         * @brief 이 프레임의 **모든** 라이트(방향광 + 점광)입니다. 값으로 싣습니다.
         * @details 렌더 스레드는 씬을 볼 수 없으므로 라이트도 패킷으로만 옵니다.
         *          위의 `_light*` 셋은 **키라이트 하나**로, 그림자 행렬과 앰비언트가 거기서 나옵니다.
         *          라이트 목록이 비어도 키라이트 하나로 그리는 폴백 경로이기도 합니다.
         */
        vector<GPULight> _listLight;
        /**
         * @brief 라이트 프로브 볼륨들의 SH9 원소 · 반사 프로브 텍셀입니다(`LightProbeElementBuilder`). 볼륨 원소는 `_listLight` 에 있고 이 묶음의 첫 프로브를 가리킵니다.
         * @details 불변 공유 값이다 — 내용이 그대로면 프레임마다 같은 포인터라 렌더 스레드가 다시 올리지 않는다. 없으면 nullptr 입니다.
         */
        shared_ptr<const vector<float4>> _pLightProbe;
        /** @brief 쿠키(빛 텍스처)를 쓰는 빛들입니다(`collectSceneLights`). 렌더러가 쿠키 아틀라스 칸을 나눠 `_listLight` 원소의 `_extra.w` 에 적습니다. */
        vector<GPULightCookie> _listLightCookie;
        /** @brief 실시간으로 갱신할 라이트 프로브 볼륨들의 요청입니다(`collectSceneLights`). 볼륨 원소는 `_listLight` 에 있고 복셀 격자는 공유 포인터로 실린다(렌더 스레드는 읽기만 한다). */
        vector<RealtimeProbeRequest> _listRealtimeProbe;
        /** @brief 렌즈 플레어를 단 빛들입니다(`RenderPacketSceneCollector::collectLensFlares`). 렌더 스레드가 주 시점에 투영해 스프라이트로 펼친다. */
        vector<LensFlareRequest> _listLensFlare;
        /** @brief 씬의 데칼(`DecalComponent`)입니다 — 그리는 차례(`_sortOrder`, 같으면 등록 순서)로 정렬해 싣는다(`RenderPacketSceneCollector::collectDecals`). */
        vector<DecalDrawData> _listDecal;
        /** @brief 씬의 하늘(`SkyComponent`)입니다. `_bHasSky` 가 0 이면 값이 없고 스카이 패스가 그리지 않는다(배경은 SceneColor 클리어 색). */
        SkyGradient _sky;
        /**
         * @brief 씬의 스카이 라이트가 프로브 버퍼(`_pLightProbe`)에서 차지한 자리입니다(`LightProbeElementBuilder::getSkyLight` → PassCB `g_SkyLight`).
         *        하늘이 없거나 거르지 못했으면 모두 0 이다 — 그 장면의 환경광은 앰비언트 색이다.
         */
        float4 _skyLight;
        /** @brief 씬의 안개(`FogComponent`, 카메라가 물속이면 물속 안개로 덮은 값)입니다. `_bHasFog` 가 0 이면 값이 없고 안개 패스가 그리지 않는다. */
        FogSettings _fog;
        /** @brief 씬의 표면 변형 층(`SurfaceDeformSystem`)입니다. 핸들이 비면 층이 없고 셰이더는 정점을 옮기지 않는다(`g_SurfaceDeformParams.z` 가 0). */
        SurfaceDeformBinding _surfaceDeform;
        /** @brief 이 프레임의 전역 머티리얼 값(날씨 · 환경 시간)입니다(`SceneEnvironment::collectMaterialGlobals` → PassCB `g_MaterialGlobal*`). */
        MaterialGlobals _materialGlobals;
        /** @brief 이번 프레임의 추가 뷰(CCTV, 백미러, 분할 화면, PiP)입니다. 쉬는 뷰도 실립니다(`RenderViewRequest::_bRender`). */
        vector<RenderViewRequest> _listView;
        /**
         * @brief 화면 2D(UI, 월드 글자) 그리기 목록입니다. 렌더 스레드가 위젯을 볼 수 없으므로 이것만 넘어갑니다. 글리프 아틀라스 업로드는 차분이라 `_deltas` 에 있다.
         * @details 렌더러가 자기 것과 바꿔치기로 받습니다(`FrameRenderer::executePacket`). 저장소가 돌아 다음 프레임에 다시 쓰입니다.
         */
        CanvasFrameData _canvas;
        /** @brief 주 시점의 출력 사각형, 해상도 배율, 끌 기능, 컷 표시입니다. */
        RenderViewSettings _mainView;
        /** @brief 비지 않으면 이 패킷을 그린 뒤 화면에 나간 이미지를 이 경로에 PPM 으로 씁니다(자동화 시나리오의 `<Screenshot>`). */
        AbsolutePath _screenshotPath;
        /**
         * @brief 이번 프레임에 그릴 미리보기 씬 패킷입니다(앞 `_scenePreviewCount` 개만 유효). 패킷마다 씬 하나, 스냅샷 하나, 출력 타깃 하나입니다.
         * @details 저장소는 남깁니다. 링을 돌며 다시 채우므로 미리보기 패킷의 스냅샷과 라이트 목록이 프레임마다 새로 할당되지 않습니다.
         *          렌더 스레드는 씬 키(`_sceneKey`)마다 따로 둔 렌더러로 그립니다(`OffscreenSceneRenderers`). 미리보기 패킷 안의 이 목록은 항상 비어 있습니다.
         */
        vector<unique_ptr<RenderFramePacket>> _listScenePreview;
        uint32                                _scenePreviewCount;
        /** @brief 일회성 차분 · 알림입니다. 렌더 스레드가 받는 즉시 통째로 옮긴다(`RenderFramePacketDeltas`). 미리보기 패킷의 것은 늘 비어 있다. */
        RenderFramePacketDeltas _deltas;
        /** @brief 이 패킷이 그리는 씬의 키입니다. 0(`kInvalidSceneKey`)이면 활성 씬(주 패킷), 아니면 미리보기 씬입니다. */
        uint32           _sceneKey;
        RHITextureHandle _outputRenderTarget; ///< 주 출력의 오프스크린 RT(에디터 게임 뷰, 씬 뷰만 보일 때는 씬 뷰). 0 = 백버퍼 경로
        uint32           _viewportWidth;      ///< 주 출력의 픽셀 너비 — 백버퍼 경로도 스왑체인 크기로 풀어 싣는다(0 은 정말 빈 화면, 최소화)
        uint32           _viewportHeight;     ///< 주 출력의 픽셀 높이(너비와 같은 규칙)
        uint64           _frameIndex;
        /** @brief 이 패킷을 그리는가입니다. `SimulationOnly` 면 렌더 스레드가 상태(업로드)만 받고 기록 · Present · 사진을 하지 않는다(`SimulationOnlyFrameQueue`). */
        FrameDrawMode _drawMode;
        /** @brief 짓는 때까지 그리지 않은 프레임의 번호(`SimulationOnlyFrameQueue::getLastSkippedSerial`)입니다 — 그린 패킷이 끝나면 따라잡기를 이 번호까지 적는다. */
        uint64 _simulationOnlySerial;
        /** @brief `_viewProj` · `_cameraPos` 가 카메라에서 온 주 시점이면 1 입니다. 0 인 패킷은 그리지 않습니다(`FrameRenderer::executePacket` — 기본 카메라로 대신 그리지 않는다). `setMainView` 로만 켭니다. */
        uint8 _bHasViewProj : 1;
        uint8 _bValid       : 1;
        /** @brief 씬에 DirectionalLightComponent 가 있어 라이트 필드가 유효하면 1 입니다. */
        uint8 _bHasLight : 1;
        /** @brief 씬에 활성 `SkyComponent` 가 있어 `_sky` 가 유효하면 1 입니다. */
        uint8 _bHasSky : 1;
        /** @brief 씬에 안개(`FogComponent` 또는 물속)가 있어 `_fog` 가 유효하면 1 입니다. */
        uint8 _bHasFog : 1;
        /** @brief 자동 렌더 배율이 켜져 GPU 프레임 시간(`GPUFrameClock`)을 원하면 1 입니다. 렌더 스레드가 시계에 넘긴다(`FrameRenderer::executePacket`). */
        uint8                  _bWantsGPUFrameTime : 1;
        [[maybe_unused]] uint8 _reserved           : 2;

        RenderFramePacket()
            : _gpuScene{}
            , _settings{}
            , _clearColor{ 0.12f, 0.15f, 0.18f, 1.0f }
            , _cameraPos{}
            , _viewProj{}
            , _shadow{}
            , _lightDirIntensity{}
            , _lightColorAmbient{}
            , _listLight{}
            , _pLightProbe{}
            , _listLightCookie{}
            , _listRealtimeProbe{}
            , _listLensFlare{}
            , _listDecal{}
            , _sky{}
            , _skyLight{}
            , _fog{}
            , _surfaceDeform{}
            , _materialGlobals{}
            , _listView{}
            , _canvas{}
            , _mainView{}
            , _screenshotPath{}
            , _listScenePreview{}
            , _scenePreviewCount{ 0 }
            , _deltas{}
            , _sceneKey{ 0 }
            , _outputRenderTarget{ 0 }
            , _viewportWidth{ 0 }
            , _viewportHeight{ 0 }
            , _frameIndex{ 0 }
            , _drawMode{ FrameDrawMode::Draw }
            , _simulationOnlySerial{ 0 }
            , _bHasViewProj{ SW_FALSE }
            , _bValid{ SW_FALSE }
            , _bHasLight{ SW_FALSE }
            , _bHasSky{ SW_FALSE }
            , _bHasFog{ SW_FALSE }
            , _bWantsGPUFrameTime{ SW_FALSE }
            , _reserved{ 0 }
        {
        }

        /**
         * @brief 주 시점(카메라의 뷰-투영과 위치)을 한 번에 싣고 `_bHasViewProj` 를 켭니다.
         * @details 행렬 · 위치 · 플래그를 따로 쓰면 한쪽이 빠진 패킷이 생긴다(행렬 없이 플래그만, 위치는 기본값 그대로).
         */
        void setMainView( const float4x4& viewProj, const float3& cameraPos )
        {
            _viewProj     = viewProj;
            _cameraPos    = cameraPos;
            _bHasViewProj = SW_TRUE;
        }

        /**
         * @brief 새 프레임을 채우기 전에 값 필드를 기본값으로 되돌립니다. **저장소는 남깁니다.**
         * @details 패킷은 GT 의 스크래치 하나가 링 슬롯과 바꿔 가며 돕니다(`RenderThread::submit`). 그래서 이 객체에는 몇 프레임
         *          전의 값이 남아 있고, 조건부로만 쓰는 필드(빛, 뷰-투영)는 여기서 지워야 합니다. 벡터와 스냅샷은 비우기만 해
         *          용량이 남습니다.
         */
        void resetForFrame()
        {
            _clearColor = float4{ 0.12f, 0.15f, 0.18f, 1.0f };
            _cameraPos  = float3{};
            _viewProj   = float4x4{};
            // 장면 스냅샷(`_gpuScene`)은 여기서 비우지 않는다 — 채우는 쪽이 대입으로 통째로 덮어써(`GPUSceneBuilder::exportCPUSnapshot`) 그룹 · 원소의 저장소가 남는다.
            // 비우면 프레임마다 머티리얼 그룹을 새로 할당한다. 채우지 않는 프레임(활성 씬 없음)은 그쪽이 비운다(`RenderPacketBuilder::populateScene`).
            _shadow.clear();
            _lightDirIntensity = float4{};
            _lightColorAmbient = float4{};
            _listLight.clear();
            _pLightProbe.reset();
            _listLightCookie.clear();
            _listRealtimeProbe.clear();
            _listLensFlare.clear();
            _listDecal.clear();
            _sky             = SkyGradient{};
            _skyLight        = float4{};
            _fog             = FogSettings{};
            _surfaceDeform   = SurfaceDeformBinding{};
            _materialGlobals = MaterialGlobals{};
            _listView.clear();
            _canvas.clear();
            _mainView          = RenderViewSettings{};
            _screenshotPath    = {};
            _scenePreviewCount = 0;
            _deltas.clear();
            _sceneKey             = 0;
            _outputRenderTarget   = 0;
            _viewportWidth        = 0;
            _viewportHeight       = 0;
            _frameIndex           = 0;
            _drawMode             = FrameDrawMode::Draw;
            _simulationOnlySerial = 0;
            _bHasViewProj         = SW_FALSE;
            _bValid               = SW_FALSE;
            _bHasLight            = SW_FALSE;
            _bHasSky              = SW_FALSE;
            _bHasFog              = SW_FALSE;
            _bWantsGPUFrameTime   = SW_FALSE;
        }
    };
} // namespace sw
