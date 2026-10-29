#include "pch.h"

#include "Core/Diagnostics/CrashContext.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/formatString.h"
#include "Core/Delegate/SubscriptionList.h"
#include "Core/Diagnostics/CallStackCapture.h"
#include "Core/Foundation/HexDigitUtil.h"
#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Math/SecureRandom.h"
#include "Core/Process/ThreadCrashStack.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/Time/WallClock.h"

#include <chrono>
#include <cstdio>
#include <thread>

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

        /// @brief 크래시 보고에 줄 시한(초)입니다(`CrashHandler::setReportDeadline`).
        constexpr uint32 kDefaultReportDeadlineSeconds = 20;
        atomic<uint32>   s_reportDeadlineSeconds{ kDefaultReportDeadlineSeconds };

        /// @brief 문맥 파일 끝에 글을 붙일 함수와 그 인자입니다(`bindCrashExtraTextWriter`). 크래시 경로는 읽기만 한다.
        atomic<CrashExtraTextWriter> s_pExtraTextWriter{ nullptr };
        atomic<void*>                s_pExtraTextUser{ nullptr };
        /// @brief 지금 걸린 함수의 구독 번호입니다. 풀린 구독이 이 번호일 때만 칸을 비운다.
        atomic<uint64> s_extraTextWriterID{ 0 };
        atomic<uint64> s_nextExtraTextWriterID{ 1 };

        /** @brief 문맥 글 함수 구독이 풀렸습니다. 지금 걸린 것이 그 구독이면 칸을 비웁니다. */
        void releaseCrashExtraTextWriter( uint64 id )
        {
            uint64 expected = id;
            if ( s_extraTextWriterID.compare_exchange_strong( expected, 0 ) )
            {
                s_pExtraTextWriter.store( nullptr );
                s_pExtraTextUser.store( nullptr );
            }
        }

        /** @brief 문맥 글 함수의 구독 목록입니다(프로세스에 하나). */
        SubscriptionList& getCrashExtraTextWriterSubscriptions()
        {
            static SubscriptionList s_list{ SubscriptionList::ReleaseFunction::create<&releaseCrashExtraTextWriter>() };
            return s_list;
        }

        /// @brief `CrashTestKind::AssertFailure` 가 거짓으로 만드는 단언 식입니다(컴파일러가 단언을 상수로 접지 못하게 volatile).
        ///        단언이 빠지는 빌드(Release · Shipping)에서는 읽는 곳이 없다.
        [[maybe_unused]] volatile bool s_bAssertProbeHolds{ false };

        /// @brief `holdStderrForeverInternal` 이 stderr 를 쥐었는지입니다.
        atomic<bool> s_bStderrHeld{ false };

        /**
         * @brief stderr 의 스트림 락을 쥐고 영영 놓지 않습니다(`CrashTestKind::StderrHeld`).
         * @details 가득 찬 파이프에 쓰다 막힌 스레드의 흉내다 — 읽는 쪽이 멈추면 그 스레드는 stderr 의 락을 쥔 채 서 있다.
         */
        [[noreturn]] void holdStderrForeverInternal()
        {
#if defined( SW_PLATFORM_WINDOWS )
            _lock_file( stderr );
#else
            flockfile( stderr );
#endif
            s_bStderrHeld.store( true );
            while ( true )
            {
                ThreadUtil::sleepForSeconds( 1 * 3600.0 );
            }
        }

        /// @brief `overflowStackInternal` 의 재귀를 멈추는 척하는 값입니다. 컴파일러가 끝없는 재귀를 없애거나 경고로 막지 못하게 합니다.
        volatile bool s_bStopStackOverflow{ false };

        /** @brief 스택이 넘칠 때까지 재귀합니다(`CrashTestKind::StackOverflow`). 프레임마다 4 KB 를 써서 금방 넘친다. */
        uint32 overflowStackInternal( uint32 depth )
        {
            volatile uint8 arrPad[constant::kMaxBuffer4096];
            arrPad[depth % constant::kMaxBuffer4096] = static_cast<uint8>( depth );
            if ( s_bStopStackOverflow )
                return 0;
            // 재귀 뒤에 값을 더해 꼬리 호출로 바뀌지 않게 한다(꼬리 호출이면 스택이 자라지 않는다).
            return overflowStackInternal( depth + 1 ) + arrPad[0];
        }

        /** @brief 새 스레드의 본체입니다. 스레드별 준비를 거친 뒤 넘친다. */
        void overflowStackOnWorkerInternal()
        {
            ThreadCrashStack::initializeCurrentThread();
            overflowStackInternal( 0 );
        }

        /** @brief 생성자에서 순수 가상 함수를 (한 단계 건너) 부르는 기반 클래스입니다(`CrashTestKind::PureVirtualCall`). */
        struct PureCallProbeBaseInternal
        {
            PureCallProbeBaseInternal() { callThrough(); }
            virtual ~PureCallProbeBaseInternal() = default;
            /** @brief 생성자에서 바로 부르면 컴파일러가 막는다. 한 단계 건너 부른다. */
            void         callThrough() { onProbe(); }
            virtual void onProbe() = 0;
        };

        struct PureCallProbeInternal final : PureCallProbeBaseInternal
        {
            void onProbe() override {}
        };

        /** @brief 세션 ID 글자입니다(16 자리 16진수 + 널). */
        struct SessionIDText
        {
            utf8 _arrText[24]{};
        };

        /** @brief 시각과 난수를 섞은 16진수 세션 ID 를 새로 만듭니다. */
        SessionIDText buildSessionIDInternal()
        {
            const uint64 nowTicks    = static_cast<uint64>( MonotonicClock::nowNanoseconds() );
            const uint64 wallSeconds = static_cast<uint64>( WallClock::nowUnixMilliseconds().count() );
            const uint64 mixed       = nowTicks ^ ( wallSeconds << 16 ) ^ SecureRandom::nextUint64();

            SessionIDText session{};
            HexDigitUtil::writeDigits( session._arrText, mixed, HexDigitUtil::kMaxDigitCount, HexCase::Lower );
            return session;
        }

        /**
         * @brief 세션 ID 를 한 번 만듭니다. 로그 파일 이름과 크래시 파일 이름에 같은 값이 들어갑니다.
         * @details GUID API 를 쓰지 않는 이유는 플랫폼마다 헤더가 다르고, 여기서 필요한 것은 "이 실행을 다른 실행과 구분하는 것"
         *          뿐이기 때문입니다.
         *          주의: 처음 부르는 스레드가 여럿일 수 있다(로그 워커가 파일을 여는 순간과 보고 스레드가 덤프 경로를 만드는 순간).
         *          검사 후 채우는 정적 버퍼로 두면 두 스레드가 다른 값을 섞어 써, 덤프와 컨텍스트 파일이 서로 다른 세션 이름을
         *          가진다. 함수 지역 정적 초기화(스레드 안전)로 한 값만 보이게 하고, 설치 때 미리 불러 크래시 경로에서 만들지 않는다.
         */
        const utf8* makeSessionID()
        {
            static const SessionIDText s_session = buildSessionIDInternal();
            return s_session._arrText;
        }
        /**
         * @brief 버퍼 뒤에 포맷한 한 줄을 이어 붙입니다. **할당하지 않습니다.**
         * @details formatstring 은 버퍼의 **처음부터** 쓰므로, 이어 붙이려면 남은 공간을 직접 넘겨야 합니다.
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
            const bool bNothingToWrite = pPath == nullptr || pText == nullptr || length == 0;
            if ( bNothingToWrite )
                return;
#if defined( SW_PLATFORM_WINDOWS )
            // 경로는 UTF-8 이다 — 힙 없이 스택에서 UTF-16 으로 바꿔 W 판에 넘긴다(StringUtil::utf8ToUTF16 은 할당한다).
            utf16 arrWidePath[constant::kMaxBuffer1024]{};
            if ( MultiByteToWideChar( CP_UTF8, 0, pPath, -1, arrWidePath, static_cast<int32>( constant::kMaxBuffer1024 ) ) == 0 )
                return;
            const HANDLE hFile = CreateFileW( arrWidePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
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
        // 공간이 없으면 조용히 버린다. 크래시 진단을 돕자고 넣은 것이 실패를 키우면 안 된다.
        if ( _entryCount >= CrashHandler::kMaxContextEntry )
            return;

        _arrEntry[_entryCount]._key   = key;
        _arrEntry[_entryCount]._value = value;
        ++_entryCount;
    }

    CrashBreadcrumbStore& CrashBreadcrumbStore::get()
    {
        static CrashBreadcrumbStore s_store{};
        return s_store;
    }

    void CrashBreadcrumbStore::add( string_view text )
    {
        const uint32 slot = _pushedCount.fetch_add( 1 ) % kMaxBreadcrumb;
        _arrEntry[slot]   = text.size() < kMaxLength ? text : text.substr( 0, kMaxLength - 1 );
    }

    uint32 CrashBreadcrumbStore::writeText( utf8* pOutText, uint32 capacity ) const
    {
        if ( pOutText == nullptr || capacity == 0 )
            return 0;
        pOutText[0]         = '\0';
        const uint32 pushed = _pushedCount.load();
        const uint32 count  = pushed < kMaxBreadcrumb ? pushed : kMaxBreadcrumb;
        const uint32 first  = pushed - count;
        uint32       length = 0;
        for ( uint32 index = 0; index < count; ++index )
        {
            appendLine( pOutText, capacity, length, "%#\n", _arrEntry[( first + index ) % kMaxBreadcrumb].c_str() );
        }
        return length;
    }

    const utf8* getCrashSessionID()
    {
        return makeSessionID();
    }

    const utf8* getCrashReportFolder()
    {
        return s_reportFolder.c_str();
    }

    void setCrashReportFolder( string_view folderPath )
    {
        s_reportFolder = folderPath;
    }

    Subscription bindCrashExtraTextWriter( CrashExtraTextWriter pWriter, void* pUser )
    {
        s_pExtraTextWriter.store( nullptr ); // 인자가 바뀌는 동안 크래시 경로가 옛 함수에 새 인자를 넘기지 않게 먼저 푼다
        s_pExtraTextUser.store( pUser );
        s_pExtraTextWriter.store( pWriter );
        const uint64 id = s_nextExtraTextWriterID.fetch_add( 1 );
        s_extraTextWriterID.store( id );
        return getCrashExtraTextWriterSubscriptions().issue( id );
    }

    uint32 getCrashReportDeadlineSeconds()
    {
        return s_reportDeadlineSeconds.load();
    }

    void makeCrashReportPath( utf8* pOutPath, uint32 outSize, const utf8* pExtension )
    {
        if ( pOutPath == nullptr || outSize == 0 )
            return;
        const utf8* pFolder = getCrashReportFolder();
        const utf8* pPrefix = ( pFolder != nullptr && pFolder[0] != '\0' ) ? pFolder : ".";
        formatstring( pOutPath, outSize, "%#/crash_%#.%#", pPrefix, getCrashSessionID(),
                      ( pExtension != nullptr ) ? pExtension : "txt" );
    }

    void writeCrashContextFile( const utf8* pReason, const void* pFaultAddress, uint64 processID, uint64 threadID )
    {
        utf8 arrPath[constant::kMaxBuffer1024]{};
        makeCrashReportPath( arrPath, constant::kMaxBuffer1024, "txt" );

        // 버퍼 하나에 모두 만든 뒤 **한 번에** 쓴다. 줄마다 fprintf 를 부르면 그때마다 스트림 락과 내부 버퍼가 걸리고,
        // 도중에 죽으면 반쯤 쓰인 파일이 남는다.
        utf8   arrReport[constant::kMaxBuffer8192]{};
        uint32 length{ 0 };
        appendLine( arrReport, constant::kMaxBuffer8192, length, "session   : %#\n", getCrashSessionID() );
        appendLine( arrReport, constant::kMaxBuffer8192, length, "reason    : %#\n", ( pReason != nullptr ) ? pReason : "unknown" );
        // 주소는 16진수여야 맵 파일 · 디스어셈블리와 맞춰 볼 수 있다(CallStackCapture 도 같은 형식이다).
        appendLine( arrReport, constant::kMaxBuffer8192, length, "address   : 0x%#\n",
                    Fmt( reinterpret_cast<uint64>( pFaultAddress ), Format().hex() ) );
        appendLine( arrReport, constant::kMaxBuffer8192, length, "processId : %#\n", processID );
        appendLine( arrReport, constant::kMaxBuffer8192, length, "threadId  : %#\n", threadID );

        const CrashContextStore& store = CrashContextStore::get();
        for ( uint32 entryIndex = 0; entryIndex < store._entryCount; ++entryIndex )
        {
            // 키 폭을 맞춘다. 위의 고정 항목과 세로줄이 맞아야 읽기 쉽다.
            appendLine( arrReport, constant::kMaxBuffer8192, length, "%#: %#\n",
                        Fmt( store._arrEntry[entryIndex]._key.c_str(), Format().width( 9 ).leftAlign() ),
                        store._arrEntry[entryIndex]._value.c_str() );
        }

        // 소유자가 건 글(서버의 최근 틱 이력 등)을 같은 파일 끝에 싣는다 — 새 파일을 쓰지 않는다.
        CrashExtraTextWriter pExtraWriter = s_pExtraTextWriter.load();
        if ( pExtraWriter != nullptr && length + 1 < constant::kMaxBuffer8192 )
        {
            const uint32 extraLength = pExtraWriter( s_pExtraTextUser.load(), arrReport + length, constant::kMaxBuffer8192 - length );
            length += extraLength < constant::kMaxBuffer8192 - length ? extraLength : 0;
        }

        writeWholeFile( arrPath, arrReport, length );
        // 빵부스러기도 할당 없는 단계에서 쓴다 — 컨텍스트 파일과 같은 단계(심볼 변환 · 스택 보고보다 먼저).
        writeCrashBreadcrumbFile();
    }

    void writeCrashBreadcrumbFile()
    {
        utf8         arrText[CrashBreadcrumbStore::kMaxBreadcrumb * CrashBreadcrumbStore::kMaxLength]{};
        const uint32 length = CrashBreadcrumbStore::get().writeText( arrText, static_cast<uint32>( sizeof( arrText ) ) );
        if ( length == 0 )
            return;
        utf8 arrPath[constant::kMaxBuffer1024]{};
        makeCrashReportPath( arrPath, constant::kMaxBuffer1024, "breadcrumbs.txt" );
        writeWholeFile( arrPath, arrText, length );
    }

    void writeFileWithoutAllocation( const utf8* pPath, const utf8* pText, uint32 length )
    {
        writeWholeFile( pPath, pText, length );
    }

    void writeCrashStackFile( const utf8* pStackText )
    {
        if ( pStackText == nullptr )
            return;
        utf8 arrPath[constant::kMaxBuffer1024]{};
        makeCrashReportPath( arrPath, constant::kMaxBuffer1024, "stack.txt" );
        writeWholeFile( arrPath, pStackText, static_cast<uint32>( StringUtil::strlen( pStackText ) ) );
    }

    void writeCrashReport( const utf8* pReason, const void* pFaultAddress, void* pPlatformContext, bool bMiniDumpWritten, void* pPlatformThread )
    {
        // 예외 컨텍스트에서 스택을 따라가야 디스패치 프레임(KiUserExceptionDispatcher 등)이 앞을 차지하지 않고
        // 실제 폴트 지점이 [0] 에 온다.
        DeepCallStack stack{};
        CallStackCapture::captureFromContext( stack, pPlatformContext, pPlatformThread );

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
                makeCrashReportPath( arrReportPath, constant::kMaxBuffer1024, "dmp" );
                builder.append( arrReportPath );
                builder.append( "\n" );
            }
            makeCrashReportPath( arrReportPath, constant::kMaxBuffer1024, "txt" );
            builder.append( arrReportPath );
            builder.append( "\n" );
            makeCrashReportPath( arrReportPath, constant::kMaxBuffer1024, "stack.txt" );
            builder.append( arrReportPath );
            builder.append( "\n" );
        }
        builder.append( "===============================================\n" );

        // 배포 환경에서는 아무도 stderr 를 보지 않는다. 파일로 남겨야 사용자가 보내 줄 수 있다 — 그래서 **파일이 먼저다.** stderr 는
        // 막힐 수 있다(읽는 쪽이 멈춘 파이프에 쓰다 그 락을 쥔 채 선 스레드 — CrashTestKind::StderrHeld). 순서가 반대면 거기서
        // 막혔을 때 스택 파일이 없다.
        writeCrashStackFile( builder.c_str() );
        // 로거가 비동기일 수 있으므로 stderr 로도 직접 내보내 크래시 직전의 기록을 확실히 남긴다.
        std::fputs( builder.c_str(), stderr );
        std::fflush( stderr );
        SW_LOG_ERROR( "%#", builder.c_str() );
        // 로그는 작업 스레드가 비동기로 쓴다. 이대로 프로세스가 끝나면 큐에 남은 줄(크래시 직전의 경고 · 방금 쓴 리포트)과 파일 버퍼가 사라진다.
        // 언리얼이 크래시 때 GLog->Panic 으로 동기 비우기를 하는 것과 같은 역할이다.
        Logger::flushGlobalForCrash();
    }

    void CrashHandler::crashForTest( CrashTestKind kind )
    {
        switch ( kind )
        {
            case CrashTestKind::AccessViolation:
            {
                SW_LOG_ERROR( "[CrashTest] writing through a null pointer" );
                volatile int32* pNull = nullptr;
                *pNull                = 1;
                break;
            }
            case CrashTestKind::StackOverflow:
            {
                SW_LOG_ERROR( "[CrashTest] overflowing the stack of this thread" );
                overflowStackInternal( 0 );
                break;
            }
            case CrashTestKind::WorkerStackOverflow:
            {
                SW_LOG_ERROR( "[CrashTest] overflowing the stack of a new thread" );
                std::thread worker( &overflowStackOnWorkerInternal );
                worker.join();
                break;
            }
            case CrashTestKind::Abort:
            {
                SW_LOG_ERROR( "[CrashTest] calling std::abort()" );
                std::abort();
            }
            case CrashTestKind::PureVirtualCall:
            {
                SW_LOG_ERROR( "[CrashTest] calling a pure virtual function during construction" );
                PureCallProbeInternal probe;
                break;
            }
            case CrashTestKind::AssertFailure:
            {
                SW_LOG_ERROR( "[CrashTest] failing SW_ASSERT" );
                SW_ASSERT( s_bAssertProbeHolds );
                break;
            }
            case CrashTestKind::StderrHeld:
            {
                SW_LOG_ERROR( "[CrashTest] writing through a null pointer while another thread holds stderr" );
                std::thread holder( &holdStderrForeverInternal );
                holder.detach();
                while ( s_bStderrHeld.load() == false )
                {
                    ThreadUtil::yield();
                }
                volatile int32* pNull = nullptr;
                *pNull                = 1;
                break;
            }
            case CrashTestKind::None:
            case CrashTestKind::Count:
            {
                break;
            }
        }
    }

    void CrashHandler::setReportDeadline( uint32 seconds )
    {
        s_reportDeadlineSeconds.store( ( seconds != 0 ) ? seconds : kDefaultReportDeadlineSeconds );
    }

    void CrashHandler::setContextValue( string_view key, string_view value )
    {
        CrashContextStore::get().set( key, value );
    }

    void CrashHandler::addBreadcrumb( string_view text )
    {
        CrashBreadcrumbStore::get().add( text );
    }

    const utf8* CrashHandler::getSessionID()
    {
        return getCrashSessionID();
    }
} // namespace sw
