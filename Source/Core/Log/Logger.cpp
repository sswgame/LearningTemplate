#include "pch.h"

#include "Core/Log/Logger.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    namespace
    {
        /** @brief 프로세스 전역 활성 로그 싱크 포인터입니다. */
        atomic<ILogSink*> s_globalSink{ nullptr };

        /**
         * @brief 호출자 표의 키 — 경로의 **마지막 두 조각**(상위 폴더/파일 이름)의 해시입니다. 구분자는 슬래시와 역슬래시를 같게 봅니다.
         * @details 파일 이름만 쓰면 다른 폴더의 같은 이름 파일(기반 폴더의 `X.cpp` 와 어느 키트의 `X.cpp`)이
         *          같은 키라, 나중에 등록된 이름이 두 파일의 로그에 모두 붙는다. 전체 경로는 쓰지 않는다 — 같은 헤더라도 TU 마다 `__FILE__` 의
         *          앞부분(절대 · 상대 · 구분자)이 다를 수 있다.
         */
        uint64 computeCallerKey( string_view filePath )
        {
            size_t cut            = filePath.size();
            int32  separatorCount = 0;
            while ( cut > 0 )
            {
                const utf8 character = filePath[cut - 1];
                if ( ( character == '/' || character == '\\' ) && ++separatorCount == 2 )
                    break;
                --cut;
            }
            utf8         arrSuffix[constant::kMaxBuffer256];
            const size_t length = MathUtil::min( filePath.size() - cut, sizeof( arrSuffix ) );
            for ( size_t index = 0; index < length; ++index )
            {
                const utf8 character = filePath[cut + index];
                arrSuffix[index]     = character == '\\' ? '/' : character;
            }
            return StringUtil::computeHash64( string_view{ arrSuffix, length } );
        }

        /** @brief 경로 키(`computeCallerKey`)별 로그 Caller 이름 항목입니다(힙 할당 없음). */
        struct CallerEntry
        {
            uint64 _fileHash{ 0 };
            utf8   _arrCallerName[constant::kMaxBuffer64]{ 0 };
        };

        static constexpr size_t kMaxCallerEntries = 512;
        CallerEntry             s_arrCallerEntry[kMaxCallerEntries]{};
        size_t                  s_callerEntryCount{ 0 };
        mutex                   s_callerMutex{};

        /// @brief 런타임 상세도입니다. 기본값은 Info 이고, 배포본은 컴파일 상한(Warning)이 더 낮아 자동으로 잘립니다.
        atomic<int32> s_runtimeVerbosity{ static_cast<int32>( LogLevel::Info ) };
    } // namespace
} // namespace sw

namespace sw
{
    void Logger::flushGlobalForCrash()
    {
        ILogSink* pSink = s_globalSink.load( std::memory_order_acquire );
        if ( pSink != nullptr )
            pSink->flushForCrash();
    }

    void Logger::registerCaller( string_view filePath, string_view callerName ) noexcept
    {
        const uint64            fileHash = computeCallerKey( filePath );
        std::scoped_lock<mutex> lock{ s_callerMutex };

        for ( size_t index = 0; index < s_callerEntryCount; ++index )
        {
            if ( s_arrCallerEntry[index]._fileHash == fileHash )
            {
                const size_t copyLen = MathUtil::min( callerName.size(), sizeof( s_arrCallerEntry[index]._arrCallerName ) - 1 );
                Memory::copy( s_arrCallerEntry[index]._arrCallerName, callerName.data(), copyLen );
                s_arrCallerEntry[index]._arrCallerName[copyLen] = '\0';
                return;
            }
        }

        if ( s_callerEntryCount < kMaxCallerEntries )
        {
            s_arrCallerEntry[s_callerEntryCount]._fileHash = fileHash;
            const size_t copyLen                           = MathUtil::min( callerName.size(), sizeof( s_arrCallerEntry[s_callerEntryCount]._arrCallerName ) - 1 );
            Memory::copy( s_arrCallerEntry[s_callerEntryCount]._arrCallerName, callerName.data(), copyLen );
            s_arrCallerEntry[s_callerEntryCount]._arrCallerName[copyLen] = '\0';
            ++s_callerEntryCount;
        }
    }

    const utf8* Logger::getCaller( const utf8* pFile )
    {
        if ( StringUtil::isNullOrEmpty( pFile ) )
            return nullptr;
        const uint64            fileHash = computeCallerKey( string_view{ pFile } );
        std::scoped_lock<mutex> lock{ s_callerMutex };
        for ( size_t index = 0; index < s_callerEntryCount; ++index )
        {
            if ( s_arrCallerEntry[index]._fileHash == fileHash )
                return s_arrCallerEntry[index]._arrCallerName;
        }
        return nullptr;
    }

    void Logger::setRuntimeVerbosity( LogLevel level )
    {
        s_runtimeVerbosity.store( static_cast<int32>( level ), std::memory_order_relaxed );
    }

    LogLevel Logger::getRuntimeVerbosity()
    {
        return static_cast<LogLevel>( s_runtimeVerbosity.load( std::memory_order_relaxed ) );
    }

    void Logger::setGlobalSink( ILogSink* pSink )
    {
        s_globalSink.store( pSink, std::memory_order_release );
    }

    ILogSink* Logger::getGlobalSink()
    {
        return s_globalSink.load( std::memory_order_acquire );
    }

    void Logger::writeLogGlobal( LogLevel level, const utf8* pTag, const utf8* pCaller, const utf8* pMessage, const utf8* pFile, int32 line )
    {
        ILogSink* pSink = s_globalSink.load( std::memory_order_acquire );
        if ( pSink == nullptr )
            return;

        pSink->writeLog( level, pTag, pCaller, pMessage, pFile, line );
    }

    DelegateHandle Logger::addGlobalListener( const LogWrittenDelegate& listener )
    {
        ILogSink* pSink = s_globalSink.load( std::memory_order_acquire );
        if ( pSink == nullptr )
            return {};

        return pSink->addLogWrittenListener( listener );
    }

    void Logger::removeGlobalListener( const DelegateHandle& handle )
    {
        ILogSink* pSink = s_globalSink.load( std::memory_order_acquire );
        if ( pSink == nullptr )
            return;

        pSink->removeLogWrittenListener( handle );
    }

    uint32 Logger::releaseGlobalListenerCodeWithin( const void* pBegin, const void* pEnd )
    {
        ILogSink* pSink = s_globalSink.load( std::memory_order_acquire );
        if ( pSink == nullptr )
            return 0;

        return pSink->releaseListenerCodeWithin( pBegin, pEnd );
    }

    void Logger::registerGlobalSink( ILogSink* pSink )
    {
        ILogSink* pExpected{ nullptr };
        s_globalSink.compare_exchange_strong( pExpected, pSink, std::memory_order_acq_rel, std::memory_order_relaxed );
    }

    void Logger::unregisterGlobalSink( ILogSink* pSink )
    {
        ILogSink* pExpected = pSink;
        s_globalSink.compare_exchange_strong( pExpected, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed );
    }
} // namespace sw
