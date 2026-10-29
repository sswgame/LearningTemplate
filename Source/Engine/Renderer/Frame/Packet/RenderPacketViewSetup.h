/**
 * @file Engine/Renderer/Frame/Packet/RenderPacketViewSetup.h
 * @brief 패킷의 시점을 정합니다(게임 스레드). 주 시점 카메라 고르기, 자동 렌더 배율, 보기 모드, 추가 뷰(CCTV · 화면 사각형 · 씬 뷰) 고르기입니다.
 * @details `RenderPacketBuilder` 가 값으로 소유합니다. 언리얼 `FSceneViewFamily` 를 만드는 자리(`FSceneView` 설정과 씬 캡처 뷰 일정)에 해당합니다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    struct HostViewTargets;
    struct RenderFramePacket;

    enum class RenderViewMode : uint8;

    class CameraComponent;
    class FrameRenderer;
    class RenderScaleAdjuster;
    class RenderViewScheduler;
    class RHI;
    class Scene;

    /**
     * @brief 이번 프레임의 뷰 카메라를 반환합니다.
     * @details tick 안의 핫 리로드와 씬 전환이 GameObject 를 파괴할 수 있으므로,
     *          카메라는 미리 잡아 두지 않고 파괴 단계가 끝난 뒤 이 델리게이트로 조회합니다.
     */
    SW_DECLARE_DELEGATE( CameraComponent*, ViewCameraProviderDelegate, void );

    /** @brief `RenderPacketViewSetup::setupMainView` 가 고른 이번 프레임의 시점입니다. 추가 뷰 고르기와 씬 내용 채우기가 읽습니다. */
    struct RenderPacketMainView
    {
        CameraComponent* _pCamera{ nullptr };          ///< 주 시점 카메라(없으면 주 시점 없는 프레임 — 렌더러가 그리지 않는다)
        CameraComponent* _pSceneViewCamera{ nullptr }; ///< 씬 뷰 카메라(씬 뷰가 없거나 꺼졌으면 nullptr)
        uint32           _outputWidth{ 0 };            ///< 주 출력 크기(게임 뷰 RT 면 그 크기, 백버퍼 경로면 스왑체인 크기)
        uint32           _outputHeight{ 0 };
        float32          _autoRenderScale{ 1.0f }; ///< 주 시점 · 화면 사각형 뷰에 곱할 자동 렌더 배율
        RenderViewMode   _sceneViewMode{};         ///< 씬 뷰 툴바의 보기 모드
    };

    /**
     * @class RenderPacketViewSetup
     * @brief 패킷의 주 시점과 추가 뷰를 정하는 게임 스레드 쪽 상태(추가 뷰 스케줄러 · 그 시계 · 자동 렌더 배율 조정기)입니다.
     */
    class SW_API RenderPacketViewSetup
    {
    public:
        RenderPacketViewSetup();
        ~RenderPacketViewSetup();

        RenderPacketViewSetup( const RenderPacketViewSetup& )            = delete;
        RenderPacketViewSetup& operator=( const RenderPacketViewSetup& ) = delete;

        /** @brief 추가 뷰 스케줄러가 없으면 만듭니다(백엔드 교체로 다시 시작할 때는 있는 것을 그대로 씁니다). */
        void initialize();
        /** @brief 추가 뷰 스케줄러를 없앱니다. */
        void destroy();

        /**
         * @brief 주 시점 카메라를 고르고(씬 뷰가 주 출력이면 씬 뷰 카메라, 아니면 게임 카메라) 주 시점 설정 · 뷰-투영 · 보기 모드를 @p packet 에 싣습니다.
         * @details 쓸 카메라가 없으면 한 번 오류로 알립니다(기본 카메라로 대신 그리지 않는다). 자동 렌더 배율은 카메라와 상관없이 프레임마다 한 번 갱신합니다.
         * @param sceneViewCameraProvider 씬 뷰 카메라 조회입니다. 씬 뷰가 있을 때만, 씬 틱과 기본 카메라 보장 **뒤에** 부릅니다.
         * @param rhi 주 출력 크기가 0 일 때(백버퍼 경로) 스왑체인 크기를 읽습니다.
         */
        RenderPacketMainView setupMainView( RenderFramePacket& packet, const HostViewTargets& views, const ViewCameraProviderDelegate& sceneViewCameraProvider, Scene& activeScene,
                                            float32 deltaTime, const RHI& rhi, const FrameRenderer* pFrameRenderer );
        /**
         * @brief 추가 뷰(캡처 카메라 · 화면 사각형) 중 이번 프레임에 그릴 것을 갱신 주기 · 보이는가 · 예산으로 고르고(쉬는 뷰도 실린다), 씬 뷰를 주 출력 여부에 맞춰 넣거나 뺍니다.
         * @details 주 시점 그림자가 정해진 **뒤에** 부릅니다(추가 뷰의 그림자는 그 뒤 `RenderPacketSceneCollector::populateViewShadows`).
         */
        void collectExtraViews( RenderFramePacket& packet, const HostViewTargets& views, Scene& activeScene, const RenderPacketMainView& mainView, float32 deltaTime,
                                const FrameRenderer* pFrameRenderer );

    private:
        /** @brief 자동 렌더 배율(`gv_autoRenderScale`)에 이번 프레임 시간(초)을 넣고 주 시점 배율에 곱할 값을 돌려줍니다. 꺼져 있으면 1 입니다. */
        float32 updateAutoRenderScale( float32 deltaTime );

        /** @brief 추가 뷰(CCTV, 백미러, PiP) 중 이번 프레임에 그릴 것을 고르는 스케줄러(갱신 주기, 예산)입니다. */
        unique_ptr<RenderViewScheduler> _renderViewScheduler;
        /** @brief 스케줄러의 시각(초)입니다. 프레임 델타의 누적입니다. */
        float64 _renderViewClock;
        /** @brief 주 시점 자동 렌더 배율 조정기입니다(`gv_autoRenderScale`). 처음 켤 때 만듭니다. */
        unique_ptr<RenderScaleAdjuster> _renderScaleAdjuster;
        bool                            _bAutoRenderScaleActive;
        /** @brief 활성 씬에 쓸 카메라가 없다는 오류를 이미 알렸는지입니다. 카메라가 생기면 다시 알릴 수 있게 내립니다. */
        bool _bMissingCameraReported;
    };
} // namespace sw
