#include "pch.h"

#include "App/Module/ModuleCallGuard.h"

#include "Core/Common/PlatformOsHeaders.h"

#if defined( SW_PLATFORM_LINUX )
    #include <csetjmp>
    #include <csignal>
#endif

namespace sw
{
    namespace
    {
        struct ModuleCallGuardInternal
        {
#if defined( SW_PLATFORM_WINDOWS )
            /** @brief 잡는 예외 코드입니다. 코드가 **어긋나서** 나는 것만 고른다 — 중단점(assert) · 스택 넘침 · C++ 예외는 여기 없다. */
            static constexpr uint32 kArrFaultCode[] = {
                EXCEPTION_ACCESS_VIOLATION,
                EXCEPTION_ILLEGAL_INSTRUCTION,
                EXCEPTION_PRIV_INSTRUCTION,
                EXCEPTION_INT_DIVIDE_BY_ZERO,
                EXCEPTION_INT_OVERFLOW,
                EXCEPTION_IN_PAGE_ERROR,
                EXCEPTION_ARRAY_BOUNDS_EXCEEDED,
                EXCEPTION_DATATYPE_MISALIGNMENT,
            };

            /** @brief `__except` 거르개입니다. 잡을 코드면 @p pOutFaultCode 에 적고 처리기로 들어갑니다. */
            static int32 filterFault( uint32 exceptionCode, uint32* pOutFaultCode )
            {
                for ( const uint32 faultCode : kArrFaultCode )
                {
                    if ( faultCode == exceptionCode )
                    {
                        *pOutFaultCode = exceptionCode;
                        return EXCEPTION_EXECUTE_HANDLER;
                    }
                }
                return EXCEPTION_CONTINUE_SEARCH;
            }

            /** @brief `__try` 가 든 함수에는 해제가 필요한 객체를 둘 수 없다(`/EHsc`). 그래서 부르기만 한다. */
            static bool invokeGuarded( const Delegate<void()>& call, uint32* pOutFaultCode )
            {
                __try
                {
                    call();
                }
                __except ( filterFault( GetExceptionCode(), pOutFaultCode ) )
                {
                    return false;
                }
                return true;
            }
#elif defined( SW_PLATFORM_LINUX )
            static constexpr int32 kArrFaultSignal[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL };

            static inline struct sigaction                   _s_arrPreviousAction[4]{}; ///< 설치 전의 처리기(크래시 처리기). 바깥 호출이 채운다
            static inline int32                              _s_installDepth{ 0 };      ///< 겹친 호출 깊이. 0 → 1 에서 설치, 1 → 0 에서 해제
            static inline thread_local sigjmp_buf*           t_pJump{ nullptr };
            static inline thread_local volatile sig_atomic_t t_faultSignal{ 0 };

            /** @brief 결함 시그널 처리기입니다. 지키는 호출 안이면 그 자리로 뛰어 돌아가고, 아니면 원래 처리기로 돌려놓습니다. */
            static void onFaultSignal( int32 signalNumber, siginfo_t*, void* )
            {
                if ( t_pJump == nullptr )
                {
                    // 지키는 호출 밖(다른 스레드)의 결함이다. 원래 처리기로 돌려놓고 돌아가면 같은 명령이 다시 결함을 내 그쪽이 받는다.
                    for ( uint32 signalIndex = 0; signalIndex < 4; ++signalIndex )
                    {
                        if ( kArrFaultSignal[signalIndex] == signalNumber )
                            sigaction( signalNumber, &_s_arrPreviousAction[signalIndex], nullptr );
                    }
                    return;
                }
                t_faultSignal = signalNumber;
                siglongjmp( *t_pJump, 1 );
            }

            /** @brief 처리기를 걸고 부른 뒤 되돌립니다. `sigsetjmp` 뒤에 바뀌어 `siglongjmp` 뒤에 읽히는 지역은 두지 않는다. */
            static bool invokeGuarded( const Delegate<void()>& call, uint32* pOutFaultCode )
            {
                if ( _s_installDepth++ == 0 )
                {
                    struct sigaction action{};
                    action.sa_sigaction = &onFaultSignal;
                    action.sa_flags     = SA_SIGINFO;
                    sigemptyset( &action.sa_mask );
                    for ( uint32 signalIndex = 0; signalIndex < 4; ++signalIndex )
                    {
                        sigaction( kArrFaultSignal[signalIndex], &action, &_s_arrPreviousAction[signalIndex] );
                    }
                }

                sigjmp_buf        jump;
                sigjmp_buf* const pOuterJump = t_pJump;
                bool              bCompleted{ false };
                t_pJump = &jump;
                // 두 번째 인자가 1 이면 시그널 마스크도 저장해, 처리기에서 뛰어 나올 때 막힌 시그널이 풀린다.
                if ( sigsetjmp( jump, 1 ) == 0 )
                {
                    call();
                    bCompleted = true;
                }
                else
                {
                    *pOutFaultCode = static_cast<uint32>( t_faultSignal );
                }
                t_pJump = pOuterJump;

                if ( --_s_installDepth == 0 )
                {
                    for ( uint32 signalIndex = 0; signalIndex < 4; ++signalIndex )
                    {
                        sigaction( kArrFaultSignal[signalIndex], &_s_arrPreviousAction[signalIndex], nullptr );
                    }
                }
                return bCompleted;
            }
#endif
        };
    } // namespace

    bool ModuleCallGuard::run( const Delegate<void()>& call, uint32& outFaultCode )
    {
        outFaultCode = 0;
#if defined( SW_PLATFORM_WINDOWS ) || defined( SW_PLATFORM_LINUX )
        return ModuleCallGuardInternal::invokeGuarded( call, &outFaultCode );
#else
        call();
        return true;
#endif
    }
} // namespace sw
