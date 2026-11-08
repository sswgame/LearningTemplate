#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/Local/LocalStoreBackend.h"

#include "Core/String/RegistrationList.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/Local/FileLocalSlotStorage.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Local/MemoryLocalStore.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Local/ThreadedLocalStore.h"

namespace sw
{
    namespace
    {
        struct LocalStoreFactoryInternal
        {
            /** @brief 기반 백엔드 "file" 의 만들기 함수입니다. 디스크 자리만 연다. */
            static unique_ptr<ILocalSlotStorage> createFileStorage( const LocalStoreLocation& location, string_view gameName, string& outError )
            {
                if ( location.getKind() == LocalStoreLocation::Kind::Memory )
                {
                    outError = "the file local store backend needs a disk location (user data or absolute), not memory";
                    return nullptr;
                }
                return sw::make_unique<FileLocalSlotStorage>( location.resolveDiskPath( gameName ) );
            }

            /** @brief 기반 백엔드 "memory" 의 만들기 함수입니다. 메모리 자리만 연다(디스크 자리를 주면 쓴 줄 알았던 세이브가 사라진다). */
            static unique_ptr<ILocalSlotStorage> createMemoryStorage( const LocalStoreLocation& location, string_view gameName, string& outError )
            {
                (void)gameName;
                if ( location.getKind() != LocalStoreLocation::Kind::Memory )
                {
                    outError = "the memory local store backend takes only a memory location";
                    return nullptr;
                }
                return sw::make_unique<MemoryLocalDatabase>();
            }
        };

        /**
         * @brief 바닥 표입니다. 기반 백엔드("file" · "memory")와 키트가 올린 백엔드가 한 목록에 있어, 기반 이름을 키트가 덮는 일은 이름 중복으로 거절된다.
         * @details 정적 등록 표라 공통 틀(잠금을 든 함수 정적 목록)에 둔다 — 키트 이미지의 정적 등록자가 모듈 로더 스레드에서 먼저 와도 표가 서 있다.
         */
        SW_DEFINE_LOCKED_REGISTRATION_LIST( getBackends, const LocalStoreBackend, "LocalStoreFactory", RegistrationOrder::Insertion, true )
    } // namespace

    // 기반 백엔드 둘 — 이 이미지(GameFramework)의 정적 등록이라 이미지를 내려도 남는다(만들기 함수가 이 이미지에 있다).
    static const LocalStoreBackendRegistration s_fileBackendRegistration{ LocalStoreBackend::kFileBackendName, LocalStoreFactoryInternal::createFileStorage };
    static const LocalStoreBackendRegistration s_memoryBackendRegistration{ LocalStoreBackend::kMemoryBackendName, LocalStoreFactoryInternal::createMemoryStorage };
} // namespace sw

namespace sw
{
    unique_ptr<ILocalStore> LocalStoreBackend::createLocalStore( string_view gameName, const LocalStoreConfig& settings, const LocalSealContext& sealContext,
                                                                 string& outError )
    {
        const LocalSlotStorageCreateFunction createFunction = getBackends().withLock( [&settings]( const RegistrationList<const LocalStoreBackend>& list ) -> LocalSlotStorageCreateFunction
        {
            const LocalStoreBackend* const pBackend = list.findByName( settings._backend );
            return pBackend == nullptr ? nullptr : pBackend->_createFunction;
        } );
        if ( createFunction == nullptr )
        {
            outError = "local store backend '" + settings._backend + "' is not registered (file, memory, or a loaded kit's backend)";
            return nullptr;
        }
        unique_ptr<ILocalSlotStorage> storage = createFunction( settings._location, gameName, outError );
        if ( storage == nullptr )
            return nullptr;
        return sw::make_unique<ThreadedLocalStore>( std::move( storage ), sealContext );
    }

    void LocalStoreBackend::registerBackend( const LocalStoreBackend& backend )
    {
        getBackends().withLock( [&backend]( RegistrationList<const LocalStoreBackend>& list )
        {
            [[maybe_unused]] const RegistrationResult result = list.addLogged( &backend, "Local store backend", backend._pName );
            SW_ASSERT( result == RegistrationResult::Added );
        } );
    }

    void LocalStoreBackend::unregisterBackend( const LocalStoreBackend& backend )
    {
        getBackends().withLock( [&backend]( RegistrationList<const LocalStoreBackend>& list )
        {
            (void)list.remove( &backend ); // 거절된 등록이면 올라 있지 않다
        } );
    }
} // namespace sw
