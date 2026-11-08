/**
 * @file IProfilerBackend.h
 * @brief 외부 프로파일러(타임라인 뷰어)로 계측을 내보내는 출력 하나의 계약입니다.
 *
 * [왜 따로 두는가]
 * 엔진 `FrameProfiler` 는 프로세스 **안에서** 구간을 접어 p50 · p99 표를 냅니다(성능 회귀 · 에디터 패널 · Shipping 오버레이).
 * 타임라인 · 스레드 · GPU 큐 · 메모리를 시간축으로 보는 일은 외부 뷰어(Tracy)가 합니다. 계측 지점(`SW_PROFILE_SCOPE`)은 하나이고,
 * 이 인터페이스가 두 번째 출력입니다. 라이브러리 헤더는 구현 폴더(`DevTools/Profiling/Tracy/`) 밖으로 나오지 않습니다 — 이 파일은
 * 라이브러리 타입을 하나도 쓰지 않으므로 Tracy 를 다른 뷰어로 바꿔도 호출부는 그대로입니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

#include "Engine/Profiling/IProfilerGPUTimeline.h"

/*
 * 외부 프로파일러의 메모리 쪽 계약입니다. 할당 · 해제를 메모리 태그 이름별로 냅니다.
 * 부르는 곳은 Core 할당 관찰(`ProfilerBackend` 의 `MemoryAllocationObserver`) 하나다. 할당 경로에서 불리므로 받는 쪽은 sw 할당자로 할당하지 않는다.
 */
namespace sw
{
    /** @class IProfilerMemoryOutput @brief 파일 머리말 참고. */
    class IProfilerMemoryOutput
    {
    public:
        virtual ~IProfilerMemoryOutput() = default;

        /** @brief 할당 한 건을 알립니다. @p pPoolName 은 프로세스 수명 문자열(메모리 태그 이름)입니다. */
        virtual void onAllocate( const void* pPtr, size_t size, const utf8* pPoolName ) = 0;
        /** @brief `onAllocate` 로 알린 블록의 해제를 알립니다. */
        virtual void onFree( const void* pPtr, const utf8* pPoolName ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class IProfilerBackend
     * @brief 외부 프로파일러 출력 하나입니다. CPU 구간 · 프레임 · 그래프는 여기에 있고, 메모리(`IProfilerMemoryOutput`) · GPU 타임라인(`IProfilerGPUTimeline`)은 역할 인터페이스로 상속합니다.
     * @details 여기 함수는 어느 스레드에서나 부를 수 있습니다(GPU 함수는 렌더 스레드 하나). 구간은 RAII(`ScopedFrameProfile`)가 열고 닫으므로 같은 스레드에서 짝이 맞습니다.
     *          `beginZone` 이 돌려준 값은 `endZone` 에 그대로 돌려줍니다.
     */
    class SW_API IProfilerBackend : public IProfilerMemoryOutput, public IProfilerGPUTimeline
    {
    public:
        IProfilerBackend()           = default;
        ~IProfilerBackend() override = default;

        IProfilerBackend( const IProfilerBackend& )            = delete;
        IProfilerBackend& operator=( const IProfilerBackend& ) = delete;

        /** @brief 출력 이름입니다("Tracy"). 로그 · 에디터 표시용입니다. */
        virtual const utf8* getBackendName() const = 0;
        /** @brief 뷰어가 지금 붙어 있으면 true 입니다. */
        virtual bool isViewerConnected() const = 0;

        /** @brief 이 스레드에서 구간을 엽니다. 반환값은 `endZone` 에 넘깁니다. */
        virtual uint64 beginZone( const ProfileZoneSite& site ) = 0;
        /** @brief `beginZone` 이 연 구간을 닫습니다. */
        virtual void endZone( uint64 zoneToken ) = 0;

        /** @brief 프레임 경계를 찍습니다. @p pFrameName 이 nullptr 이면 주 프레임, 아니면 그 이름의 보조 프레임입니다(이름은 프로세스 수명 문자열). */
        virtual void markFrame( const utf8* pFrameName ) = 0;
        /** @brief 이름 붙은 값 그래프에 한 점을 더합니다(프레임당 드로우 수 등). 이름은 프로세스 수명 문자열입니다. */
        virtual void plotValue( const utf8* pPlotName, float64 value ) = 0;
    };
} // namespace sw
