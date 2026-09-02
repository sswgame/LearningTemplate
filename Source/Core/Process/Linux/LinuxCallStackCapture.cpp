#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Math/MathUtil.h"
#include "Core/Process/CallStackCapture.h"
#include "Core/String/StringBuilder.h"

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Common/PlatformOsHeaders.h"

namespace sw
{
    static atomic<int32> s_initRefCount{ 0 };
    static mutex         s_symbolMutex{};

    void CallStackCapture::initialize()
    {
        if ( s_initRefCount.fetch_add( 1, std::memory_order_acq_rel ) != 0 )
            return;

        // Linux / POSIX: 추가 심볼 초기화 불필요
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
        (void)pPlatformContext;
        CallStack shallow{};
        capture( shallow, 1 );
        outStack._frameCount = shallow._frameCount;
        for ( uint32 frameIndex = 0; frameIndex < shallow._frameCount; ++frameIndex )
            outStack._arrFrame[frameIndex] = shallow._arrFrame[frameIndex];
    }

    namespace
    {
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
