/**
 * @file PresentHookDelegate.h
 * @brief 렌더 스레드의 프레임 시작 · 프레젠트 앞뒤에 끼는 훅의 델리게이트 타입입니다. 타입만 필요한 곳(`Engine.h`)이 패킷 헤더 없이 이것만 포함합니다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"

namespace sw
{
    struct RenderFramePacket;

    class IRHIDevice;

    /** @brief 렌더 스레드가 프레젠트 직전과 직후에 부릅니다. 에디터 오버레이가 여기에 연결됩니다. */
    SW_DECLARE_DELEGATE( void, PresentHookDelegate, IRHIDevice&, RenderFramePacket& );
    /** @brief 렌더 스레드가 프레임의 `beginFrame`(펜스 대기 자리) 바로 앞에서 부릅니다. 장치 손실 흉내 같은 개발 도구가 여기에 연결됩니다. */
    SW_DECLARE_DELEGATE( void, BeginFrameHookDelegate, IRHIDevice& );
} // namespace sw
