#include "pch.h"

#include "GameFramework/Base/Online/Local/LocalStoreFactory.h"

#include "Core/Container/vector.h"
#include "Core/File/UserDataPath.h"

#include "GameFramework/Base/Online/Local/FileLocalSlotStorage.h"
#include "GameFramework/Base/Online/Local/MemoryLocalStore.h"
#include "GameFramework/Base/Online/Local/ThreadedLocalStore.h"

namespace sw
{
    namespace
    {
        struct LocalStoreFactoryInternal
        {
            struct Backend
            {
                string                         _name{};
                LocalSlotStorageCreateFunction _createFunction{ nullptr };
            };

            static vector<Backend>& getBackends()
            {
                static vector<Backend> s_listBackend;
                return s_listBackend;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    unique_ptr<ILocalStore> LocalStoreFactory::createLocalStore( string_view gameName, const LocalStoreSettings& settings, const LocalSealContext& sealContext,
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
                for ( const LocalStoreFactoryInternal::Backend& backend : LocalStoreFactoryInternal::getBackends() )
                {
                    if ( backend._name == settings._backend )
                        storage = backend._createFunction( rootPath, outError );
                }
                if ( storage == nullptr && outError.empty() )
                    outError = "local store backend '" + settings._backend + "' is not registered (file, memory, or a kit's backend)";
            }
        }
        if ( storage == nullptr )
            return nullptr;
        return sw::make_unique<ThreadedLocalStore>( std::move( storage ), sealContext );
    }

    bool LocalStoreFactory::registerBackend( string_view backendName, LocalSlotStorageCreateFunction createFunction )
    {
        const bool bReserved = backendName == kFileBackendName || backendName == kMemoryBackendName;
        if ( bReserved || createFunction == nullptr )
            return false;
        vector<LocalStoreFactoryInternal::Backend>& listBackend = LocalStoreFactoryInternal::getBackends();
        for ( const LocalStoreFactoryInternal::Backend& backend : listBackend )
        {
            if ( backend._name == backendName )
                return backend._createFunction == createFunction; // 같은 함수를 두 번 올린 것은 받아 준다
        }
        listBackend.push_back( LocalStoreFactoryInternal::Backend{ string( backendName ), createFunction } );
        return true;
    }

    void LocalStoreFactory::unregisterBackend( string_view backendName )
    {
        vector<LocalStoreFactoryInternal::Backend>& listBackend = LocalStoreFactoryInternal::getBackends();
        for ( size_t index = 0; index < listBackend.size(); ++index )
        {
            if ( listBackend[index]._name != backendName )
                continue;
            listBackend.erase( listBackend.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }
} // namespace sw
