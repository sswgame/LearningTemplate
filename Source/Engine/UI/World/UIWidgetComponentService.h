/**
 * @file UIWidgetComponentService.h
 * @brief 위젯 컴포넌트(`WidgetComponent`)의 UI 쪽 서비스입니다 — 등록 목록, 화면 마커(HUD 마커 화면 · 앵커 표), World 위젯의 광선 입력과 렌더 텍스처 목록(엔진 범위 서브시스템 `UIWidgetComponents`).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Foundation/EngineSubsystemHost.h"
#include "Engine/Graphics/Canvas/CanvasDrawList.h"
#include "Engine/UI/Screen/UIScreenMarkers.h"
#include "Engine/UI/WidgetTree/UIEvents.h"
#include "Engine/UI/WidgetTree/WidgetTypes.h"
#include "Engine/UI/World/UIWorldPointerRouter.h"
#include "Engine/UI/World/WorldWidgetPick.h"

namespace sw
{
    class UISystem;
    class Widget;
    class WidgetComponent;

    /**
     * @class UIWidgetComponentService
     * @brief 씬의 위젯 컴포넌트가 화면 스택 위에 띄우는 것(화면 마커)과 화면 스택 밖에 그리는 것(World 렌더 텍스처)을 맡습니다
     *        (엔진 범위 서브시스템 `UIWidgetComponents`, 컴포넌트는 씬이 받은 엔진 범위에서 찾는다).
     * @details 화면 스택(`UISystem`)은 화면을 쌓고 그리는 데까지이고, 월드를 따라가는 위젯은 그 위의 서비스다(언리얼 `UWidgetComponent` 의 Screen 공간이
     *          `UGameViewportSubsystem` 위의 위젯 레이어인 것과 같다). 그래서 화면 스택의 값 멤버가 아니라 화면 스택에 의존하는 서브시스템이고,
     *          UI 갱신의 칸 셋에 건다 — 포인터 처리 뒤(`onPointerProcessed`, 마우스 광선을 World 위젯으로), 레이아웃 앞(`onUpdateBeforeLayout`, 마커 투영),
     *          그리기 뒤(`onUpdateAfterPaint`, 앵커 표를 물리 픽셀로 내고 World 트리를 칠한다).
     *          마커 화면(HUD 레이어, 캔버스 패널 루트, 클릭을 막지 않음)은 처음 붙일 때 화면 스택에 올리고 마지막 위젯을 뗄 때 닫는다. 게임 스레드에서만 쓴다.
     */
    class SW_API UIWidgetComponentService final : public ISubsystem
    {
    public:
        UIWidgetComponentService();
        ~UIWidgetComponentService() override;

        // ISubsystem — 화면 스택에 의존한다. 종료는 등록한 컴포넌트가 이 서비스를 잊게 하고(컴포넌트가 더 오래 남을 수 있다) 마커 화면을 닫는다.
        /** @brief 서브시스템 이름입니다. */
        static constexpr const utf8* kSubsystemName = "UIWidgetComponents";
        SubsystemInitResult          initializeSubsystem() override;
        void                         shutdownSubsystem() override;
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const>      getSubsystemDependencies() const override;
        SubsystemHostMask            getSubsystemHost() const override { return EngineSubsystemHost::kClient; }
        /** @brief 기동했고 아직 내려가지 않았으면 true 입니다(컴포넌트가 붙기 전에 본다). */
        bool isActive() const { return _pUI != nullptr; }

        // --- 등록 ------------------------------------------------------------------
        /** @brief 위젯 컴포넌트를 등록합니다. 컴포넌트가 시작할 때 부르며, UI 갱신이 레이아웃 앞에서 화면 마커 위치를 갱신합니다. 이미 있으면 아무것도 하지 않습니다. */
        void   registerComponent( WidgetComponent& component );
        void   unregisterComponent( WidgetComponent& component );
        uint32 getComponentCount() const { return static_cast<uint32>( _listComponent.size() ); }

        // --- 화면 마커(WidgetComponent Screen) -------------------------------------------
        /**
         * @brief 마커 화면에 위젯을 붙이고 그 번호를 반환합니다. 마커 화면은 처음 붙일 때 만들고, 마지막 위젯을 뗄 때 닫습니다.
         * @details 위젯마다 화면 앵커 하나를 주고(`Widget::getPaintAnchor`) 위젯은 앵커 점을 원점으로 배치한다. 화면 위치와 배율은 붙인 쪽이 매 프레임
         *          앵커로 정한다(`getMarkers().setAnchor`, `WidgetComponent::applyPlacement`). 마커가 움직여도 배치 · 칠하기 · 사각형 업로드가 없다.
         */
        WidgetID addScreenMarker( unique_ptr<Widget> widget );
        /** @brief 마커 위젯을 떼어 지웁니다(그 앵커 번호는 다음 마커가 다시 쓴다). */
        void removeScreenMarker( WidgetID widget );
        /** @brief 마커 위젯을 반환합니다(없으면 nullptr). */
        Widget* findScreenMarker( WidgetID widget ) const;
        /**
         * @brief 마커의 앵커 표와 GPU 투영 기록입니다. 붙인 쪽이 앵커 값(`setAnchor`) · GPU 기록(`setGPUMarker` · `clearGPUMarker`)을 쓰고,
         *        렌더 패킷이 낸 앵커(`getCanvasAnchors`)와 GPU 입력(`getGPUMarkers` · `getGPUMarkerView`)을 싣습니다.
         * @details 마커 화면 핸들과 앵커 번호 발급은 이 서비스의 일이다(`addScreenMarker` · `removeScreenMarker`) — 표를 받는 쪽은 그 둘을 부르지 않는다.
         */
        UIScreenMarkers&       getMarkers() { return _markers; }
        const UIScreenMarkers& getMarkers() const { return _markers; }

        // --- World 위젯 --------------------------------------------------------------
        /**
         * @brief 광선 @p ray 가 맞힌 가장 가까운 World 위젯(입력을 켠 `WidgetComponent`)에 포인터 이벤트 @p event 를 보냅니다(위치 · 변화량은 위젯 좌표로 채워진다).
         * @details 마우스는 UI 입력 처리의 포인터 뒤 칸이 스스로 한다. 마우스를 잠근 1인칭의 크로스헤어나 패드 같은 다른 출처는 게임 코드가 자기 광선과 `Down` · `Up` · `Move` · `Wheel` 로 부른다.
         * @return 위젯이 처리했으면 true 입니다.
         */
        bool routeWorldPointer( const WorldWidgetRay& ray, const UIPointerEvent& event );
        /** @brief World 위젯 컴포넌트의 렌더 텍스처 목록을 덧붙입니다. 렌더 패킷이 캔버스의 대상 목록에 넣으며, 렌더러가 장면 앞에서 그립니다. */
        void appendWorldCanvases( vector<CanvasTargetDrawList>& inoutListTarget ) const;

    private:
        /** @brief 포인터 처리 뒤 칸입니다 — 이번 프레임의 마우스 이벤트 중 화면 위젯 아래가 아닌 것을 게임 카메라 광선으로 바꿔 World 위젯으로 보냅니다. */
        void processWorldPointer( float32 deltaSeconds );
        /** @brief 레이아웃 앞 칸입니다 — 게임 틱과 트랜스폼 적용 뒤의 월드 점을 이번 뷰포트로 투영해 마커를 옮깁니다(같은 프레임에 배치된다). */
        void updateScreenMarkers( float32 deltaSeconds );
        /** @brief 그리기 뒤 칸입니다 — 앵커 표를 이번 배율의 물리 픽셀로 내고, World 컴포넌트마다 자기 트리를 렌더 텍스처 크기(배율 1)로 배치하고 칠합니다. */
        void updateAfterPaint( float32 deltaSeconds );

    private:
        UISystem*                _pUI;                      ///< 화면 스택(기동이 찾는다, 그 전 · 종료 뒤에는 nullptr)
        vector<WidgetComponent*> _listComponent;            ///< 등록된 위젯 컴포넌트입니다(소유하지 않으며, 끝날 때 스스로 뺍니다).
        UIScreenMarkers          _markers;                  ///< 화면 마커 화면과 앵커 표입니다.
        UIWorldPointerRouter     _worldPointer;             ///< World 위젯의 광선 입력 상태입니다.
        Subscription             _pointerSubscription;      ///< 포인터 처리 뒤 칸(`UISystem::onPointerProcessed`) 구독
        Subscription             _beforeLayoutSubscription; ///< 레이아웃 앞 칸(`UISystem::onUpdateBeforeLayout`) 구독
        Subscription             _afterPaintSubscription;   ///< 그리기 뒤 칸(`UISystem::onUpdateAfterPaint`) 구독
    };
} // namespace sw
