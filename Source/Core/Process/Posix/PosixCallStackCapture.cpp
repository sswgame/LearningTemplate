#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Math/MathUtil.h"
#include "Core/Process/CallStackCapture.h"
#include "Core/String/StringBuilder.h"

#if defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_MACOS )
    #include "Core/Common/PlatformOsHeaders.h"

    #if defined( SW_PLATFORM_LINUX )
        // REG_RIP 등 시그널 컨텍스트의 레지스터 인덱스가 여기 있다 (glibc 는 _GNU_SOURCE 를 요구한다).
        #include <sys/ucontext.h>
        #include <ucontext.h>
    #endif

namespace sw
{
    namespace
    {
        /// @brief 초기화 참조 카운트. 이 TU 전용이라 익명 네임스페이스에 둔다(파일 스코프 static 과 같은 내부 링키지).
        atomic<int32> s_initRefCount{ 0 };
        /// @brief backtrace_symbols / dladdr 동시 호출을 막는다.
        mutex s_symbolMutex{};

        /**
         * @brief 시그널 컨텍스트에서 폴트가 난 명령 주소를 꺼냅니다.
         * @return 알 수 없는 아키텍처거나 컨텍스트가 없으면 nullptr.
         * @details **이 파일에서 OS 마다 갈리는 곳은 여기뿐이다.** 나머지(backtrace · dladdr · 디맹글 ·
         *          해시)는 POSIX 라 두 플랫폼이 같은 코드를 쓴다. macOS 는 지금 nullptr 을 돌려주고,
         *          그러면 아래 captureFromContext 가 트리밍 없이 그대로 담는다 — 합치기 전 동작 그대로다.
         */
        void* faultProgramCounterInternal( const void* pPlatformContext )
        {
            if ( pPlatformContext == nullptr )
                return nullptr;

    #if defined( SW_PLATFORM_LINUX )
            const ucontext_t* pContext = static_cast<const ucontext_t*>( pPlatformContext );
        #if defined( __x86_64__ )
            return reinterpret_cast<void*>( pContext->uc_mcontext.gregs[REG_RIP] );
        #elif defined( __i386__ )
            return reinterpret_cast<void*>( pContext->uc_mcontext.gregs[REG_EIP] );
        #elif defined( __aarch64__ )
            return reinterpret_cast<void*>( pContext->uc_mcontext.pc );
        #elif defined( __arm__ )
            return reinterpret_cast<void*>( pContext->uc_mcontext.arm_pc );
        #else
            (void)pContext;
            return nullptr;
        #endif
    #else
            // macOS 의 폴트 PC 추출은 아직 없다(Darwin 의 mcontext 는 모양이 다르다). 없으면 트리밍만
            // 건너뛰고 스택 자체는 그대로 남으므로, 리포트가 비지는 않는다.
            return nullptr;
    #endif
        }

        string symbolizeFrames( void* const* ppFrame, uint32 frameCount )
        {
            if ( ppFrame == nullptr || frameCount == 0 )
                return "[Empty CallStack]";

            StringBuilder<constant::kMaxBuffer8192> sb;

            std::unique_lock<mutex> lock{ s_symbolMutex, std::try_to_lock };
            if ( lock.owns_lock() == false )
            {
                for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
                {
                    sb.appendFormat( "  [%#] 0x%# (symbols busy)\n", frameIndex,
                                     Fmt( reinterpret_cast<uint64>( ppFrame[frameIndex] ), Format().hex() ) );
                }
                return string( sb.view() );
            }

            utf8** ppSymbol = backtrace_symbols( ppFrame, frameCount );
            for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
            {
                string symbolName = ( ppSymbol != nullptr ) ? ppSymbol[frameIndex] : "??";

                // dladdr 로 더 정확한 심볼 정보 시도
                Dl_info    info{};
                const bool bResolved = ( dladdr( ppFrame[frameIndex], &info ) != 0 && info.dli_sname != nullptr );
                if ( bResolved )
                {
                    int32 status     = 0;
                    utf8* pDemangled = abi::__cxa_demangle( info.dli_sname, nullptr, nullptr, &status );
                    symbolName       = ( status == 0 && pDemangled != nullptr ) ? pDemangled : info.dli_sname;
                    free( pDemangled );
                }

                sb.appendFormat( "  [%#] %#", frameIndex, symbolName.c_str() );
                if ( bResolved && info.dli_saddr != nullptr )
                {
                    const ptrdiff_t frameOffset =
                        static_cast<const utf8*>( ppFrame[frameIndex] ) - static_cast<const utf8*>( info.dli_saddr );
                    sb.appendFormat( " + 0x%#", Fmt( static_cast<uint64>( frameOffset ), Format().hex() ) );
                }
                sb.append( "\n" );
            }
            free( ppSymbol );

            return string( sb.view() );
        }
    } // namespace

    void CallStackCapture::initialize()
    {
        if ( s_initRefCount.fetch_add( 1, std::memory_order_acq_rel ) != 0 )
            return;

        // POSIX: 추가 심볼 초기화 불필요
    }

    void CallStackCapture::shutdown()
    {
        const int32 previous = s_initRefCount.fetch_sub( 1, std::memory_order_acq_rel );
        if ( previous <= 0 )
        {
            s_initRefCount.store( 0, std::memory_order_release );
            return;
        }
    }

    void CallStackCapture::capture( CallStack& outStack, uint32 skipFrames )
    {
        outStack._frameCount = 0;
        outStack._hash       = 0;

        void* arrTempFrame[CallStack::kMaxFrames + 16];
        int32 count      = backtrace( arrTempFrame, CallStack::kMaxFrames + skipFrames + 1 );
        int32 validCount = count - ( skipFrames + 1 );
        if ( validCount > 0 )
        {
            outStack._frameCount = MathUtil::min<uint32>( static_cast<uint32>( validCount ), CallStack::kMaxFrames );
            for ( uint32 frameIndex = 0; frameIndex < outStack._frameCount; ++frameIndex )
            {
                outStack._arrFrame[frameIndex] = arrTempFrame[frameIndex + skipFrames + 1];
            }
        }

        // 프레임 주소로 해시를 계산합니다.
        uint64 hash{ 0 };
        for ( uint32 frameIndex = 0; frameIndex < outStack._frameCount; ++frameIndex )
        {
            hash ^= reinterpret_cast<uint64>( outStack._arrFrame[frameIndex] ) + 0x9e3779b9 + ( hash << 6 ) + ( hash >> 2 );
        }
        outStack._hash = hash;
    }

    void CallStackCapture::captureFromContext( DeepCallStack& outStack, void* pPlatformContext )
    {
        outStack._frameCount = 0;

        // glibc 의 backtrace() 는 시그널 트램폴린에 CFI 가 있어 **폴트 지점까지** 걸어 내려간다.
        // 문제는 그 위에 핸들러 프레임(이 함수·reportCrash·onFatalSignal·트램폴린)이 얹혀 있다는 것뿐이다.
        // 그래서 컨텍스트에서 폴트 PC 를 꺼내 그 프레임을 찾아 **거기서부터** 담는다 — 스택이 폴트 지점에서
        // 시작하는 Windows(StackWalk64 + CONTEXT) 와 같은 모양이 된다.
        void*       arrTempFrame[DeepCallStack::kMaxFrames + 16];
        const int32 count = backtrace( arrTempFrame, static_cast<int32>( DeepCallStack::kMaxFrames + 16 ) );
        if ( count <= 0 )
            return;

        void* pFaultPc = faultProgramCounterInternal( pPlatformContext );

        int32 startIndex = 0;
        if ( pFaultPc != nullptr )
        {
            // 폴트 PC 와 정확히 같은 프레임을 찾는다. 못 찾으면(트램폴린 모양이 다른 환경) 폴트 PC 를
            // 0번 프레임으로 직접 얹어, 적어도 어디서 죽었는지는 리포트에 남게 한다.
            int32 matchIndex = -1;
            for ( int32 frameIndex = 0; frameIndex < count; ++frameIndex )
            {
                if ( arrTempFrame[frameIndex] == pFaultPc )
                {
                    matchIndex = frameIndex;
                    break;
                }
            }
            if ( matchIndex >= 0 )
            {
                startIndex = matchIndex;
            }
            else
            {
                outStack._arrFrame[0] = pFaultPc;
                outStack._frameCount  = 1;
            }
        }

        for ( int32 frameIndex = startIndex; frameIndex < count; ++frameIndex )
        {
            if ( outStack._frameCount >= DeepCallStack::kMaxFrames )
                break;
            outStack._arrFrame[outStack._frameCount++] = arrTempFrame[frameIndex];
        }
    }

    string CallStackCapture::symbolize( const CallStack& stack )
    {
        return symbolizeFrames( stack._arrFrame, stack._frameCount );
    }

    string CallStackCapture::symbolize( const DeepCallStack& stack )
    {
        return symbolizeFrames( stack._arrFrame, stack._frameCount );
    }

} // namespace sw

#endif
