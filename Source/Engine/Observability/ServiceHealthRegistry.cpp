#include "pch.h"

#include "Engine/Observability/ServiceHealthRegistry.h"

namespace sw
{
    namespace
    {
        struct ServiceHealthRegistryInternal
        {
            static constexpr int64 kDefaultLivenessTimeoutMs = 10000;
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( HealthState state )
    {
        switch ( state )
        {
            case HealthState::Ok:
                return "ok";
            case HealthState::Degraded:
                return "degraded";
            case HealthState::Failing:
                return "failing";
        }
        return "unknown";
    }

    ServiceHealthRegistry::ServiceHealthRegistry()
        : _listCheck{}
        , _mutex{}
        , _lastTickMs{ 0 }
        , _livenessTimeoutMs{ ServiceHealthRegistryInternal::kDefaultLivenessTimeoutMs }
        , _bTicked{ SW_FALSE }
        , _bDraining{ SW_FALSE }
    {
    }

    int32 ServiceHealthRegistry::registerCheck( string_view name, bool bRequiredForReady )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        Check&                  check = _listCheck.emplace_back();
        check._name                   = string( name );
        check._bRequiredForReady      = bRequiredForReady ? SW_TRUE : SW_FALSE;
        return static_cast<int32>( _listCheck.size() ) - 1;
    }

    void ServiceHealthRegistry::setCheck( int32 checkIndex, HealthState state, string_view detail )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        if ( checkIndex < 0 || checkIndex >= static_cast<int32>( _listCheck.size() ) )
            return;
        Check& check  = _listCheck[static_cast<size_t>( checkIndex )];
        check._state  = state;
        check._detail = string( detail );
    }

    void ServiceHealthRegistry::markTick( int64 monotonicMs )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _lastTickMs = monotonicMs;
        _bTicked    = SW_TRUE;
    }

    void ServiceHealthRegistry::setDraining( bool bDraining )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _bDraining = bDraining ? SW_TRUE : SW_FALSE;
    }

    void ServiceHealthRegistry::setLivenessTimeoutMs( int64 timeoutMs )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _livenessTimeoutMs = timeoutMs;
    }

    bool ServiceHealthRegistry::isLive( int64 monotonicMs ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return isLiveLocked( monotonicMs );
    }

    bool ServiceHealthRegistry::isReady( int64 monotonicMs ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return isReadyLocked( monotonicMs );
    }

    bool ServiceHealthRegistry::isDraining() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _bDraining == SW_TRUE;
    }

    void ServiceHealthRegistry::writeReport( string& outText, int64 monotonicMs ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        outText += isLiveLocked( monotonicMs ) ? "live 1\n" : "live 0\n";
        outText += isReadyLocked( monotonicMs ) ? "ready 1\n" : "ready 0\n";
        outText += _bDraining == SW_TRUE ? "draining 1\n" : "draining 0\n";
        for ( const Check& check : _listCheck )
        {
            outText += "check ";
            outText += check._name;
            outText.push_back( ' ' );
            outText += toString( check._state );
            if ( check._detail.empty() == false )
            {
                outText.push_back( ' ' );
                outText += check._detail;
            }
            outText.push_back( '\n' );
        }
    }

    bool ServiceHealthRegistry::isLiveLocked( int64 monotonicMs ) const { return _bTicked == SW_TRUE && monotonicMs - _lastTickMs <= _livenessTimeoutMs; }

    bool ServiceHealthRegistry::isReadyLocked( int64 monotonicMs ) const
    {
        if ( isLiveLocked( monotonicMs ) == false || _bDraining == SW_TRUE )
            return false;
        for ( const Check& check : _listCheck )
        {
            if ( check._bRequiredForReady == SW_TRUE && check._state == HealthState::Failing )
                return false;
        }
        return true;
    }
} // namespace sw
