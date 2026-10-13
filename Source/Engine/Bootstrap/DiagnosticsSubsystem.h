/**
 * @file Engine/Bootstrap/DiagnosticsSubsystem.h
 * @brief 부트스트랩 범위의 뿌리 — 메모리 프로파일러와 (진단 구성이면) 교착 감지기를 세웁니다.
 * @details 부트스트랩 범위에서 가장 먼저 서고(의존이 없는 뿌리) 가장 나중에 사라진다. 그 앞에서 잡힌 sw 블록이 없도록 이름 풀 · 로거보다 먼저 선다 —
 *          메모리 프로파일러는 모든 해제가 태그 줄에서 빠진 뒤(소멸자)에 기동 뒤 기준선과 견주어 보고한다.
 *          메모리 프로파일러는 배포본이 아닌 모든 구성에 선다(추적은 진단 구성에서만 켜 두고 Release 는 `-gv_memoryTracking=1` 로 켠다).
 *          배포본에는 할당 헤더가 없어 셀 것이 없으므로 진단 구성(테스트 하네스)에서만 세운다. 교착 감지기와 플랫폼 누수 검사는 진단 구성만.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

namespace sw
{
    class DeadlockDetector;
    class MemoryProfiler;

    /**
     * @class DiagnosticsSubsystem
     * @brief 진단 도구 서브시스템입니다. 진단 구성 여부는 만들 때 받는다(호스트마다 다르다 — App 은 Debug 만, 테스트 하네스는 늘).
     */
    class SW_API DiagnosticsSubsystem final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "Diagnostics";

        /** @brief @p bDiagnostics 면 교착 감지기 · 플랫폼 누수 검사 · 할당 추적을 켭니다. */
        explicit DiagnosticsSubsystem( bool bDiagnostics );
        /** @brief 메모리 프로파일러의 종료 보고(Debug) 뒤 그것을 놓습니다 — 같은 범위의 다른 것이 모두 사라진 뒤다. */
        ~DiagnosticsSubsystem() override;

        DiagnosticsSubsystem( const DiagnosticsSubsystem& )            = delete;
        DiagnosticsSubsystem& operator=( const DiagnosticsSubsystem& ) = delete;

        SubsystemInitResult initializeSubsystem() override;
        /** @brief 교착 감지기를 뗀다. 메모리 프로파일러는 소멸자까지 남는다(뒤의 해제가 그것을 부른다). */
        void        shutdownSubsystem() override;
        const utf8* getSubsystemName() const override { return kSubsystemName; }

        /** @brief 메모리 프로파일러입니다. 배포본(진단 구성이 아니면)에는 없어 nullptr 입니다. */
        MemoryProfiler* findMemoryProfiler() const { return _memoryProfiler.get(); }

    private:
        unique_ptr<MemoryProfiler>   _memoryProfiler;   ///< 배포본이 아니면 있다
        unique_ptr<DeadlockDetector> _deadlockDetector; ///< 진단 구성에서만 있다
        bool                         _bDiagnostics;     ///< 만들 때 받은 진단 구성 여부
    };
} // namespace sw
