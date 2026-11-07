#include "pch.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Service/ServiceServerCallTableBase.h"

#include "Core/Time/MonotonicClock.h"

namespace sw
{
    ServiceServerCallTableBase::ServiceServerCallTableBase()
        : _metrics{}
        , _serviceName{}
        , _callIDAllocator{}
    {
    }

    void ServiceServerCallTableBase::warnUnknownCall( uint64 callID ) const
    {
        SW_LOG_WARNING_ID( "ServiceServerCallTable.UnknownCall", "Service '%#' completed an unknown call %#", _serviceName.c_str(), callID );
    }

    void ServiceServerCallTableBase::reportAbandoned( size_t callCount ) const
    {
        SW_LOG_ERROR_ID( "ServiceServerCallTable.AbandonedCalls", "Service '%#' shut down with %# calls still pending", _serviceName.c_str(), callCount );
    }

    int64 ServiceServerCallTableBase::nowNanoseconds()
    {
        return MonotonicClock::nowNanoseconds(); // 예외 — 지연 지표는 실제 처리 시간을 잰다(가짜 시각으로 재면 뜻이 없다, 프로파일러와 같은 자리)
    }
} // namespace sw
