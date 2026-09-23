/**
 * @file PresentHookDelegate.h
 * @brief 프레젠트 앞뒤에 끼는 훅의 델리게이트 형 — 형만 필요한 곳(`EngineLoop.h`)이 패킷 헤더 없이 이것만 포함한다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"

namespace sw
{
    struct RenderFramePacket;

    class IRHIDevice;

    /** @brief 렌더 스레드가 프레젠트 직전·직후에 부른다 — 에디터 오버레이가 여기에 낀다. */
    SW_DECLARE_DELEGATE( void, PresentHookDelegate, IRHIDevice&, RenderFramePacket& );
} // namespace sw
