#include "pch.h"

#include "Engine/Bootstrap/DiagnosticsSubsystem.h"

#include "Core/Diagnostics/DeadlockDetector.h"
#include "Core/Diagnostics/MemoryProfiler.h"

namespace sw
{
    DiagnosticsSubsystem::DiagnosticsSubsystem( bool bDiagnostics )
        : _memoryProfiler{ nullptr }
        , _deadlockDetector{ nullptr }
        , _bDiagnostics{ bDiagnostics }
    {
    }

    DiagnosticsSubsystem::~DiagnosticsSubsystem()
    {
        // 위의 해제가 모두 태그 줄에서 빠진 뒤에 기동 뒤 기준선과 비교한다(어느 용도가 남았나, Debug). 플랫폼 누수 검사(CRT)는 바이트만 말하고,
        // 이 보고는 그 바이트가 어느 하위 시스템의 것인지 말한다.
        if ( _memoryProfiler == nullptr )
            return;
#if defined( SW_DEBUG )
        // 기준선(열린 창)이 없으면 닫지 않고 빈 보고서를 찍어 "창이 없다" 는 오류 줄을 남긴다(CRT 보고와 같다). 보고만 한다 — 종료 경로는 늘린 태그 수로 멈추지 않는다.
        const LeakWindowReport leakReport = _memoryProfiler->isLeakWindowOpen() ? _memoryProfiler->endLeakWindow() : LeakWindowReport{};
        (void)MemoryProfiler::printLeakWindowReport( leakReport, "shutdown" );
#endif
        _memoryProfiler->shutdown();
        _memoryProfiler.reset();
    }

    SubsystemInitResult DiagnosticsSubsystem::initializeSubsystem()
    {
        // 플랫폼 누수 추적을 가장 먼저 켠다(Windows Debug CRT: 할당 추적 + 보고를 stderr 로). 엔진 루프가 기동 뒤 기준선을 찍고 종료 뒤
        // `reportMemoryLeaks` 로 비교한다 — 켜지 않으면 누수 덤프가 디버거 출력으로만 나가 콘솔 · CI 에서 보이지 않는다.
        if ( _bDiagnostics )
            MemoryProfiler::enableMemoryLeakChecks();
#if defined( SW_SHIPPING )
        const bool bCreateMemoryProfiler = _bDiagnostics;
#else
        const bool bCreateMemoryProfiler = true;
#endif
        if ( bCreateMemoryProfiler && _memoryProfiler == nullptr )
        {
            _memoryProfiler = make_unique<MemoryProfiler>();
            _memoryProfiler->initialize();
            _memoryProfiler->setTrackingEnabled( _bDiagnostics );
        }
        // 교착 감지기는 잠금 관찰자를 걸 뿐 로그를 남기지 않는다 — 로거보다 먼저 서도 된다(찾은 교착은 로거가 선 뒤에 알린다).
        if ( _bDiagnostics && _deadlockDetector == nullptr )
        {
            _deadlockDetector = make_unique<DeadlockDetector>();
            _deadlockDetector->initialize();
        }
        return SubsystemInitResult::Succeeded;
    }

    void DiagnosticsSubsystem::shutdownSubsystem()
    {
        if ( _deadlockDetector != nullptr )
            _deadlockDetector->shutdown();
        _deadlockDetector.reset();
    }
} // namespace sw
