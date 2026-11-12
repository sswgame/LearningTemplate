/**
 * @file Engine/Presentation/IUILayerVisibility.h
 * @brief 게임 창 위 UI 레이어를 숨겨 달라고 거는 창구입니다 — 포토 모드처럼 "UI 없이 보기" 를 거는 게임 코드가 UI 시스템을 모르고 겁니다.
 * @details 구현은 `UISystem` 이고, 없으면(전용 서버 · UI 를 넣지 않은 하네스) 칸이 비어 가릴 화면도 없다. 언리얼 `AHUD::bShowHUD` · Godot `CanvasLayer.visible` 자리입니다(기억).
 *          서비스 칸의 정의는 이 폴더에 있어 UI 폴더를 짓지 않는 구성에서도 칸을 읽는 쪽이 링크됩니다(`IInputCaptureQuery` · `IPlayerNoticeSink` 와 같은 모양).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Subsystem/ServiceSlot.h"

#include "Engine/Presentation/UILayer.h"

namespace sw
{
    /**
     * @class IUILayerVisibility
     * @brief 레이어 숨김 요청을 받는 쪽입니다. 게임 스레드에서 부릅니다.
     */
    class SW_API IUILayerVisibility
    {
    public:
        virtual ~IUILayerVisibility() = default;

        /** @brief 레이어 집합 @p layers 를 숨기는 요청을 하나 겁니다. 요청은 레이어마다 세어 쌓입니다. 건 쪽이 같은 집합으로 `removeLayerHideRequest` 를 꼭 부릅니다. */
        virtual void addLayerHideRequest( UILayerMask layers ) = 0;
        /** @brief `addLayerHideRequest` 로 건 요청을 하나 풉니다. */
        virtual void removeLayerHideRequest( UILayerMask layers ) = 0;

        SW_SERVICE_SLOT( IUILayerVisibility, Optional );
    };
} // namespace sw
