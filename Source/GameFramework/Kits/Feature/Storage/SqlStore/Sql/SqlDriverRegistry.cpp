#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/SqlStore/Sql/SqlDriverRegistry.h"

#include "Core/Common/BuildInfo.h"
#include "Core/Concurrency/mutex.h"
#include "Core/String/StringBuilder.h"

#include "GameFramework/Kits/Feature/Storage/SqlStore/Driver/Sqlite/SqliteDriver.h"
#include "GameFramework/Kits/Feature/Storage/SqlStore/Sql/SqlDriver.h"

namespace sw
{
    namespace
    {
        struct SqlDriverRegistryInternal
        {
            struct State
            {
                mutable mutex       _mutex{};
                vector<ISqlDriver*> _listDriver{};
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
                state._listDriver.push_back( &SqliteDriver::getInstance() );
            }

            static ISqlDriver* findLocked( const State& state, string_view driverName )
            {
                for ( ISqlDriver* pDriver : state._listDriver )
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
    void SqlDriverRegistry::registerDriver( ISqlDriver* pDriver )
    {
        if ( pDriver == nullptr )
            return;
        SqlDriverRegistryInternal::State& state = SqlDriverRegistryInternal::getState();
        std::scoped_lock<mutex>           lock{ state._mutex };
        SqlDriverRegistryInternal::registerBuiltInDrivers( state );
        ISqlDriver* pExisting = SqlDriverRegistryInternal::findLocked( state, pDriver->getName() );
        if ( pExisting == pDriver )
            return;
        if ( pExisting != nullptr )
        {
            SW_LOG_ERROR( "SQL driver '%#' is already registered — the second one is ignored", pDriver->getName() );
            return;
        }
        state._listDriver.push_back( pDriver );
    }

    void SqlDriverRegistry::unregisterDriver( ISqlDriver* pDriver )
    {
        SqlDriverRegistryInternal::State& state = SqlDriverRegistryInternal::getState();
        std::scoped_lock<mutex>           lock{ state._mutex };
        for ( size_t driverIndex = 0; driverIndex < state._listDriver.size(); ++driverIndex )
        {
            if ( state._listDriver[driverIndex] != pDriver )
                continue;
            state._listDriver.erase( state._listDriver.begin() + static_cast<ptrdiff_t>( driverIndex ) );
            return;
        }
    }

    ISqlDriver* SqlDriverRegistry::findDriver( string_view driverName, string& outError )
    {
        SqlDriverRegistryInternal::State& state = SqlDriverRegistryInternal::getState();
        std::scoped_lock<mutex>           lock{ state._mutex };
        SqlDriverRegistryInternal::registerBuiltInDrivers( state );
        ISqlDriver* pDriver = SqlDriverRegistryInternal::findLocked( state, driverName );
        if ( pDriver != nullptr )
            return pDriver;
        string available;
        for ( const ISqlDriver* pRegistered : state._listDriver )
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

    void SqlDriverRegistry::getDriverNames( vector<string>& outListName )
    {
        SqlDriverRegistryInternal::State& state = SqlDriverRegistryInternal::getState();
        std::scoped_lock<mutex>           lock{ state._mutex };
        SqlDriverRegistryInternal::registerBuiltInDrivers( state );
        for ( const ISqlDriver* pDriver : state._listDriver )
        {
            outListName.push_back( string( pDriver->getName() ) );
        }
    }
} // namespace sw
