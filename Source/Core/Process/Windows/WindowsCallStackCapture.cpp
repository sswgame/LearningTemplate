#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Process/CallStackCapture.h"
#include "Core/String/StringBuilder.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"

namespace sw
{
    // DeadlockDetector / MemoryProfiler / CrashHandler 가 각자 initialize·shutdown 한다.
    // bool 래치로 두면 먼저 shutdown 한 쪽이 SymCleanup 을 불러 나머지의 심볼화가 죽는다
    // (특히 종료 중 크래시에서 스택을 못 남긴다). 참조 카운트로 마지막 소유자만 정리한다.
    static atomic<int32> s_initRefCount{ 0 };
    static mutex         s_symbolMutex{};

    void CallStackCapture::initialize()
    {
        if ( s_initRefCount.fetch_add( 1, std::memory_order_acq_rel ) != 0 )
            return;

        std::scoped_lock<mutex> lock{ s_symbolMutex };
        SymSetOptions( SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES );
        HANDLE process = GetCurrentProcess();
        SymInitialize( process, nullptr, FALSE );
        SymRefreshModuleList( process );
    }

    void CallStackCapture::shutdown()
    {
        const int32 previous = s_initRefCount.fetch_sub( 1, std::memory_order_acq_rel );
        if ( previous <= 0 )
        {
            // 짝이 안 맞는 shutdown. 카운트를 음수로 두지 않는다.
            s_initRefCount.store( 0, std::memory_order_release );
            return;
        }
        if ( previous != 1 )
            return;

        std::scoped_lock<mutex> lock{ s_symbolMutex };
        HANDLE                  process = GetCurrentProcess();
        SymCleanup( process );
    }

    void CallStackCapture::capture( CallStack& outStack, uint32 skipFrames )
    {
        outStack._frameCount = 0;
        outStack._hash       = 0;

        // 이 capture 함수 자신을 건너뛰기 위해 +1
        outStack._frameCount = CaptureStackBackTrace( skipFrames + 1, CallStack::kMaxFrames, outStack._arrFrame, nullptr );

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

        if ( pPlatformContext == nullptr )
            return;

        // StackWalk64 는 넘겨준 CONTEXT 를 진행하며 수정하므로 복사본으로 걷는다.
        CONTEXT walkContext = *static_cast<const CONTEXT*>( pPlatformContext );

        STACKFRAME64 frame{};
        DWORD        machineType;
    #if defined( _M_X64 )
        machineType            = IMAGE_FILE_MACHINE_AMD64;
        frame.AddrPC.Offset    = walkContext.Rip;
        frame.AddrFrame.Offset = walkContext.Rbp;
        frame.AddrStack.Offset = walkContext.Rsp;
    #elif defined( _M_ARM64 )
        machineType            = IMAGE_FILE_MACHINE_ARM64;
        frame.AddrPC.Offset    = walkContext.Pc;
        frame.AddrFrame.Offset = walkContext.Fp;
        frame.AddrStack.Offset = walkContext.Sp;
    #else
        return;
    #endif
        frame.AddrPC.Mode    = AddrModeFlat;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Mode = AddrModeFlat;

        HANDLE                  process = GetCurrentProcess();
        HANDLE                  thread  = GetCurrentThread();
        std::scoped_lock<mutex> lock{ s_symbolMutex }; // StackWalk64 도 dbghelp 전역 상태를 쓴다.
        while ( outStack._frameCount < DeepCallStack::kMaxFrames )
        {
            if ( StackWalk64( machineType, process, thread, &frame, &walkContext,
                              nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr ) == FALSE )
                break;
            if ( frame.AddrPC.Offset == 0 )
                break;
            outStack._arrFrame[outStack._frameCount++] = reinterpret_cast<void*>( frame.AddrPC.Offset );
        }
    }

    namespace
    {
        /** @brief CallStack / DeepCallStack 공용 심볼화 본체입니다. */
        string symbolizeFrames( void* const* ppFrame, uint32 frameCount )
        {
            if ( ppFrame == nullptr || frameCount == 0 )
                return "[Empty CallStack]";

            // 32프레임 × (심볼 + 전체 파일 경로 + 라인)은 2KB 를 쉽게 넘긴다.
            StringBuilder<constant::kMaxBuffer8192> sb;

            // 크래시 경로에서도 불리므로 절대 막히면 안 된다. 다른 스레드가 심볼화 중이면
            // 교착 대신 주소만 출력한다(맵 파일로 후처리 가능).
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

            HANDLE                      process = GetCurrentProcess();
            alignas( SYMBOL_INFO ) utf8 symbolBuffer[sizeof( SYMBOL_INFO ) + MAX_SYM_NAME * sizeof( TCHAR )];
            SYMBOL_INFO*                pSymbol = reinterpret_cast<SYMBOL_INFO*>( symbolBuffer );
            pSymbol->SizeOfStruct               = sizeof( SYMBOL_INFO );
            pSymbol->MaxNameLen                 = MAX_SYM_NAME;

            for ( uint32 frameIndex = 0; frameIndex < frameCount; ++frameIndex )
            {
                DWORD64 address = reinterpret_cast<DWORD64>( ppFrame[frameIndex] );
                if ( address == 0 )
                    continue;

                DWORD64 displacement = 0;
                if ( SymFromAddr( process, address, &displacement, pSymbol ) )
                {
                    DWORD           displacementLine = 0;
                    IMAGEHLP_LINE64 lineInfo         = {};
                    lineInfo.SizeOfStruct            = sizeof( IMAGEHLP_LINE64 );

                    if ( SymGetLineFromAddr64( process, address, &displacementLine, &lineInfo ) )
                    {
                        sb.appendFormat( "  [%#] %# (%#:%#)\n", frameIndex, pSymbol->Name, lineInfo.FileName, lineInfo.LineNumber );
                    }
                    else
                    {
                        sb.appendFormat( "  [%#] %#\n", frameIndex, pSymbol->Name );
                    }
                }
                else
                {
                    sb.appendFormat( "  [%#] 0x%#\n", frameIndex, Fmt( address, Format().hex() ) );
                }
            }

            return string( sb.view() );
        }
    } // namespace

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
