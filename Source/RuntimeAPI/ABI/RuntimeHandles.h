/**
 * @file RuntimeHandles.h
 * @brief App ↔ Editor/Game Runtime API용 공유 불투명 핸들
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 불투명 핸들 — C ABI 테이블이 넘기는 값
    //    실제 타입은 넘기는 쪽이 소유한다. 받는 쪽은 역참조하지 않고 되돌려줄 뿐이다.
    //    테이블마다 흩어 두면 "경계를 넘는 값이 무엇인가" 에 답하는 자리가 없어진다.
    // ------------------------------------------------------------------------------
    /** @brief 윈도우 인스턴스를 가리키는 불투명(opaque) 핸들. 호스트가 소유. */
    using WindowHandle = void*;
    /** @brief RHI 디바이스를 가리키는 불투명(opaque) 핸들. 호스트가 소유. */
    using RHIDeviceHandle = void*;
    /** @brief 에디터 인스턴스를 가리키는 불투명(opaque) 핸들. EditorModule 이 소유. */
    using EditorHandle = void*;
    /** @brief 게임 인스턴스를 가리키는 불투명(opaque) 핸들. SWGame 이 소유. */
    using GameHandle = void*;
    /** @brief RHI 텍스처를 가리키는 핸들. 값 자체가 식별자이므로 포인터가 아니다. */
    using TextureHandle = uint64;
} // namespace sw
