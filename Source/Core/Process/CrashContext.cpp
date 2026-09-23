#include "pch.h"

#include "Core/Process/CrashContext.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Process/CallStackCapture.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include <chrono>
#include <cstdio>
#include <random>

#if !defined( SW_PLATFORM_WINDOWS )
    #include <fcntl.h>
    #include <unistd.h>
#endif

SW_LOG_CALLER( "CrashHandler" );
namespace sw
{
    namespace
    {
        /// @brief 덤프 · 리포트를 쓸 폴더입니다. 부팅 때 한 번 정하고 크래시 경로에서는 읽기만 합니다.
        fixed_string<constant::kMaxBuffer1024> s_reportFolder{};

        /**
         * @brief 세션 ID 를 한 번 만듭니다. 시각과 난수를 섞은 16진수 문자열입니다.
         * @details GUID API 를 쓰지 않는 이유는 플랫폼마다 헤더가 다르고, 여기서 필요한 것은 "이 실행을 다른 실행과 구분하는 것"
         *          뿐이기 때문입니다. 로그 파일 이름에도 같은 값이 들어갑니다.
         */
        const utf8* makeSessionId()
        {
            static utf8 s_arrSession[24]{};
            if ( s_arrSession[0] != '\0' )
                return s_arrSession;

            const uint64 nowTicks =
                static_cast<uint64>( std::chrono::steady_clock::now().time_since_epoch().count() );
            const uint64 wallSeconds =
                static_cast<uint64>( std::chrono::system_clock::now().time_since_epoch().count() );
            std::random_device randomDevice;
            const uint64       mixed = nowTicks ^ ( wallSeconds << 16 ) ^ ( static_cast<uint64>( randomDevice() ) << 32 );

            static const utf8 kHex[] = "0123456789abcdef";
            for ( uint32 digit = 0; digit < 16; ++digit )
                s_arrSession[digit] = kHex[( mixed >> ( digit * 4 ) ) & 0xFu];
            s_arrSession[16] = '\0';
            return s_arrSession;
        }
        /**
         * @brief 버퍼 뒤에 포맷한 한 줄을 이어 붙입니다. **할당하지 않습니다.**
         * @details formatstring 은 버퍼의 **처음부터** 쓰므로, 이어 붙이려면 남은 자리를 직접 넘겨야 합니다.
         * @param inOutLength 현재 길이. 쓴 만큼 늘려서 돌려줍니다.
         */
        template <typename... Args>
        void appendLine( utf8* pBuffer, uint32 capacity, uint32& inOutLength, string_view format, Args&&... args )
        {
            if ( inOutLength + 1 >= capacity )
                return;
            formatstring( pBuffer + inOutLength, capacity - inOutLength, format, std::forward<Args>( args )... );
            inOutLength += static_cast<uint32>( StringUtil::strlen( pBuffer + inOutLength ) );
        }

        /**
         * @brief 파일 하나를 통째로 씁니다. **stdio 를 쓰지 않습니다.**
         * @details fopen/fprintf 는 스트림 락을 잡고 내부 버퍼를 할당합니다. 크래시 지점에서 힙이 깨져 있으면 거기서 다시
         *          죽습니다. OS 원시 호출에는 그런 것이 없습니다(POSIX 에서는 async-signal-safe 이기도 합니다). 할당 없이 먼저
         *          쓰겠다는 이 경로의 취지가 바로 이것입니다.
         *
         * @note **FileUtil::writeFile 을 쓰면 안 됩니다.** normalizeSeparators 가 sw::string 을 만들어 힙을 쓰고, 내부도
         *       fopen/fwrite 입니다. 크래시 경로에서 피해야 할 두 가지를 모두 합니다. FileUtil 은 부팅 때(로그 폴더 준비 등)
         *       쓰는 것이 맞고, 여기서는 아닙니다.
         */
        void writeWholeFile( const utf8* pPath, const utf8* pText, uint32 length )
        {
            if ( pPath == nullptr || pText == nullptr || length == 0 )
                return;
#if defined( SW_PLATFORM_WINDOWS )
            const HANDLE hFile = CreateFileA( pPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
            if ( hFile == INVALID_HANDLE_VALUE )
                return;
            DWORD written{ 0 };
            WriteFile( hFile, pText, static_cast<DWORD>( length ), &written, nullptr );
            CloseHandle( hFile );
#else
            const int32 fileDesc = ::open( pPath, O_WRONLY | O_CREAT | O_TRUNC, 0644 );
            if ( fileDesc < 0 )
                return;
            ssize_t     remaining = static_cast<ssize_t>( length );
            const utf8* pCursor   = pText;
            while ( remaining > 0 )
            {
                const ssize_t written = ::write( fileDesc, pCursor, static_cast<size_t>( remaining ) );
                if ( written <= 0 )
                    break;
                pCursor += written;
                remaining -= written;
            }
            ::close( fileDesc );
#endif
        }
    } // namespace

    CrashContextStore& CrashContextStore::get()
    {
        static CrashContextStore s_store{};
        return s_store;
    }

    void CrashContextStore::set( string_view key, string_view value )
    {
        if ( key.empty() )
            return;

        for ( uint32 entryIndex = 0; entryIndex < _entryCount; ++entryIndex )
        {
            if ( _arrEntry[entryIndex]._key.view() == key )
            {
                _arrEntry[entryIndex]._value = value;
                return;
            }
        }
        // 자리가 없으면 조용히 버린다. 크래시 진단을 돕자고 넣은 것이 실패를 키우면 안 된다.
        if ( _entryCount >= CrashHandler::kMaxContextEntry )
            return;

        _arrEntry[_entryCount]._key   = key;
        _arrEntry[_entryCount]._value = value;
        ++_entryCount;
    }

    const utf8* getCrashSessionId()
    {
        return makeSessionId();
    }

    const utf8* getCrashReportFolder()
    {
        return s_reportFolder.c_str();
    }

    void setCrashReportFolder( string_view folderPath )
    {
        s_reportFolder = folderPath;
    }

    void buildCrashReportPath( utf8* pOutPath, uint32 outSize, const utf8* pExtension )
    {
        if ( pOutPath == nullptr || outSize == 0 )
            return;
        const utf8* pFolder = getCrashReportFolder();
        const utf8* pPrefix = ( pFolder != nullptr && pFolder[0] != '\0' ) ? pFolder : ".";
        std::snprintf( pOutPath, outSize, "%s/crash_%s.%s", pPrefix, getCrashSessionId(),
                       ( pExtension != nullptr ) ? pExtension : "txt" );
    }

    void writeCrashContextFile( const utf8* pReason, const void* pFaultAddress, uint64 processId, uint64 threadId )
    {
        utf8 arrPath[constant::kMaxBuffer1024]{};
        buildCrashReportPath( arrPath, constant::kMaxBuffer1024, "txt" );

        // 버퍼 하나에 모두 만든 뒤 **한 번에** 쓴다. 줄마다 fprintf 를 부르면 그때마다 스트림 락과 내부 버퍼가 걸리고,
        // 도중에 죽으면 반쯤 쓰인 파일이 남는다.
        utf8   arrReport[constant::kMaxBuffer8192]{};
        uint32 length{ 0 };
        appendLine( arrReport, constant::kMaxBuffer8192, length, "session   : %#\n", getCrashSessionId() );
        appendLine( arrReport, constant::kMaxBuffer8192, length, "reason    : %#\n", ( pReason != nullptr ) ? pReason : "unknown" );
        // 주소는 16진수여야 맵 파일 · 디스어셈블리와 맞춰 볼 수 있다(CallStackCapture 도 같은 형식이다).
        appendLine( arrReport, constant::kMaxBuffer8192, length, "address   : 0x%#\n",
                    Fmt( reinterpret_cast<uint64>( pFaultAddress ), Format().hex() ) );
        appendLine( arrReport, constant::kMaxBuffer8192, length, "processId : %#\n", processId );
        appendLine( arrReport, constant::kMaxBuffer8192, length, "threadId  : %#\n", threadId );

        const CrashContextStore& store = CrashContextStore::get();
        for ( uint32 entryIndex = 0; entryIndex < store._entryCount; ++entryIndex )
        {
            // 키 폭을 맞춘다. 위의 고정 항목과 세로줄이 맞아야 읽기 쉽다.
            appendLine( arrReport, constant::kMaxBuffer8192, length, "%#: %#\n",
                        Fmt( store._arrEntry[entryIndex]._key.c_str(), Format().width( 9 ).leftAlign() ),
                        store._arrEntry[entryIndex]._value.c_str() );
        }

        writeWholeFile( arrPath, arrReport, length );
    }

    void writeCrashStackFile( const utf8* pStackText )
    {
        if ( pStackText == nullptr )
            return;
        utf8 arrPath[constant::kMaxBuffer1024]{};
        buildCrashReportPath( arrPath, constant::kMaxBuffer1024, "stack.txt" );
        writeWholeFile( arrPath, pStackText, static_cast<uint32>( StringUtil::strlen( pStackText ) ) );
    }

    void writeCrashReport( const utf8* pReason, const void* pFaultAddress, void* pPlatformContext, bool bMiniDumpWritten )
    {
        // 예외 컨텍스트에서 스택을 따라가야 디스패치 프레임(KiUserExceptionDispatcher 등)이 앞을 차지하지 않고
        // 실제 폴트 지점이 [0] 에 온다.
        DeepCallStack stack{};
        CallStackCapture::captureFromContext( stack, pPlatformContext );

        StringBuilder<constant::kMaxBuffer8192> builder;
        builder.append( "\n==================== CRASH ====================\n" );
        builder.append( ( pReason != nullptr ) ? pReason : "unknown fault" );
        if ( pFaultAddress != nullptr )
        {
            builder.append( "\n  at address: " );
            builder.append( reinterpret_cast<uint64>( pFaultAddress ) );
        }
        builder.append( "\n----------------- call stack ------------------\n" );
        builder.append( CallStackCapture::symbolize( stack ).c_str() );

        // 무엇을 보내면 되는지 리포트 안에 적는다. 이 목록이 없으면 파일이 어디에 생겼는지 알 수 없다. 세션 ID 가 이름에
        // 들어 있어 로그와 짝지을 수도 있다.
        builder.append( "-------------- crash report files -------------\n" );
        {
            utf8 arrReportPath[constant::kMaxBuffer1024]{};
            if ( bMiniDumpWritten )
            {
                buildCrashReportPath( arrReportPath, constant::kMaxBuffer1024, "dmp" );
                builder.append( arrReportPath );
                builder.append( "\n" );
            }
            buildCrashReportPath( arrReportPath, constant::kMaxBuffer1024, "txt" );
            builder.append( arrReportPath );
            builder.append( "\n" );
            buildCrashReportPath( arrReportPath, constant::kMaxBuffer1024, "stack.txt" );
            builder.append( arrReportPath );
            builder.append( "\n" );
        }
        builder.append( "===============================================\n" );

        // 로거가 비동기일 수 있으므로 stderr 로도 직접 내보내 크래시 직전의 기록을 확실히 남긴다.
        std::fputs( builder.c_str(), stderr );
        std::fflush( stderr );
        // 배포 환경에서는 아무도 stderr 를 보지 않는다. 파일로도 남겨야 사용자가 보내 줄 수 있다.
        writeCrashStackFile( builder.c_str() );
        SW_LOG_ERROR( "%#", builder.c_str() );
    }

    void CrashHandler::setContextValue( string_view key, string_view value )
    {
        CrashContextStore::get().set( key, value );
    }

    const utf8* CrashHandler::getSessionId()
    {
        return getCrashSessionId();
    }

    void CrashHandler::setReportFolder( string_view folderPath )
    {
        setCrashReportFolder( folderPath );
    }
} // namespace sw
