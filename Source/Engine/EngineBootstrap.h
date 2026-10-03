/**
 * @file Engine/EngineBootstrap.h
 * @brief 기동 표(`EngineStartupStepList.xxx`) **앞**의 고정 부트스트랩과 표 **뒤**의 끝 정리입니다. 두 호스트(`EngineLoop` · 시험 하네스)가 함께 씁니다.
 * @details 이름 풀 · 로거 · 크래시 핸들러 · 리소스 루트 · 진단 도구(교착 감지기 · 메모리 프로파일러) · 명령줄 · 전역 변수가 여기서 서고,
 *          `shutdown` 이 그 역순으로 내린다. 서비스 표를 언제 끊는지(`unbindEngineServices`) · 로거를 언제 세우는지도 여기 한 곳에서 정한다.
 *          표를 **채우는** 일(`bindEngineServices`)은 호스트가 한다 — 호스트마다 직접 만드는 서비스(`HostCreated`)가 다르고,
 *          `CheckEngineServiceBinding` 이 그 대입을 호스트 파일에서 본다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    struct EngineOwnedServices;
    struct EngineServices;

    class DeadlockDetector;
    class Logger;
    class MemoryProfiler;

    /**
     * @class EngineBootstrap
     * @brief 기동 표 밖의 부트스트랩을 세우고 내립니다.
     * @note 소멸자는 `shutdown` 을 부릅니다(초기화 도중에 실패해 돌아간 호스트도 같은 순서로 내린다). `EngineOwnedServices` 는 이 객체보다 오래 살아야 합니다.
     */
    class SW_API EngineBootstrap
    {
    public:
        EngineBootstrap();
        ~EngineBootstrap();

        EngineBootstrap( const EngineBootstrap& )            = delete;
        EngineBootstrap& operator=( const EngineBootstrap& ) = delete;

        /**
         * @brief 이름 풀 · 로거 · 크래시 핸들러 · 리소스 루트 · (@p bDiagnostics 면) 교착 감지기 · 메모리 프로파일러를 세우고,
         *        @p owned 에 명령줄 · 전역 변수를 만들어 서로 잇습니다. 명령줄은 아직 파싱하지 않습니다(`parseCommandLine`).
         * @return 리소스 루트를 찾지 못하면 false 입니다(진단은 로거에 남는다).
         */
        [[nodiscard]] bool initialize( EngineOwnedServices& owned, bool bDiagnostics );
        /** @brief 인자를 파싱하고 전역 변수에 반영합니다. 하네스는 시험 프레임워크 인자를 먼저 걸러 낸 나머지를 넘깁니다. */
        void parseCommandLine( int32 argc, utf8* pArgv[] );
        /**
         * @brief 서비스 표에 저장소의 `EngineCreated` 칸과 메모리 프로파일러를 채웁니다. 나머지 `HostCreated` 칸과 `bindEngineServices` 는 호스트의 몫입니다.
         */
        void fillServices( EngineServices& outServices ) const;
        /**
         * @brief 표 밖 부트스트랩을 세운 역순으로 내립니다. 기동 단계의 종료 · 해제(`EngineStartupSequence`) 뒤에 부릅니다. 두 번 불러도 됩니다.
         * @details 전역 변수 기본값 복원 → 표 밖 서비스 해제(목록의 역순) → 서비스 표 끊기 → 로거 스레드 → 메모리 프로파일러 →
         *          교착 감지기 → 이름 풀 → 크래시 핸들러 → 로거 객체.
         */
        void shutdown();

    private:
        EngineOwnedServices*         _pOwned;           ///< `initialize` 가 받은 저장소(명령줄 · 전역 변수 · 표 밖 서비스)
        unique_ptr<Logger>           _logger;           ///< 처음 서고 마지막에 사라진다
        unique_ptr<DeadlockDetector> _deadlockDetector; ///< 진단 구성에서만 있다
        unique_ptr<MemoryProfiler>   _memoryProfiler;   ///< 진단 구성에서만 있다
        bool                         _bStarted;         ///< 이름 풀을 세웠다(= `shutdown` 이 내릴 것이 있다)
    };
} // namespace sw
