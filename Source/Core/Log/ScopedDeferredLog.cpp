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

    bool ScopedDeferredLog::tryDefer( const LogEvent& event )
    {
        if ( t_pDeferredLog == nullptr )
            return false;
        t_pDeferredLog->_listLine.push_back( Line{ event, string( event._pMessage != nullptr ? event._pMessage : "" ) } );
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
            Logger::writeEventGlobal( deferred._event.withMessage( deferred._message.c_str() ) );
        }
        _listLine.clear();
    }
} // namespace sw
