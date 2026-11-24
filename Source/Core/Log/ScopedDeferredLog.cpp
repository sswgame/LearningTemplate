#include "pch.h"

#include "Core/Log/ScopedDeferredLog.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Log/Logger.h"

namespace sw
{
    namespace
    {
        /** @brief 이 스레드의 로그를 모으는 범위입니다. 없으면 nullptr 입니다. */
        thread_local const ScopedDeferredLog* t_pDeferredLog{ nullptr };
    } // namespace

    ScopedDeferredLog::ScopedDeferredLog()
        : _listLine{}
    {
        if ( t_pDeferredLog == nullptr )
            t_pDeferredLog = this;
    }

    ScopedDeferredLog::~ScopedDeferredLog()
    {
        if ( t_pDeferredLog != this )
            return;
        t_pDeferredLog = nullptr;
        writeLines();
    }

    bool ScopedDeferredLog::tryDefer( LogLevel level, const utf8* pTag, const utf8* pMessage, const utf8* pFile, int32 line, const utf8* pID )
    {
        if ( t_pDeferredLog == nullptr )
            return false;
        t_pDeferredLog->_listLine.push_back( Line{ level, pTag, pFile, pID, line, string( pMessage != nullptr ? pMessage : "" ) } );
        return true;
    }

    void ScopedDeferredLog::flushForCrash()
    {
        const ScopedDeferredLog* const pDeferredLog = t_pDeferredLog;
        if ( pDeferredLog == nullptr )
            return;
        t_pDeferredLog = nullptr;
        const ScopedLeafLockExemption exemption; // 잠금을 쥔 채 죽는 중이다 — 줄을 잃지 않는 쪽이 먼저다
        pDeferredLog->writeLines();
    }

    void ScopedDeferredLog::writeLines() const
    {
        for ( const Line& deferred : _listLine )
        {
            Logger::writeLogGlobal( deferred._level, deferred._pTag, deferred._message.c_str(), deferred._pFile, deferred._line, deferred._pID );
        }
        _listLine.clear();
    }
} // namespace sw
