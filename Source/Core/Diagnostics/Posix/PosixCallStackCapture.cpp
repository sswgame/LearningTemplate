#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Diagnostics/CallStackCapture.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#if defined( SW_PLATFORM_LINUX ) || defined( SW_PLATFORM_ANDROID )
    #include "Core/Foundation/PlatformOSHeaders.h"

    // 시그널 컨텍스트의 레지스터 인덱스(REG_RIP 등)가 여기에 있다(glibc 는 _GNU_SOURCE 를 요구한다).
    #include <sys/ucontext.h>
    #include <ucontext.h>
    #if defined( SW_PLATFORM_ANDROID )
        #include <unwind.h> // bionic 은 API 33 부터 backtrace 가 있다 — 대상(29)에서는 언와인더로 모은다(언리얼 Android 와 같다)
    #endif

namespace sw
{
    namespace
    {
        /// @brief 초기화 참조 카운트입니다. 이 TU 전용이라 익명 네임스페이스에 둡니다(파일 스코프 static 과 같은 내부 링키지).
        atomic<int32> s_initRefCount{ 0 };
        /// @brief backtrace_symbols / dladdr 를 동시에 부르지 못하게 막습니다.
        mutex s_symbolMutex{};

    #if defined( SW_PLATFORM_ANDROID )
        /** @brief `_Unwind_Backtrace` 가 프레임마다 부르는 함수의 상태입니다. */
        struct UnwindCollectState
        {
            void** _ppFrame;
            int32  _capacity;
            int32  _count;
        };

        _Unwind_Reason_Code collectUnwindFrameInternal( _Unwind_Context* pContext, void* pArgument )
        {
            UnwindCollectState* pState = static_cast<UnwindCollectState*>( pArgument );
            const uintptr_t     pc     = _Unwind_GetIP( pContext );
            if ( pc == 0 )
                return _URC_NO_REASON;
            if ( pState->_count >= pState->_capacity )
                return _URC_END_OF_STACK;
            pState->_ppFrame[pState->_count++] = reinterpret_cast<void*>( pc );
            return _URC_NO_REASON;
        }
    #endif

        /** @brief 지금 스레드의 반환 주소를 @p ppFrame 에 최대 @p capacity 개 모읍니다(glibc `backtrace` 와 같은 뜻). */
        int32 collectFramesInternal( void** ppFrame, int32 capacity )
        {
    #if defined( SW_PLATFORM_LINUX )
            return backtrace( ppFrame, capacity );
    #elif defined( SW_PLATFORM_ANDROID )
            UnwindCollectState state{ ppFrame, capacity, 0 };
            _Unwind_Backtrace( &collectUnwindFrameInternal, &state );
            return state._count;
    #else
        #error "Unsupported platform"
    #endif
        }

        /** @brief 프레임 이름표 묶음입니다(glibc `backtrace_symbols`). bionic 에는 없어 nullptr — 그때는 dladdr 만 쓴다. 돌려받은 것은 free 로 놓는다. */
        utf8** symbolizeFramesRawInternal( void* const* ppFrame, int32 frameCount )
        {
    #if defined( SW_PLATFORM_LINUX )
            return backtrace_symbols( ppFrame, frameCount );
    #elif defined( SW_PLATFORM_ANDROID )
            (void)ppFrame;
            (void)frameCount;
            return nullptr;
    #else
        #error "Unsupported platform"
    #endif
        }

        /**
         * @brief 시그널 컨텍스트에서 폴트가 난 명령의 주소를 꺼냅니다.
         * @return 컨텍스트가 없으면 nullptr
         * @details 레지스터 이름만 아키텍처마다 다릅니다. 나머지(backtrace · dladdr · 디맹글 · 해시)는 POSIX 공통 코드입니다.
         */
        void* faultProgramCounterInternal( const void* pPlatformContext )
        {
            if ( pPlatformContext == nullptr )
                return nullptr;

            const ucontext_t* pContext = static_cast<const ucontext_t*>( pPlatformContext );
    #if defined( SW_X64 )
            return reinterpret_cast<void*>( pContext->uc_mcontext.gregs[REG_RIP] );
    #elif defined( SW_ARM64 )
            return reinterpret_cast<void*>( pContext->uc_mcontext.pc );
    #endif
        }
    } // namespace

    void CallStackCapture::initialize()
    {
        if ( s_initRefCount.fetch_add( 1, std::memory_order_acq_rel ) != 0 )
            return;

        // 심볼 초기화는 따로 없다. 대신 backtrace() 를 **한 번 불러 둔다** — glibc 는 처음 불릴 때 libgcc_s 를 dlopen 하고 거기서
        // malloc 한다(`man 3 backtrace` 의 NOTES). 그 첫 호출이 크래시 핸들러 안이면, malloc 의 락을 쥔 채 죽은 경우(힙 손상 abort)
        // 그 자리에서 멈춘다. 크로미움의 WarmUpBacktrace 와 같은 일이다.
        void* arrWarmUpFrame[2]{};
        collectFramesInternal( arrWarmUpFrame, 2 );
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
        int32 count      = collectFramesInternal( arrTempFrame, static_cast<int32>( CallStack::kMaxFrames + skipFrames + 1 ) );
        int32 validCount = count - ( skipFrames + 1 );
        if ( validCount > 0 )
        {
            outStack._frameCount = MathUtil::min<uint32>( static_cast<uint32>( validCount ), CallStack::kMaxFrames );
            for ( uint32 frameIndex = 0; frameIndex < outStack._frameCount; ++frameIndex )
            {
                outStack._arrFrame[frameIndex] = arrTempFrame[frameIndex + skipFrames + 1];
            }
        }

        // 프레임 주소로 해시를 계산한다.
        uint64 hash{ 0 };
        for ( uint32 frameIndex = 0; frameIndex < outStack._frameCount; ++frameIndex )
        {
            hash ^= reinterpret_cast<uint64>( outStack._arrFrame[frameIndex] ) + HashUtil::kGoldenRatio32 + ( hash << 6 ) + ( hash >> 2 );
        }
        outStack._hash = hash;
    }

    void CallStackCapture::captureFromContext( DeepCallStack& outStack, void* pPlatformContext, [[maybe_unused]] void* pPlatformThread )
    {
        outStack._frameCount = 0;

        // glibc 의 backtrace() 는 시그널 트램폴린에 CFI 가 있어 **폴트 지점까지** 따라 내려간다. 문제는 그 위에 핸들러
        // 프레임(이 함수 · reportCrash · onFatalSignal · 트램폴린)이 얹혀 있다는 것뿐이다. 그래서 컨텍스트에서 폴트 PC 를
        // 꺼내 그 프레임을 찾고 **거기서부터** 담는다. 그러면 스택이 폴트 지점에서 시작하는 Windows(StackWalk64 + CONTEXT)
        // 와 같은 형태가 된다.
        void*       arrTempFrame[DeepCallStack::kMaxFrames + 16];
        const int32 count = collectFramesInternal( arrTempFrame, static_cast<int32>( DeepCallStack::kMaxFrames + 16 ) );
        if ( count <= 0 )
            return;

        void* pFaultPc = faultProgramCounterInternal( pPlatformContext );

        int32 startIndex = 0;
        if ( pFaultPc != nullptr )
        {
            // 폴트 PC 와 정확히 같은 프레임을 찾는다. 찾지 못하면(트램폴린 형태가 다른 환경) 폴트 PC 를 0번 프레임으로 직접
            // 넣어, 적어도 어디서 죽었는지는 리포트에 남게 한다.
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

    string CallStackCapture::symbolizeFrames( void* const* ppFrame, uint32 frameCount, atomic<uint32>* pFrameProgress )
    {
        if ( ppFrame == nullptr || frameCount == 0 )
            return kEmptyCallStackText;

        // 크래시 경로에서도 불리므로 절대 막히면 안 된다. 다른 스레드가 심볼 변환 중이면 교착 대신 주소만 출력한다
        // (맵 파일로 나중에 풀 수 있다). **이 관문은 플랫폼 공통**이고, 그 아래 본체만 플랫폼마다 완전히 다르다
        // (DbgHelp 와 backtrace_symbols).
        UniqueLock<mutex> lock{ s_symbolMutex, std::defer_lock };
        if ( lock.try_lock() == false )
            return formatRawCallStackFrames( ppFrame, frameCount );

        StringBuilder<constant::buffer::kMax8192> sb;

        utf8** ppSymbol = symbolizeFramesRawInternal( ppFrame, static_cast<int32>( frameCount ) );
        for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
        {
            if ( pFrameProgress != nullptr )
                pFrameProgress->fetch_add( 1 ); // 크래시 보고의 시한은 진척 없는 시간이다 — 기호 파일을 처음 읽는 느린 프레임에서 끊기지 않게
            string symbolName = ( ppSymbol != nullptr ) ? ppSymbol[frameIndex] : "??";

            // dladdr 로 더 정확한 심볼 정보를 얻어 본다
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

    string CallStackCapture::symbolize( const CallStack& stack )
    {
        return symbolizeFrames( stack._arrFrame, stack._frameCount, nullptr );
    }

    string CallStackCapture::symbolize( const DeepCallStack& stack, atomic<uint32>* pFrameProgress )
    {
        return symbolizeFrames( stack._arrFrame, stack._frameCount, pFrameProgress );
    }

} // namespace sw

#endif
