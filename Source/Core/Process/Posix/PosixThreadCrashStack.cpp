#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/Process/ThreadCrashStack.h"

#if defined( SW_PLATFORM_LINUX )
    #include <csignal>

SW_LOG_CALLER( "PosixCrashHandler" );
namespace sw
{
    namespace
    {
        struct PosixThreadCrashStackInternal
        {
            /**
             * @brief 대체 시그널 스택의 크기입니다.
             * @details 스택 오버플로로 생긴 SIGSEGV 는 스택이 이미 바닥난 상태라 그 스택 위에서 핸들러를 실행할 수 없습니다. 핸들러에
             *          들어가는 순간 다시 폴트가 나고, 프로세스는 **아무 기록도 없이** 죽습니다. 그래서 별도 스택을 깔고 SA_ONSTACK 으로
             *          그 위에서 핸들러를 돌립니다. 최신 glibc 의 SIGSTKSZ 는 sysconf() 를 부르도록 바뀌어 상수가 아니라 넉넉한 고정 크기를 쓴다.
             *          리포트 경로가 스택에 올리는 것은 StringBuilder<8192> 와 DeepCallStack(64 프레임) 정도이고, 나머지는 힙이다.
             */
            static constexpr size_t kSignalStackSize = 128 * 1024;
        };

        /**
         * @brief 이 스레드의 대체 시그널 스택입니다. **`sigaltstack` 은 스레드마다다** — 한 스레드에만 깔면 작업 스레드 · 렌더 스레드의
         *        스택 오버플로는 기록 없이 죽는다. 스레드가 끝날 때 스택을 끄고 돌려준다.
         */
        struct ThreadSignalStackInternal
        {
            uint8* _pStack{ nullptr };

            ~ThreadSignalStackInternal() { release(); }

            void release()
            {
                if ( _pStack == nullptr )
                    return;
                stack_t disableStack{};
                disableStack.ss_flags = SS_DISABLE;
                sigaltstack( &disableStack, nullptr );
                Memory::free( _pStack );
                _pStack = nullptr;
            }
        };

        thread_local ThreadSignalStackInternal t_signalStack{};
    } // namespace
} // namespace sw

namespace sw
{
    void ThreadCrashStack::initializeCurrentThread()
    {
        if ( t_signalStack._pStack != nullptr )
            return;

        uint8* pStack = static_cast<uint8*>( Memory::allocate( PosixThreadCrashStackInternal::kSignalStackSize ) );
        if ( pStack == nullptr )
            return;

        stack_t signalStack{};
        signalStack.ss_sp    = pStack;
        signalStack.ss_size  = PosixThreadCrashStackInternal::kSignalStackSize;
        signalStack.ss_flags = 0;
        if ( sigaltstack( &signalStack, nullptr ) != 0 )
        {
            Memory::free( pStack );
            SW_LOG_WARNING( "sigaltstack failed — a stack overflow on this thread will not be reported" );
            return;
        }
        t_signalStack._pStack = pStack;
    }

    void ThreadCrashStack::releaseCurrentThread()
    {
        t_signalStack.release();
    }
} // namespace sw

#endif
