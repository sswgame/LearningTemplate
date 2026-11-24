#include "pch.h"

#include "Engine/CoreServices/DiagnosticsSubsystem.h"

#include "Core/Diagnostics/DeadlockDetector.h"
#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/Foundation/BuildInfo.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    DiagnosticsSubsystem::DiagnosticsSubsystem( bool bDiagnostics, bool bDeadlockDetection )
        : _memoryProfiler{ nullptr }
        , _deadlockDetector{ nullptr }
        , _bDiagnostics{ bDiagnostics }
        , _bDeadlockDetection{ bDeadlockDetection }
    {
    }

    DiagnosticsSubsystem::~DiagnosticsSubsystem()
    {
        // 위의 해제가 모두 태그 줄에서 빠진 뒤에 기동 뒤 기준선과 비교한다(어느 용도가 남았나, Debug). sw 블록의 누수는 이 보고가 말한다 —
        // 플랫폼 누수 검사(CRT)는 sw 할당자 밖(외부 라이브러리)의 바이트만 본다(힙 백엔드가 mimalloc 이면 sw 블록은 CRT 힙에 없다).
        if ( _memoryProfiler == nullptr )
            return;
        // 진단 구성(Debug — `Engine::initialize` 의 kDiagnostics)만 창을 연다. 다른 구성에서 닫으면 "창이 없다" 오류만 남는다.
        if ( _bDiagnostics )
        {
            // 기준선(열린 창)이 없으면 닫지 않고 빈 보고서를 찍어 "창이 없다" 는 오류 줄을 남긴다(CRT 보고와 같다). 보고만 한다 — 종료 경로는 늘린 태그 수로 멈추지 않는다.
            MemoryLeakWindow&      leakWindow = _memoryProfiler->getLeakWindow();
            const LeakWindowReport leakReport = leakWindow.isOpen() ? leakWindow.close() : LeakWindowReport{};
            (void)leakReport.print( "shutdown" );
        }
        _memoryProfiler->shutdown();
        _memoryProfiler.reset();
    }

    SubsystemInitResult DiagnosticsSubsystem::initializeSubsystem()
    {
        // 플랫폼 누수 추적을 가장 먼저 켠다(Windows Debug CRT: 할당 추적 + 보고를 stderr 로). 엔진 루프가 기동 뒤 기준선을 찍고 종료 뒤
        // `reportMemoryLeaks` 로 비교한다 — 켜지 않으면 누수 덤프가 디버거 출력으로만 나가 콘솔 · CI 에서 보이지 않는다.
        if ( _bDiagnostics )
            MemoryProfiler::enableMemoryLeakChecks();
        const bool bCreateMemoryProfiler = build::kShipping == false || _bDiagnostics;
        if ( bCreateMemoryProfiler && _memoryProfiler == nullptr )
        {
            _memoryProfiler = make_unique<MemoryProfiler>();
            _memoryProfiler->initialize();
            _memoryProfiler->setTrackingEnabled( _bDiagnostics );
        }
        // 교착 감지기는 잠금 관찰자를 걸 뿐 로그를 남기지 않는다 — 로거보다 먼저 서도 된다(찾은 교착은 로거가 선 뒤에 알린다).
        // 진단 구성은 스레드 합류만 듣고, `-deadlock-detection` 이면 `mutex` 잠금도 듣는다(코어 범위의 첫째라 아직 쥔 잠금이 없다 — 실행 중에는 끄지 않는다).
        if ( ( _bDiagnostics || _bDeadlockDetection ) && _deadlockDetector == nullptr )
        {
            _deadlockDetector = make_unique<DeadlockDetector>();
            _deadlockDetector->initialize();
        }
        if ( _bDeadlockDetection )
            mutex::enableLockTracking();
        return SubsystemInitResult::Succeeded;
    }

    void DiagnosticsSubsystem::shutdownSubsystem()
    {
        if ( _deadlockDetector != nullptr )
            _deadlockDetector->shutdown();
        _deadlockDetector.reset();
    }
} // namespace sw

namespace sw
{
    // 이 서브시스템이 든 메모리 프로파일러의 서비스 칸. 비어 있을 수 있다 — 진단 도구를 끈 구성(배포본)은 만들지 않는다(`Engine::initialize` 가 건다).
    SW_ENGINE_SERVICE( MemoryProfiler, HostOnly );
} // namespace sw
