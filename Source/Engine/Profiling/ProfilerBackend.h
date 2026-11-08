/**
 * @file ProfilerBackend.h
 * @brief 외부 프로파일러 출력(`IProfilerBackend`)의 활성 칸과 계측 지점의 프로세스 수명 저장소입니다.
 *
 * [켜는 법]
 * 개발 빌드에서도 기본은 꺼짐입니다. 출력 구현(Tracy — `DevTools/Profiling/Tracy/TracyProfilerBackend`)은 개발 도구에 있고, 켜는 쪽이 `startBackend` 로 꽂습니다
 * (`-gv_tracy=1` 은 `ProfilerLaunchSwitches` 가 기동 단계에, 에디터 프로파일러 패널의 "Tracy 열기" 는 실행 중에). 엔진은 어떤 외부 프로파일러인지 모릅니다.
 *
 * [Shipping]
 * `SW_PROFILER_BACKEND_COMPILED` 가 0 이라 계측 매크로가 지점을 등록하지도, 출력에 묻지도 않습니다. 개발 도구가 없어 출력도 없습니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Profiling/IProfilerBackend.h"

/**
 * @brief 외부 프로파일러 출력이 이 빌드에 컴파일되는지 나타냅니다. Shipping 에서는 0 입니다(계측 매크로가 출력을 묻지도 않는다).
 * @details Tracy 가 이 빌드에 링크되었는지는 따로입니다(`TracyProfilerBackend::isCompiled`). 링크하지 않은 개발 빌드는 출력 자리만 있고 비어 있습니다.
 */
#if defined( SW_SHIPPING )
    #define SW_PROFILER_BACKEND_COMPILED 0
#else
    #define SW_PROFILER_BACKEND_COMPILED 1
#endif

namespace sw
{
    /**
     * @class ProfilerBackend
     * @brief 활성 외부 프로파일러 출력 하나와 계측 지점 저장소입니다(모두 static — 프로세스에 하나).
     * @details 출력은 **프로세스 수명**이어야 합니다. 열린 구간을 닫는 스레드가 엔진 종료보다 늦을 수 있고, 뷰어는 지점 문자열을
     *          나중에 물어봅니다. 그래서 지점 저장소도 엔진 서비스가 아니라 이 모듈(Engine.dll)의 정적 저장소이고, 이름 풀
     *          (`HashedStringPool`, 엔진 종료 때 내린다)에 기대지 않습니다.
     */
    class SW_API ProfilerBackend
    {
    public:
        /** @brief 등록할 수 있는 계측 지점 수입니다. 넘으면 그 지점은 외부 출력에 나가지 않습니다(측정이 실행을 막지 않는다). */
        static constexpr uint32 kMaxZoneSite = 1024;
        /** @brief 지점 · 그래프 이름 문자열을 복사해 두는 저장소 크기(바이트)입니다. */
        static constexpr uint32 kNameArenaBytes = 64u * 1024u;

        /** @brief 메모리 관찰을 떼고 활성 출력을 비웁니다. 출력 객체는 살려 둡니다(늦게 닫히는 구간이 그것을 부른다). */
        static void shutdown();

        /**
         * @brief 외부 출력 @p backend 를 활성으로 꽂고 계측을 잇습니다 — 태스크 · 스테이지 구간 훅, @p bObserveAllocations 면 할당 관찰, 엔진 `FrameProfiler`(카운터 그래프 · GPU 타임스탬프가
         *        그 경로를 탄다). 이미 같은 출력이 활성이면 아무것도 하지 않고 true 입니다.
         * @note @p backend 는 프로세스 수명이어야 합니다(늦게 닫히는 구간이 그것을 부른다 — `setActiveBackend` 와 같은 규칙).
         * @return 이 빌드에 외부 출력이 없으면(Shipping) false 입니다.
         */
        static bool startBackend( IProfilerBackend& backend, bool bObserveAllocations );

        /** @brief 활성 출력입니다. 없으면 nullptr 입니다. 어느 스레드에서나 부를 수 있습니다. */
        static IProfilerBackend* getActiveBackend();
        /**
         * @brief 활성 출력을 바꿉니다. 테스트가 기록용 가짜 출력을 꽂을 때 씁니다.
         * @note @p pBackend 는 그것으로 열린 마지막 구간이 닫힐 때까지 살아 있어야 합니다(구간은 연 출력으로 닫는다).
         */
        static void setActiveBackend( IProfilerBackend* pBackend );
#if SW_PROFILER_BACKEND_COMPILED
        /** @brief 출력을 켤 때(`startBackend`) `TaskManager::bindProfileHook` 에 꽂는 훅입니다(테스트가 같은 것을 꽂는다). 태스크 · 스테이지 이름이 구간 이름이 됩니다. */
        static const TaskProfileHook* getTaskProfileHook();
#endif

        /**
         * @brief 계측 지점 하나를 등록하고 프로세스 수명 주소를 돌려줍니다. 같은 (이름 · 파일 · 줄)이면 같은 주소입니다.
         * @details 문자열은 복사합니다(핫 리로드로 내려간 모듈의 상수를 가리키지 않는다). 파일 경로는 `Source/` 부터만 둡니다.
         *          저장소가 차면 nullptr 이고, 그 지점은 외부 출력에 나가지 않습니다.
         */
        static const ProfileZoneSite* registerZoneSite( const utf8* pName, const utf8* pFunction, const utf8* pFile, uint32 line );
        /** @brief 이름을 프로세스 수명 저장소에 복사해 돌려줍니다(그래프 이름 등). 같은 이름이면 같은 주소, 저장소가 차면 nullptr 입니다. */
        static const utf8* internName( const utf8* pName );
    };
} // namespace sw
