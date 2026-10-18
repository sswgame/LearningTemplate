#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQL/SQLDriverRegistry.h"

#include "Core/Common/BuildInfo.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Module/ModuleUnloadListener.h"
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

            void add( ISQLDriver* pDriver )
            {
                SW_ASSERT( pDriver != nullptr );
                if ( pDriver == nullptr )
                    return;
                std::scoped_lock<mutex> lock{ _mutex };
                const bool              bDuplicate = findLocked( pDriver->getName() ) != nullptr;
                SW_ASSERT( bDuplicate == false );
                if ( bDuplicate )
                {
                    SW_LOG_ERROR( "SQL driver '%#' is already registered by another module - the second one is ignored", pDriver->getName() );
                    return;
                }
                _listDriver.push_back( pDriver );
            }

            void remove( ISQLDriver* pDriver )
            {
                std::scoped_lock<mutex> lock{ _mutex };
                for ( size_t driverIndex = 0; driverIndex < _listDriver.size(); ++driverIndex )
                {
                    if ( _listDriver[driverIndex] != pDriver )
                        continue;
                    _listDriver.erase( _listDriver.begin() + static_cast<ptrdiff_t>( driverIndex ) );
                    return;
                }
            }

            ISQLDriver* find( string_view driverName, string& outError ) const
            {
                std::scoped_lock<mutex> lock{ _mutex };
                ISQLDriver*             pDriver = findLocked( driverName );
                if ( pDriver != nullptr )
                    return pDriver;
                string available;
                for ( const ISQLDriver* pRegistered : _listDriver )
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

            void collectNames( vector<string>& outListName ) const
            {
                std::scoped_lock<mutex> lock{ _mutex };
                for ( const ISQLDriver* pDriver : _listDriver )
                {
                    outListName.push_back( string( pDriver->getName() ) );
                }
            }

            const utf8* getModuleUnloadListenerName() const override { return "SQL drivers"; }
            bool        isReleaseExpected() const override { return true; }

            uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override
            {
                (void)outKeepImageMapped;
                std::scoped_lock<mutex> lock{ _mutex };
                uint32                  releasedCount = 0;
                for ( size_t driverIndex = _listDriver.size(); driverIndex > 0; --driverIndex )
                {
                    if ( isAddressWithin( findVtableAddress( _listDriver[driverIndex - 1] ), pBegin, pEnd ) == false )
                        continue;
                    _listDriver.erase( _listDriver.begin() + static_cast<ptrdiff_t>( driverIndex - 1 ) );
                    ++releasedCount;
                }
                return releasedCount;
            }

        private:
            SQLDriverTable()
                : IModuleUnloadListener{}
                , _mutex{}
                , _listDriver{}
            {
            }

            ISQLDriver* findLocked( string_view driverName ) const
            {
                for ( ISQLDriver* pDriver : _listDriver )
                {
                    if ( driverName == pDriver->getName() )
                        return pDriver;
                }
                return nullptr;
            }

            mutable mutex       _mutex;
            vector<ISQLDriver*> _listDriver;
        };
    } // namespace
} // namespace sw

namespace sw
{
    void SQLDriverRegistry::registerDriver( ISQLDriver* pDriver )
    {
        SQLDriverTable::get().add( pDriver );
    }

    void SQLDriverRegistry::unregisterDriver( ISQLDriver* pDriver )
    {
        SQLDriverTable::get().remove( pDriver );
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
