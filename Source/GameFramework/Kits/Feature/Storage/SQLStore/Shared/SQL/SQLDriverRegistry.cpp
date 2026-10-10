#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQL/SQLDriverRegistry.h"

#include "Core/Common/BuildInfo.h"
#include "Core/Concurrency/mutex.h"
#include "Core/String/StringBuilder.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/Driver/SQLite/SQLiteDriver.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQL/SQLDriver.h"

namespace sw
{
    namespace
    {
        struct SQLDriverRegistryInternal
        {
            struct State
            {
                mutable mutex       _mutex{};
                vector<ISQLDriver*> _listDriver{};
                bool                _bBuiltInRegistered{ false };
            };

            /** @brief 등록 표 — 처음 부를 때 이 키트의 드라이버(SQLite)를 올린다. 잠근 채로 돌려주지 않는다(부르는 쪽이 잠근다). */
            static State& getState()
            {
                static State s_state;
                return s_state;
            }

            /** @brief 잠금 안에서 — 이 키트의 드라이버를 한 번 올립니다. */
            static void registerBuiltInDrivers( State& state )
            {
                if ( state._bBuiltInRegistered )
                    return;
                state._bBuiltInRegistered = true;
                state._listDriver.push_back( &SQLiteDriver::getInstance() );
            }

            static ISQLDriver* findLocked( const State& state, string_view driverName )
            {
                for ( ISQLDriver* pDriver : state._listDriver )
                {
                    if ( driverName == pDriver->getName() )
                        return pDriver;
                }
                return nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void SQLDriverRegistry::registerDriver( ISQLDriver* pDriver )
    {
        if ( pDriver == nullptr )
            return;
        SQLDriverRegistryInternal::State& state = SQLDriverRegistryInternal::getState();
        std::scoped_lock<mutex>           lock{ state._mutex };
        SQLDriverRegistryInternal::registerBuiltInDrivers( state );
        ISQLDriver* pExisting = SQLDriverRegistryInternal::findLocked( state, pDriver->getName() );
        if ( pExisting == pDriver )
            return;
        if ( pExisting != nullptr )
        {
            SW_LOG_ERROR( "SQL driver '%#' is already registered — the second one is ignored", pDriver->getName() );
            return;
        }
        state._listDriver.push_back( pDriver );
    }

    void SQLDriverRegistry::unregisterDriver( ISQLDriver* pDriver )
    {
        SQLDriverRegistryInternal::State& state = SQLDriverRegistryInternal::getState();
        std::scoped_lock<mutex>           lock{ state._mutex };
        for ( size_t driverIndex = 0; driverIndex < state._listDriver.size(); ++driverIndex )
        {
            if ( state._listDriver[driverIndex] != pDriver )
                continue;
            state._listDriver.erase( state._listDriver.begin() + static_cast<ptrdiff_t>( driverIndex ) );
            return;
        }
    }

    ISQLDriver* SQLDriverRegistry::findDriver( string_view driverName, string& outError )
    {
        SQLDriverRegistryInternal::State& state = SQLDriverRegistryInternal::getState();
        std::scoped_lock<mutex>           lock{ state._mutex };
        SQLDriverRegistryInternal::registerBuiltInDrivers( state );
        ISQLDriver* pDriver = SQLDriverRegistryInternal::findLocked( state, driverName );
        if ( pDriver != nullptr )
            return pDriver;
        string available;
        for ( const ISQLDriver* pRegistered : state._listDriver )
        {
            if ( available.empty() == false )
                available += ", ";
            available += pRegistered->getName();
        }
        StringBuilder<constant::kMaxBuffer512> text;
        text.appendFormat( "no SQL driver '%#' in this build target (%#) - available: %#", driverName, build::kTargetName, available.c_str() );
        outError = string( text.view() );
        return nullptr;
    }

    void SQLDriverRegistry::getDriverNames( vector<string>& outListName )
    {
        SQLDriverRegistryInternal::State& state = SQLDriverRegistryInternal::getState();
        std::scoped_lock<mutex>           lock{ state._mutex };
        SQLDriverRegistryInternal::registerBuiltInDrivers( state );
        for ( const ISQLDriver* pDriver : state._listDriver )
        {
            outListName.push_back( string( pDriver->getName() ) );
        }
    }
} // namespace sw
