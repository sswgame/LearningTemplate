/**
 * @file RuntimeHandles.h
 * @brief App ↔ Editor/Game Runtime API용 공유 불투명 핸들
 */
#pragma once

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) 불투명 핸들 — C ABI 테이블이 넘기는 void*
    //    실제 타입은 App이 소유. Editor/Game 모듈은 역참조하지 않음
    // ------------------------------------------------------------------------------
    /** @brief 윈도우 인스턴스를 가리키는 불투명(opaque) 핸들 */
    using WindowHandle = void*;
    /** @brief RHI 디바이스를 가리키는 불투명(opaque) 핸들 */
    using RHIDeviceHandle = void*;
} // namespace sw
