/**
 * @file ModuleHandles.h
 * @brief App ↔ Editor/Game 모듈 API 가 함께 쓰는 불투명 핸들입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 불투명 핸들 — C ABI 테이블이 넘기는 값
    //    실제 타입은 넘기는 쪽이 소유한다. 받는 쪽은 역참조하지 않고 되돌려줄 뿐이다.
    //    테이블마다 흩어 두면 "경계를 넘는 값이 무엇인가" 에 답하는 자리가 없어진다.
    // ------------------------------------------------------------------------------
    /** @brief 윈도우 인스턴스를 가리키는 불투명(opaque) 핸들입니다. 호스트가 소유합니다. */
    using WindowHandle = void*;
    /** @brief RHI 디바이스를 가리키는 불투명(opaque) 핸들입니다. 호스트가 소유합니다. */
    using RHIDeviceHandle = void*;
    /** @brief 에디터 인스턴스를 가리키는 불투명(opaque) 핸들입니다. EditorModule 이 소유합니다. */
    using EditorHandle = void*;
    /** @brief 게임 인스턴스를 가리키는 불투명(opaque) 핸들입니다. SWGame 이 소유합니다. */
    using GameHandle = void*;
} // namespace sw
