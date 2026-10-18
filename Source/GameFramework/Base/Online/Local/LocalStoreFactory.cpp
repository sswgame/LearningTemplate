#include "pch.h"

#include "GameFramework/Base/Online/Local/LocalStoreFactory.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/File/UserDataPath.h"
#include "Core/Module/ModuleUnloadListener.h"

#include "GameFramework/Base/Online/Local/FileLocalSlotStorage.h"
#include "GameFramework/Base/Online/Local/MemoryLocalStore.h"
#include "GameFramework/Base/Online/Local/ThreadedLocalStore.h"

namespace sw
{
    namespace
    {
        /**
         * @class LocalStoreBackendTable
         * @brief 키트가 올린 바닥 표입니다. 모듈 언로드 리스너라서, 핫 리로드가 이미지를 내리기 전에 그 이미지의 만들기 함수를 지운다.
         * @details 지우는 것은 정상 경로다 — 키트의 정적 등록 객체는 이미지를 실제로 언로드할 때야 소멸하고, 핫 리로드는 그 언로드를 미룬다.
         */
        class LocalStoreBackendTable final : public IModuleUnloadListener
        {
        public:
            struct Backend
            {
                string                         _name{};
                LocalSlotStorageCreateFunction _createFunction{ nullptr };
            };

            static LocalStoreBackendTable& get()
            {
                static LocalStoreBackendTable s_table;
                return s_table;
            }

            void add( const utf8* pBackendName, LocalSlotStorageCreateFunction createFunction )
            {
                const string_view backendName{ pBackendName };
                const bool        bReserved = backendName == LocalStoreFactory::kFileBackendName || backendName == LocalStoreFactory::kMemoryBackendName;
                SW_ASSERT( bReserved == false && createFunction != nullptr );
                if ( bReserved || createFunction == nullptr )
                {
                    SW_LOG_ERROR( "Local store backend '%#' cannot be registered: the name is built in or the create function is null", pBackendName );
                    return;
                }
                std::scoped_lock<mutex> lock{ _mutex };
                const bool              bDuplicate = findLocked( backendName ) != nullptr;
                SW_ASSERT( bDuplicate == false );
                if ( bDuplicate )
                {
                    SW_LOG_ERROR( "Local store backend '%#' is already registered by another module - the second one is ignored", pBackendName );
                    return;
                }
                _listBackend.push_back( Backend{ string( backendName ), createFunction } );
            }

            void remove( LocalSlotStorageCreateFunction createFunction )
            {
                std::scoped_lock<mutex> lock{ _mutex };
                for ( size_t index = 0; index < _listBackend.size(); ++index )
                {
                    if ( _listBackend[index]._createFunction != createFunction )
                        continue;
                    _listBackend.erase( _listBackend.begin() + static_cast<ptrdiff_t>( index ) );
                    return;
                }
            }

            LocalSlotStorageCreateFunction find( string_view backendName ) const
            {
                std::scoped_lock<mutex> lock{ _mutex };
                return findLocked( backendName );
            }

            const utf8* getModuleUnloadListenerName() const override { return "local store backends"; }
            bool        isReleaseExpected() const override { return true; }

            uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override
            {
                (void)outKeepImageMapped;
                std::scoped_lock<mutex> lock{ _mutex };
                uint32                  releasedCount = 0;
                for ( size_t index = _listBackend.size(); index > 0; --index )
                {
                    if ( isAddressWithin( reinterpret_cast<const void*>( _listBackend[index - 1]._createFunction ), pBegin, pEnd ) == false )
                        continue;
                    _listBackend.erase( _listBackend.begin() + static_cast<ptrdiff_t>( index - 1 ) );
                    ++releasedCount;
                }
                return releasedCount;
            }

        private:
            LocalStoreBackendTable()
                : IModuleUnloadListener{}
                , _mutex{}
                , _listBackend{}
            {
            }

            LocalSlotStorageCreateFunction findLocked( string_view backendName ) const
            {
                for ( const Backend& backend : _listBackend )
                {
                    if ( backend._name == backendName )
                        return backend._createFunction;
                }
                return nullptr;
            }

            mutable mutex   _mutex;
            vector<Backend> _listBackend;
        };
    } // namespace
} // namespace sw

namespace sw
{
    unique_ptr<ILocalStore> LocalStoreFactory::createLocalStore( string_view gameName, const LocalStoreConfig& settings, const LocalSealContext& sealContext,
                                                                 string& outError )
    {
        unique_ptr<ILocalSlotStorage> storage;
        if ( settings._backend == kMemoryBackendName )
        {
            storage = sw::make_unique<MemoryLocalDatabase>();
        }
        else
        {
            const string rootPath = UserDataPath::resolve( gameName, settings._root );
            if ( settings._backend == kFileBackendName )
            {
                storage = sw::make_unique<FileLocalSlotStorage>( rootPath );
            }
            else
            {
                const LocalSlotStorageCreateFunction createFunction = LocalStoreBackendTable::get().find( settings._backend );
                if ( createFunction == nullptr )
                {
                    outError = "local store backend '" + settings._backend + "' is not registered (file, memory, or a loaded kit's backend)";
                    return nullptr;
                }
                storage = createFunction( rootPath, outError );
            }
        }
        if ( storage == nullptr )
            return nullptr;
        return sw::make_unique<ThreadedLocalStore>( std::move( storage ), sealContext );
    }

    void LocalStoreFactory::registerBackend( const utf8* pBackendName, LocalSlotStorageCreateFunction createFunction )
    {
        LocalStoreBackendTable::get().add( pBackendName, createFunction );
    }

    void LocalStoreFactory::unregisterBackend( LocalSlotStorageCreateFunction createFunction )
    {
        LocalStoreBackendTable::get().remove( createFunction );
    }
} // namespace sw
