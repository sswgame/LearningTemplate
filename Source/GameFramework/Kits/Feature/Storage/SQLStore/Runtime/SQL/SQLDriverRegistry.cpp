#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLDriverRegistry.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/Registry/RegistrationList.h"
#include "Core/String/StringBuilder.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLDriver.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 드라이버 등록 표입니다(정적 등록 공통 틀 — 잠금을 든 함수 정적 목록). 이름은 올릴 때 복사한다 — 이미지가 내려간 뒤에도 안내 문구가 죽은 드라이버를 묻지 않는다.
         */
        SW_DEFINE_LOCKED_REGISTRATION_LIST( getDrivers, ISQLDriver, "SQLDriverRegistry" )

        struct SQLDriverRegistryInternal
        {
            /** @brief 올라 있는 드라이버 이름을 @p outListName 뒤에 담습니다(잠금 안에서 부른다). */
            static void appendDriverNames( const RegistrationList<ISQLDriver>& list, vector<string>& outListName )
            {
                for ( uint32 driverIndex = 0; driverIndex < list.getCount(); ++driverIndex )
                {
                    outListName.push_back( list.getNameAt( driverIndex ) );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void SQLDriverRegistry::registerDriver( ISQLDriver& driver )
    {
        getDrivers().withLock( [&driver]( RegistrationList<ISQLDriver>& list )
        {
            [[maybe_unused]] const RegistrationResult result = list.addLogged( &driver, "SQL driver", driver.getName() );
            SW_ASSERT( result == RegistrationResult::Added );
        } );
    }

    void SQLDriverRegistry::unregisterDriver( ISQLDriver& driver )
    {
        getDrivers().withLock( [&driver]( RegistrationList<ISQLDriver>& list )
        {
            (void)list.remove( &driver ); // 거절된 등록이면 올라 있지 않다
        } );
    }

    expected<ISQLDriver*> SQLDriverRegistry::findDriver( string_view driverName )
    {
        vector<string>    listAvailable;
        ISQLDriver* const pDriver = getDrivers().withLock( [driverName, &listAvailable]( const RegistrationList<ISQLDriver>& list ) -> ISQLDriver*
        {
            ISQLDriver* const pFound = list.findByName( driverName );
            if ( pFound == nullptr )
                SQLDriverRegistryInternal::appendDriverNames( list, listAvailable );
            return pFound;
        } );
        if ( pDriver != nullptr )
            return pDriver;
        string available;
        for ( const string& name : listAvailable )
        {
            if ( available.empty() == false )
                available += ", ";
            available += name;
        }
        StringBuilder<constant::buffer::kMax256> detailBuilder;
        detailBuilder.appendFormat( "%# (target %#, available: %#)", driverName, build::kTargetName, available.c_str() );
        return unexpected( Error{ ErrorKind::NotFound, "no such SQL driver in this build target", detailBuilder.view() } );
    }

    void SQLDriverRegistry::getDriverNames( vector<string>& outListName )
    {
        getDrivers().withLock( [&outListName]( const RegistrationList<ISQLDriver>& list )
        { SQLDriverRegistryInternal::appendDriverNames( list, outListName ); } );
    }
} // namespace sw
