#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQL/SQLDriverRegistry.h"

#include "Core/Common/BuildInfo.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Module/ModuleUnloadListener.h"
#include "Core/String/RegistrationList.h"
#include "Core/String/StringBuilder.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQL/SQLDriver.h"

namespace sw
{
    namespace
    {
        /**
         * @class SQLDriverTable
         * @brief 드라이버 등록 표입니다. 모듈 언로드 리스너라서, 핫 리로드가 이미지를 내리기 전에 그 이미지의 드라이버(vtable 이 그 이미지에 있는 객체)를 지운다.
         * @details 지우는 것은 정상 경로다 — 드라이버의 정적 등록 객체는 이미지를 실제로 언로드할 때야 소멸하고, 핫 리로드는 그 언로드를 미룬다.
         */
        class SQLDriverTable final : public IModuleUnloadListener
        {
        public:
            static SQLDriverTable& get()
            {
                static SQLDriverTable s_table;
                return s_table;
            }

            void add( ISQLDriver& driver )
            {
                std::scoped_lock<mutex>                   lock{ _mutex };
                [[maybe_unused]] const RegistrationResult result = _registeredDriver.addLogged( &driver, "SQL driver", driver.getName() );
                SW_ASSERT( result == RegistrationResult::Added );
            }

            void remove( ISQLDriver& driver )
            {
                std::scoped_lock<mutex> lock{ _mutex };
                (void)_registeredDriver.remove( &driver ); // 언로드 리스너가 이미 지웠거나 거절된 등록이면 올라 있지 않다
            }

            ISQLDriver* find( string_view driverName, string& outError ) const
            {
                std::scoped_lock<mutex> lock{ _mutex };
                ISQLDriver*             pDriver = _registeredDriver.findByName( driverName );
                if ( pDriver != nullptr )
                    return pDriver;
                string available;
                for ( uint32 driverIndex = 0; driverIndex < _registeredDriver.getCount(); ++driverIndex )
                {
                    if ( available.empty() == false )
                        available += ", ";
                    available += _registeredDriver.getNameAt( driverIndex );
                }
                StringBuilder<constant::kMaxBuffer512> text;
                text.appendFormat( "no SQL driver '%#' in this build target (%#) - available: %#", driverName, build::kTargetName, available.c_str() );
                outError = string( text.view() );
                return nullptr;
            }

            void collectNames( vector<string>& outListName ) const
            {
                std::scoped_lock<mutex> lock{ _mutex };
                for ( uint32 driverIndex = 0; driverIndex < _registeredDriver.getCount(); ++driverIndex )
                {
                    outListName.push_back( _registeredDriver.getNameAt( driverIndex ) );
                }
            }

            const utf8* getModuleUnloadListenerName() const override { return "SQL drivers"; }
            bool        isReleaseExpected() const override { return true; }

            uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override
            {
                (void)outKeepImageMapped;
                std::scoped_lock<mutex> lock{ _mutex };
                uint32                  releasedCount = 0;
                for ( uint32 driverIndex = _registeredDriver.getCount(); driverIndex > 0; --driverIndex )
                {
                    ISQLDriver* pDriver = _registeredDriver.getAt( driverIndex - 1 );
                    if ( isAddressWithin( findVtableAddress( pDriver ), pBegin, pEnd ) == false )
                        continue;
                    (void)_registeredDriver.remove( pDriver ); // 방금 목록에서 읽은 항목이라 올라 있다
                    ++releasedCount;
                }
                return releasedCount;
            }

        private:
            SQLDriverTable()
                : IModuleUnloadListener{}
                , _mutex{}
                , _registeredDriver{ "SQLDriverRegistry" }
            {
            }

            mutable mutex                _mutex;
            RegistrationList<ISQLDriver> _registeredDriver; ///< 이름은 올릴 때 복사한다 — 이미지가 내려간 뒤에도 안내 문구가 죽은 드라이버를 묻지 않는다
        };
    } // namespace
} // namespace sw

namespace sw
{
    void SQLDriverRegistry::registerDriver( ISQLDriver& driver )
    {
        SQLDriverTable::get().add( driver );
    }

    void SQLDriverRegistry::unregisterDriver( ISQLDriver& driver )
    {
        SQLDriverTable::get().remove( driver );
    }

    ISQLDriver* SQLDriverRegistry::findDriver( string_view driverName, string& outError )
    {
        return SQLDriverTable::get().find( driverName, outError );
    }

    void SQLDriverRegistry::getDriverNames( vector<string>& outListName )
    {
        SQLDriverTable::get().collectNames( outListName );
    }
} // namespace sw
