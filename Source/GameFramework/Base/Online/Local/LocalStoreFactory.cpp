#include "pch.h"

#include "GameFramework/Base/Online/Local/LocalStoreFactory.h"

#include "Core/Concurrency/mutex.h"
#include "Core/File/UserDataPath.h"
#include "Core/Module/ModuleUnloadListener.h"
#include "Core/String/RegistrationList.h"

#include "GameFramework/Base/Online/Local/FileLocalSlotStorage.h"
#include "GameFramework/Base/Online/Local/MemoryLocalStore.h"
#include "GameFramework/Base/Online/Local/ThreadedLocalStore.h"

namespace sw
{
    namespace
    {
        struct LocalStoreFactoryInternal
        {
            /** @brief 기반 백엔드 "file" 의 만들기 함수입니다. */
            static unique_ptr<ILocalSlotStorage> createFileStorage( string_view rootPath, string& outError )
            {
                (void)outError;
                return sw::make_unique<FileLocalSlotStorage>( string( rootPath ) );
            }

            /** @brief 기반 백엔드 "memory" 의 만들기 함수입니다. 경로는 쓰지 않는다. */
            static unique_ptr<ILocalSlotStorage> createMemoryStorage( string_view rootPath, string& outError )
            {
                (void)rootPath;
                (void)outError;
                return sw::make_unique<MemoryLocalDatabase>();
            }
        };

        /**
         * @class LocalStoreBackendTable
         * @brief 바닥 표입니다. 기반 백엔드("file" · "memory")와 키트가 올린 백엔드가 한 목록에 있어, 기반 이름을 키트가 덮는 일은 이름 중복으로 거절된다.
         * @details 모듈 언로드 리스너라서, 핫 리로드가 이미지를 내리기 전에 그 이미지의 만들기 함수를 지운다.
         *          지우는 것은 정상 경로다 — 키트의 정적 등록 객체는 이미지를 실제로 언로드할 때야 소멸하고, 핫 리로드는 그 언로드를 미룬다.
         */
        class LocalStoreBackendTable final : public IModuleUnloadListener
        {
        public:
            static LocalStoreBackendTable& get()
            {
                static LocalStoreBackendTable s_table;
                return s_table;
            }

            void add( const LocalStoreBackend& backend )
            {
                ScopedLock<mutex>                         lock{ _mutex };
                [[maybe_unused]] const RegistrationResult result = _registeredBackend.addLogged( &backend, "Local store backend", backend._pName );
                SW_ASSERT( result == RegistrationResult::Added );
            }

            void remove( const LocalStoreBackend& backend )
            {
                ScopedLock<mutex> lock{ _mutex };
                (void)_registeredBackend.remove( &backend ); // 언로드 리스너가 이미 지웠거나 거절된 등록이면 올라 있지 않다
            }

            LocalSlotStorageCreateFunction find( string_view backendName ) const
            {
                ScopedLock<mutex>        lock{ _mutex };
                const LocalStoreBackend* pBackend = _registeredBackend.findByName( backendName );
                return pBackend == nullptr ? nullptr : pBackend->_createFunction;
            }

            const utf8* getModuleUnloadListenerName() const override { return "local store backends"; }
            bool        isReleaseExpected() const override { return true; }

            uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override
            {
                (void)outKeepImageMapped;
                ScopedLock<mutex> lock{ _mutex };
                uint32            releasedCount = 0;
                for ( uint32 index = _registeredBackend.getCount(); index > 0; --index )
                {
                    const LocalStoreBackend* pBackend = _registeredBackend.getAt( index - 1 );
                    if ( isAddressWithin( reinterpret_cast<const void*>( pBackend->_createFunction ), pBegin, pEnd ) == false )
                        continue;
                    (void)_registeredBackend.remove( pBackend ); // 방금 목록에서 읽은 항목이라 올라 있다
                    ++releasedCount;
                }
                return releasedCount;
            }

        private:
            LocalStoreBackendTable()
                : IModuleUnloadListener{}
                , _mutex{}
                , _registeredBackend{ "LocalStoreFactory", RegistrationOrder::Insertion, true }
                , _fileBackend{ LocalStoreFactory::kFileBackendName, &LocalStoreFactoryInternal::createFileStorage }
                , _memoryBackend{ LocalStoreFactory::kMemoryBackendName, &LocalStoreFactoryInternal::createMemoryStorage }
            {
                (void)_registeredBackend.addLogged( &_fileBackend, "Local store backend", _fileBackend._pName );
                (void)_registeredBackend.addLogged( &_memoryBackend, "Local store backend", _memoryBackend._pName );
            }

            mutable mutex                                                _mutex;
            RegistrationList<const LocalStoreBackend> _registeredBackend SW_GUARDED_BY( _mutex );
            LocalStoreBackend                                            _fileBackend;   ///< 기반 백엔드. 표가 소유하고, 이미지를 내려도 지워지지 않는다(만들기 함수가 기반 이미지에 있다)
            LocalStoreBackend                                            _memoryBackend; ///< 기반 백엔드
        };
    } // namespace
} // namespace sw

namespace sw
{
    unique_ptr<ILocalStore> LocalStoreFactory::createLocalStore( string_view gameName, const LocalStoreConfig& settings, const LocalSealContext& sealContext,
                                                                 string& outError )
    {
        const LocalSlotStorageCreateFunction createFunction = LocalStoreBackendTable::get().find( settings._backend );
        if ( createFunction == nullptr )
        {
            outError = "local store backend '" + settings._backend + "' is not registered (file, memory, or a loaded kit's backend)";
            return nullptr;
        }
        const string                  rootPath = UserDataPath::resolve( gameName, settings._root );
        unique_ptr<ILocalSlotStorage> storage  = createFunction( rootPath, outError );
        if ( storage == nullptr )
            return nullptr;
        return sw::make_unique<ThreadedLocalStore>( std::move( storage ), sealContext );
    }

    void LocalStoreFactory::registerBackend( const LocalStoreBackend& backend )
    {
        LocalStoreBackendTable::get().add( backend );
    }

    void LocalStoreFactory::unregisterBackend( const LocalStoreBackend& backend )
    {
        LocalStoreBackendTable::get().remove( backend );
    }
} // namespace sw
