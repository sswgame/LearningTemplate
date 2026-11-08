#include "pch.h"

#include "Core/Diagnostics/CrashHandler.h"

#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Diagnostics/CallStackCapture.h"
#include "Core/Diagnostics/CrashHandlerProcessHook.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Process/ProcessSession.h"
#include "Core/Process/ThreadCrashStack.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

#include <cstdio>
#include <thread>

namespace sw
{
    namespace
    {
        /**
         * @brief 버퍼 뒤에 포맷한 한 줄을 이어 붙입니다. **할당하지 않습니다.** 문맥 파일과 빵부스러기 글이 함께 쓴다(주인 타입이 없다).
         * @details formatstring 은 버퍼의 **처음부터** 쓰므로, 이어 붙이려면 남은 공간을 직접 넘겨야 합니다.
         * @param inOutLength 현재 길이. 쓴 만큼 늘려서 돌려줍니다.
         */
        template <typename... Args>
        void appendCrashLine( utf8* pBuffer, uint32 capacity, uint32& inOutLength, string_view format, Args&&... args )
        {
            if ( inOutLength + 1 >= capacity )
                return;
            formatstring( pBuffer + inOutLength, capacity - inOutLength, format, std::forward<Args>( args )... );
            inOutLength += static_cast<uint32>( StringUtil::strlen( pBuffer + inOutLength ) );
        }
    } // namespace

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
        if ( _entryCount >= kMaxEntry )
            return;

        _arrEntry[_entryCount]._key   = key;
        _arrEntry[_entryCount]._value = value;
        ++_entryCount;
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
            appendCrashLine( pOutText, capacity, length, "%#\n", _arrEntry[( first + index ) % kMaxBreadcrumb].c_str() );
        }
        return length;
    }

    atomic<CrashHandler*> CrashHandler::_s_pInstalled{ nullptr };

    mutex& CrashHandler::getChainMutex()
    {
        static mutex s_mutex;
        return s_mutex;
    }

    CrashHandler::CrashHandler()
        : _reportFolder{}
        , _contextStore{}
        , _breadcrumbStore{}
        , _reportDeadlineSeconds{ kDefaultReportDeadlineSeconds }
        , _reportProgress{ 0 }
        , _pExtraTextWriter{ nullptr }
        , _pExtraTextUser{ nullptr }
        , _extraTextWriterID{ 0 }
        , _nextExtraTextWriterID{ 1 }
        , _extraTextSubscriptions{ SubscriptionList::ReleaseFunction::create<&CrashHandler::releaseExtraTextWriter>( this ) }
        , _pOuter{ nullptr }
        , _bInstalled{ SW_FALSE }
    {
    }

    CrashHandler::~CrashHandler()
    {
        uninstall();
    }

    void CrashHandler::install()
    {
        ScopedLock<mutex> lock{ getChainMutex() };
        if ( _bInstalled == SW_TRUE )
            return;
        (void)ProcessSession::getID(); // 크래시 경로에서 만들지 않게 지금 만든다(`ProcessSession::getID` 설명)
        _pOuter = _s_pInstalled.load();
        if ( _pOuter == nullptr )
            CrashHandlerProcessHook::install();
        ThreadCrashStack::initializeCurrentThread();
        _s_pInstalled.store( this );
        _bInstalled = SW_TRUE;
    }

    void CrashHandler::uninstall()
    {
        ScopedLock<mutex> lock{ getChainMutex() };
        if ( _bInstalled == SW_FALSE )
            return;
        // 사슬에서 이 처리기를 뺀다. 보통 가장 안쪽(나중에 설치한 것)이지만, 순서가 어긋나도 사슬이 끊기지 않게 어디서든 뺀다.
        CrashHandler* pInner = nullptr;
        for ( CrashHandler* pCurrent = _s_pInstalled.load(); pCurrent != nullptr && pCurrent != this; pCurrent = pCurrent->_pOuter )
        {
            pInner = pCurrent;
        }
        if ( pInner == nullptr )
            _s_pInstalled.store( _pOuter );
        else
            pInner->_pOuter = _pOuter;
        _pOuter     = nullptr;
        _bInstalled = SW_FALSE;
        if ( _s_pInstalled.load() == nullptr )
            CrashHandlerProcessHook::uninstall();
    }

    CrashHandler* CrashHandlerProcessHook::findInstalled()
    {
        return CrashHandler::_s_pInstalled.load();
    }

    void CrashHandler::setContextValue( string_view key, string_view value )
    {
        _contextStore.set( key, value );
    }

    void CrashHandler::addBreadcrumb( string_view text )
    {
        _breadcrumbStore.add( text );
    }

    Subscription CrashHandler::bindExtraTextWriter( CrashExtraTextWriter pWriter, void* pUser )
    {
        _pExtraTextWriter.store( nullptr ); // 인자가 바뀌는 동안 크래시 경로가 옛 함수에 새 인자를 넘기지 않게 먼저 푼다
        _pExtraTextUser.store( pUser );
        _pExtraTextWriter.store( pWriter );
        const uint64 id = _nextExtraTextWriterID.fetch_add( 1 );
        _extraTextWriterID.store( id );
        return _extraTextSubscriptions.issue( id );
    }

    void CrashHandler::releaseExtraTextWriter( uint64 id )
    {
        uint64 expected = id;
        if ( _extraTextWriterID.compare_exchange_strong( expected, 0 ) )
        {
            _pExtraTextWriter.store( nullptr );
            _pExtraTextUser.store( nullptr );
        }
    }

    void CrashHandler::reportHang( const utf8* pReason )
    {
        // 예외도 폴트 스레드도 없다 — 덤프(있으면)는 모든 스레드를 담고, 디버거가 멈춘 스레드를 연다.
        CrashHandlerProcessHook::writeHangDump( *this );
#if defined( SW_PLATFORM_WINDOWS )
        const uint64 processID = static_cast<uint64>( GetCurrentProcessId() );
#elif defined( SW_PLATFORM_POSIX )
        const uint64 processID = static_cast<uint64>( ::getpid() );
#else
    #error "Unsupported platform"
#endif
        writeContextFile( pReason, nullptr, processID, 0 );
    }

    void CrashHandler::setReportDeadline( uint32 seconds )
    {
        _reportDeadlineSeconds.store( ( seconds != 0 ) ? seconds : kDefaultReportDeadlineSeconds );
    }

    void CrashHandler::makeReportPath( utf8* pOutPath, uint32 outSize, const utf8* pExtension ) const
    {
        if ( pOutPath == nullptr || outSize == 0 )
            return;
        const utf8* pFolder = _reportFolder.c_str();
        const utf8* pPrefix = ( pFolder[0] != '\0' ) ? pFolder : ".";
        formatstring( pOutPath, outSize, "%#/crash_%#.%#", pPrefix, ProcessSession::getID(), ( pExtension != nullptr ) ? pExtension : "txt" );
    }

    void CrashHandler::writeContextFile( const utf8* pReason, const void* pFaultAddress, uint64 processID, uint64 threadID )
    {
        utf8 arrPath[constant::buffer::kMax1024]{};
        makeReportPath( arrPath, constant::buffer::kMax1024, "txt" );

        // 버퍼 하나에 모두 만든 뒤 **한 번에** 쓴다. 줄마다 fprintf 를 부르면 그때마다 스트림 락과 내부 버퍼가 걸리고,
        // 도중에 죽으면 반쯤 쓰인 파일이 남는다.
        utf8   arrReport[constant::buffer::kMax8192]{};
        uint32 length{ 0 };
        appendCrashLine( arrReport, constant::buffer::kMax8192, length, "session   : %#\n", ProcessSession::getID() );
        appendCrashLine( arrReport, constant::buffer::kMax8192, length, "reason    : %#\n", ( pReason != nullptr ) ? pReason : "unknown" );
        // 주소는 16진수여야 맵 파일 · 디스어셈블리와 맞춰 볼 수 있다(CallStackCapture 도 같은 형식이다).
        appendCrashLine( arrReport, constant::buffer::kMax8192, length, "address   : 0x%#\n", Fmt( reinterpret_cast<uint64>( pFaultAddress ), Format().hex() ) );
        appendCrashLine( arrReport, constant::buffer::kMax8192, length, "processId : %#\n", processID );
        appendCrashLine( arrReport, constant::buffer::kMax8192, length, "threadId  : %#\n", threadID );

        for ( uint32 entryIndex = 0; entryIndex < _contextStore._entryCount; ++entryIndex )
        {
            // 키 폭을 맞춘다. 위의 고정 항목과 세로줄이 맞아야 읽기 쉽다.
            appendCrashLine( arrReport, constant::buffer::kMax8192, length, "%#: %#\n", Fmt( _contextStore._arrEntry[entryIndex]._key.c_str(), Format().width( 9 ).leftAlign() ),
                             _contextStore._arrEntry[entryIndex]._value.c_str() );
        }

        // 소유자가 건 글(서버의 최근 틱 이력 등)을 같은 파일 끝에 싣는다 — 새 파일을 쓰지 않는다.
        const CrashExtraTextWriter pExtraWriter = _pExtraTextWriter.load();
        if ( pExtraWriter != nullptr && length + 1 < constant::buffer::kMax8192 )
        {
            const uint32 extraLength = pExtraWriter( _pExtraTextUser.load(), arrReport + length, constant::buffer::kMax8192 - length );
            length += extraLength < constant::buffer::kMax8192 - length ? extraLength : 0;
        }

        (void)FileUtil::writeCrashFile( arrPath, arrReport, length ); // 크래시 경로 - 못 써도 다음 보고를 이어 간다
        recordProgress();
        // 빵부스러기도 할당 없는 단계에서 쓴다 — 컨텍스트 파일과 같은 단계(심볼 변환 · 스택 보고보다 먼저).
        writeBreadcrumbFile();
    }

    void CrashHandler::writeBreadcrumbFile() const
    {
        utf8         arrText[CrashBreadcrumbStore::kMaxBreadcrumb * CrashBreadcrumbStore::kMaxLength]{};
        const uint32 length = _breadcrumbStore.writeText( arrText, static_cast<uint32>( sizeof( arrText ) ) );
        if ( length == 0 )
            return;
        utf8 arrPath[constant::buffer::kMax1024]{};
        makeReportPath( arrPath, constant::buffer::kMax1024, "breadcrumbs.txt" );
        (void)FileUtil::writeCrashFile( arrPath, arrText, length ); // 크래시 경로 - 못 써도 다음 보고를 이어 간다
    }

    void CrashHandler::writeStackFile( const utf8* pStackText ) const
    {
        if ( pStackText == nullptr )
            return;
        utf8 arrPath[constant::buffer::kMax1024]{};
        makeReportPath( arrPath, constant::buffer::kMax1024, "stack.txt" );
        (void)FileUtil::writeCrashFile( arrPath, pStackText, static_cast<uint32>( StringUtil::strlen( pStackText ) ) ); // 크래시 경로 - 못 써도 다음 보고를 이어 간다
    }

    void CrashHandler::writeReport( const utf8* pReason, const void* pFaultAddress, void* pPlatformContext, bool bMiniDumpWritten, void* pPlatformThread )
    {
        // 예외 컨텍스트에서 스택을 따라가야 디스패치 프레임(KiUserExceptionDispatcher 등)이 앞을 차지하지 않고
        // 실제 폴트 지점이 [0] 에 온다.
        DeepCallStack stack{};
        CallStackCapture::captureFromContext( stack, pPlatformContext, pPlatformThread );
        recordProgress();

        StringBuilder<constant::buffer::kMax8192> builder;
        builder.append( "\n==================== CRASH ====================\n" );
        builder.append( ( pReason != nullptr ) ? pReason : "unknown fault" );
        if ( pFaultAddress != nullptr )
        {
            builder.append( "\n  at address: " );
            builder.append( reinterpret_cast<uint64>( pFaultAddress ) );
        }
        builder.append( "\n----------------- call stack ------------------\n" );
        // 기호 파일을 처음 읽는 프레임은 길다 — 프레임마다 진척으로 세어 시한(진척 없는 시간)에 끊기지 않게 한다.
        builder.append( CallStackCapture::symbolize( stack, &_reportProgress ).c_str() );

        // 무엇을 보내면 되는지 리포트 안에 적는다. 이 목록이 없으면 파일이 어디에 생겼는지 알 수 없다. 세션 ID 가 이름에
        // 들어 있어 로그와 짝지을 수도 있다.
        builder.append( "-------------- crash report files -------------\n" );
        {
            utf8 arrReportPath[constant::buffer::kMax1024]{};
            if ( bMiniDumpWritten )
            {
                makeReportPath( arrReportPath, constant::buffer::kMax1024, "dmp" );
                builder.append( arrReportPath );
                builder.append( "\n" );
            }
            makeReportPath( arrReportPath, constant::buffer::kMax1024, "txt" );
            builder.append( arrReportPath );
            builder.append( "\n" );
            makeReportPath( arrReportPath, constant::buffer::kMax1024, "stack.txt" );
            builder.append( arrReportPath );
            builder.append( "\n" );
        }
        builder.append( "===============================================\n" );

        // 배포 환경에서는 아무도 stderr 를 보지 않는다. 파일로 남겨야 사용자가 보내 줄 수 있다 — 그래서 **파일이 먼저다.** stderr 는
        // 막힐 수 있다(읽는 쪽이 멈춘 파이프에 쓰다 그 락을 쥔 채 선 스레드 — CrashInjectionKind::StderrHeld). 순서가 반대면 거기서
        // 막혔을 때 스택 파일이 없다.
        writeStackFile( builder.c_str() );
        recordProgress();
        // 로거가 비동기일 수 있으므로 stderr 로도 직접 내보내 크래시 직전의 기록을 확실히 남긴다.
        std::fputs( builder.c_str(), stderr );
        std::fflush( stderr );
        SW_LOG_ERROR_ID( "Crash.Report", "%#", builder.c_str() );
        // 로그는 작업 스레드가 비동기로 쓴다. 이대로 프로세스가 끝나면 큐에 남은 줄(크래시 직전의 경고 · 방금 쓴 리포트)과 파일 버퍼가 사라진다.
        // 언리얼이 크래시 때 GLog->Panic 으로 동기 비우기를 하는 것과 같은 역할이다.
        Logger::flushGlobalForCrash();
    }

} // namespace sw
