/**
 * @file UIWorldPointerRouter.h
 * @brief 광선을 World 위젯(입력을 켠 `WidgetComponent`)으로 보내 포인터 이벤트로 바꿉니다(언리얼 `WidgetInteractionComponent` 에 해당).
 * @details 마우스는 `UISystem::processInput` 이 화면 위젯 아래가 아닌 이벤트를 카메라 광선으로 바꿔 여기에 넣고, 크로스헤어 · 패드 같은 다른 출처는 게임 코드가
 *          `UISystem::routeWorldPointer` 로 자기 광선을 넣는다. 가장 가까이 맞힌 위젯 하나가 이벤트를 받고, 위젯마다 호버 · 포인터 잡기 상태(`UIPointerState`)를 가지므로
 *          광선이 옮겨 가면 Leave · Enter 가 맞게 간다. 위젯 안의 위젯이 포인터를 잡았으면(슬라이더 끌기) 광선이 사각형 밖으로 벗어나도 그 위젯이 평면 위의 점을 받는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/UI/WidgetTree/UIEvents.h"
#include "Engine/UI/World/WorldWidgetPick.h"

namespace sw
{
    class WidgetComponent;

    /**
     * @class UIWorldPointerRouter
     * @brief 광선 하나를 World 위젯 하나로 고르고 위젯 좌표 이벤트로 보내는 상태입니다(`UISystem` 의 값 멤버). 컴포넌트는 소유하지 않는다.
     */
    class SW_API UIWorldPointerRouter
    {
    public:
        UIWorldPointerRouter();

        /**
         * @brief @p ray 가 맞힌 가장 가까운 World 위젯에 @p event(위치 · 변화량은 이 함수가 위젯 좌표로 바꿔 채운다)를 보냅니다.
         * @param listComponent 후보입니다(입력을 받지 않는 것은 건너뛴다).
         * @return 위젯이 이벤트를 처리했으면 true 입니다(부르는 쪽이 그 마우스 버튼을 소비한다).
         */
        bool route( const vector<WidgetComponent*>& listComponent, const WorldWidgetRay& ray, UIPointerEvent event );
        /** @brief 호버를 모두 풉니다(포인터가 화면 위젯 위로 올라갔거나 잠겼다). 잡은 위젯은 유지한다. */
        void releaseHover();
        /** @brief @p component 를 더 쓰지 않습니다(컴포넌트가 끝나거나 UI 시스템이 내려간다). 호버와 잡기를 알림 없이 버린다. */
        void forgetComponent( const WidgetComponent& component );
        /** @brief 지금 호버 중인 위젯입니다(없으면 nullptr). */
        const WidgetComponent* getHoveredComponent() const { return _pHovered; }

    private:
        WidgetComponent* _pHovered;  ///< 마지막 광선이 맞힌 위젯입니다.
        WidgetComponent* _pCaptured; ///< 포인터를 잡은 위젯이 든 컴포넌트입니다.
        float2           _lastPoint; ///< `_pHovered` 안의 마지막 위젯 좌표입니다(변화량 계산).
    };
} // namespace sw
