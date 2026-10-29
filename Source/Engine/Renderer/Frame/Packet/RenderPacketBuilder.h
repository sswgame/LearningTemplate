/**
 * @file Engine/Renderer/Frame/Packet/RenderPacketBuilder.h
 * @brief 게임 스레드가 프레임마다 렌더 스레드에 넘길 패킷(`RenderFramePacket`)을 채웁니다. `Engine` 의 값 멤버입니다.
 * @details 씬 스냅샷 빌더, 패킷 저장소, GPU 업로드 큐, 추가 뷰 스케줄러가 여기 있고, 기동 단계 `FrameRenderer` 가 생성하고 종료합니다.
 *          렌더 스레드는 씬을 보지 못하므로(패킷이 유일한 통로) 씬에서 읽을 것은 모두 여기서 패킷에 담습니다.
 *          부르는 순서(패킷 열기 → 관찰자 → 씬과 뷰 → 캔버스 → 제출)는 `Engine::update` 의 호출 코드에서 볼 수 있습니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/Scene/AnimationLOD.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"

namespace sw
{
    struct HostViewTargets;
    struct RenderFramePacket;

    class CameraComponent;
    class FontService;
    class FrameRenderer;
    class GPUSceneBuilder;
    class GPUUploadQueue;
    class IRHIDevice;
    class RenderScaleAdjuster;
    class RenderViewScheduler;
    class RHI;
    class Scene;
    class SceneManager;
    class TaskManager;
    class UISystem;

    /**
     * @brief 이번 프레임의 뷰 카메라를 반환합니다.
     * @details tick 안의 핫 리로드와 씬 전환이 GameObject 를 파괴할 수 있으므로,
     *          카메라는 미리 잡아 두지 않고 파괴 단계가 끝난 뒤 이 델리게이트로 조회합니다.
     */
    SW_DECLARE_DELEGATE( CameraComponent*, ViewCameraProviderDelegate, void );

    /**
     * @class RenderPacketBuilder
     * @brief 프레임 패킷을 채우는 게임 스레드 쪽 상태(씬 스냅샷 빌더, 패킷, 업로드 큐, 뷰 스케줄러)와 그 채우기입니다.
     * @details 객체들은 힙에 둡니다. 값으로 들면 이 헤더가 Graphics 의 씬 스냅샷 헤더들을 App 까지 끌고 갑니다(전방 선언으로 끊습니다).
     *          헤드리스 작업에는 기동 단계가 서지 않아 아무것도 없습니다(`isInitialized` 가 false).
     */
    class SW_API RenderPacketBuilder
    {
    public:
        RenderPacketBuilder();
        ~RenderPacketBuilder();

        RenderPacketBuilder( const RenderPacketBuilder& )            = delete;
        RenderPacketBuilder& operator=( const RenderPacketBuilder& ) = delete;

        /**
         * @brief 없는 객체를 만들고, 디바이스에 매인 설정(배치 합치기, 업로드 큐의 디바이스)을 @p device 로 설정합니다.
         * @details 백엔드 교체로 다시 시작할 때는 있는 객체를 그대로 쓰고 디바이스에 매인 설정만 새로 설정합니다.
         */
        void initialize( IRHIDevice& device, TaskManager* pTaskManager );
        /** @brief 씬 스냅샷 빌더가 보관한 머티리얼과 인스턴스를 놓습니다. **디바이스가 살아 있을 때** 부릅니다(렌더러 종료와 같은 시점). */
        void shutdown();
        /** @brief 객체를 모두 없앱니다. 기동이 어디서 멈췄든 불리므로 null 안전합니다. */
        void destroy();

        /** @brief 패킷을 낼 수 있는지(기동 단계 `FrameRenderer` 가 섰는지) 반환합니다. */
        bool isInitialized() const;
        /** @brief 씬 스냅샷 빌더가 머티리얼이 달라도 배치를 합치는지 반환합니다(없으면 false). 백엔드 교체 뒤 로그가 읽습니다. */
        bool isMergeBatchesAcrossMaterials() const;

        /** @brief 링에서 돌아온 저장소를 이번 프레임용으로 비우고 유효로 표시해 돌려줍니다. `isInitialized` 일 때만 부릅니다. */
        RenderFramePacket& beginPacket();
        /**
         * @brief 주 출력, 주 광원, 라이트, 카메라, 그림자, 추가 뷰, 애니메이션 LOD 뷰를 싣고 씬 스냅샷을 내보냅니다(GPU 업로드 요청 포함).
         * @param pActiveScene 활성 씬입니다. null 이면 주 출력만 싣습니다.
         * @param sceneViewCameraProvider 씬 뷰 카메라 조회입니다. 씬 뷰가 있을 때만, 씬 틱과 기본 카메라 보장 **뒤에** 부릅니다.
         * @param rhi 주 출력 크기가 0 일 때(백버퍼 경로) 스왑체인 크기를 읽습니다.
         */
        void populateScene( RenderFramePacket& packet, const HostViewTargets& views, const ViewCameraProviderDelegate& sceneViewCameraProvider, Scene* pActiveScene,
                            float32 deltaTime, const RHI& rhi, const FrameRenderer* pFrameRenderer );
        /** @brief 애니메이션 LOD · 메시 LOD 가 다음 프레임에 볼 뷰(주 시점과 이번 프레임에 그리는 추가 뷰)를 씬의 애니메이션 시스템과 메시 LOD 고르기에 넘깁니다. */
        void updateLODViews( const RenderFramePacket& packet, Scene& activeScene );
        /** @brief GPUScene 을 씬에서 채우고 추가 뷰의 투명 순서 · GPU 업로드 요청을 만든 뒤 CPU 스냅샷을 패킷에 내보냅니다. */
        void populateGPUSceneSnapshot( RenderFramePacket& packet, Scene* pActiveScene );
        /**
         * @brief 화면 2D(런타임 UI 의 캔버스, 월드 UI 캔버스, 개발 테스트 이미지), 글리프 아틀라스 구간, 색각 보정을 싣습니다.
         * @param bSceneMain 씬 뷰가 주 출력인 프레임이면 화면 UI 를 싣지 않습니다. UI 는 게임 화면(게임 뷰, 백버퍼)에만 있습니다.
         */
        static void populateCanvas( RenderFramePacket& packet, bool bSceneMain, UISystem* pUISystem, FontService* pFontService, const RHI* pRHI );
        /**
         * @brief 패킷의 GPU 투영 마커(`CanvasFrameData::_listMarker`) 중 활성 씬의 메시에 붙은 것에 GPUScene 인스턴스 칸을 풉니다(`populateScene` · `populateCanvas` 뒤).
         * @details 마커가 속한 관리자가 활성 씬의 것이 아니거나 칸을 못 찾으면 기록은 월드 점을 쓴다(`_instanceSlot` 이 비어 있다).
         */
        void resolveMarkerInstanceSlots( RenderFramePacket& packet, const UISystem* pUISystem, const Scene* pActiveScene ) const;
        /**
         * @brief 호스트가 요청한 미리보기 씬 뷰(`HostViewTargets::_arrSceneView`)마다 미리보기 패킷을 하나씩 채워 @p packet 에 붙입니다.
         * @details 씬마다 스냅샷 빌더를 따로 둔다(빌더의 재구축 판단 캐시가 씬별이기 때문이다). 요청이 끊긴 씬의 빌더는 이 프레임에 해제한다.
         *          미리보기 패킷에는 주 광원, 라이트, 그림자, 카메라, 씬 스냅샷만 싣는다. 추가 뷰, 캔버스, 스크린샷은 주 패킷에만 있다.
         * @param sceneManager 씬 키로 씬을 찾는 곳입니다(`SceneManager::findPreviewScene`). 없는 키는 건너뜁니다.
         */
        void populateScenePreviews( RenderFramePacket& packet, const HostViewTargets& views, const SceneManager& sceneManager );

    private:
        /** @brief 자동 렌더 배율(`gv_autoRenderScale`)에 이번 프레임 시간(초)을 넣고 주 시점 배율에 곱할 값을 돌려줍니다. 꺼져 있으면 1 입니다. */
        float32 updateAutoRenderScale( float32 deltaTime );

        /** @brief 미리보기 씬 하나의 GT 쪽 스냅샷 빌더입니다. 씬 키로 찾습니다. */
        struct ScenePreviewBuilder
        {
            unique_ptr<GPUSceneBuilder> _builder;
            uint32                      _sceneKey{ 0 };
            uint8                       _bSeen{ SW_FALSE }; ///< 이번 프레임에 요청이 있었다
        };
        /** @brief 씬 키의 빌더 항목을 찾고, 없으면 만듭니다. */
        ScenePreviewBuilder& getOrCreateScenePreviewBuilder( uint32 sceneKey );

        /** @brief GT 쪽 씬 스냅샷 빌더입니다. populateFromScene 의 재구축 판단 캐시가 프레임을 넘어 유지되도록 여기서 소유합니다.
         *         프레임마다 CPU 스냅샷만 exportCPUSnapshot 으로 뽑아 패킷에 담아 RT 로 넘깁니다. */
        unique_ptr<GPUSceneBuilder> _gpuSceneBuilder;
        /**
         * @brief GT 가 프레임마다 채우는 패킷입니다. 링의 슬롯과 바꿔 가며 돕니다(`RenderThread::submit`).
         * @details 링에서 돌아온 저장소를 그대로 다시 채웁니다(지역 변수로 두면 스냅샷의 배치와 그룹 목록, 라이트 목록이
         *          프레임마다 새로 할당됩니다).
         */
        unique_ptr<RenderFramePacket> _packetScratch;
        unique_ptr<GPUUploadQueue>    _gpuUploadQueue;
        /** @brief 추가 뷰(CCTV, 백미러, PiP) 중 이번 프레임에 그릴 것을 고르는 스케줄러(갱신 주기, 예산)입니다. */
        unique_ptr<RenderViewScheduler> _renderViewScheduler;
        /** @brief 스케줄러의 시각(초)입니다. 프레임 델타의 누적입니다. */
        float64 _renderViewClock;
        /** @brief 주 시점 자동 렌더 배율 조정기입니다(`gv_autoRenderScale`). 처음 켤 때 만듭니다. */
        unique_ptr<RenderScaleAdjuster> _renderScaleAdjuster;
        bool                            _bAutoRenderScaleActive;
        /** @brief 애니메이션 LOD · 메시 LOD 에 넘기는 이번 프레임의 뷰(주 시점 + 그리는 추가 뷰)입니다. 프레임마다 재사용합니다. */
        vector<LODView> _listLODView;
        /** @brief 그림자를 켠 점광과 스폿의 라이트 원소 번호입니다(`collectSceneLights` → `ShadowSetupUtil::populateFrame`). 프레임마다 재사용합니다. */
        vector<uint32> _listShadowLightIndex;
        /** @brief 미리보기 씬마다의 스냅샷 빌더입니다(`populateScenePreviews`). 요청이 끊긴 씬의 것은 해제합니다. */
        vector<ScenePreviewBuilder> _listScenePreviewBuilder;
        /** @brief 미리보기 씬 하나의 애니메이션 LOD · 메시 LOD 뷰입니다. 프레임마다 재사용합니다. */
        vector<LODView> _listPreviewLODView;
        /** @brief 디버그 드로우 겹치기가 펼친 선분입니다(`DebugDrawCanvasUtil::paint`). 게임 스레드 하나가 패킷을 채우므로 하나를 프레임마다 재사용합니다. */
        vector<DebugLine> _listDebugDrawLine;
        /** @brief 디바이스가 텍스처를 인덱스로 고를 수 있어 머티리얼이 달라도 배치를 합치는지입니다(`initialize` 가 정한다). 미리보기 빌더에도 같은 값을 준다. */
        bool _bMergeBatchesAcrossMaterials;
        /** @brief 디바이스가 GPU 컬링 컴퓨트를 돌릴 수 있어 인스턴스 LOD 를 GPU 가 고를 수 있는지입니다(`initialize` 가 정한다). 미리보기 빌더는 쓰지 않는다. */
        bool _bGPUInstanceLODCapable;
        /** @brief 활성 씬에 쓸 카메라가 없다는 오류를 이미 알렸는지입니다. 카메라가 생기면 다시 알릴 수 있게 내립니다. */
        bool _bMissingCameraReported;
    };
} // namespace sw
