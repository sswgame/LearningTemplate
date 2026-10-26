#include "pch.h"

#include "Core/Module/ModuleUnloadListener.h"

#include "GameFramework/Base/Online/Server/OnlineStoreDriverTable.h"

#include "TestFramework/TestFramework.h"

// 저장 드라이버 테이블 — 핫 리로드로 저장 키트만 내렸다 올려도(새 이미지가 먼저 묶고 옛 묶음이 나중에 사라짐) 테이블이 새 함수를 들고 있는지,
// 내려가는 이미지 범위의 함수는 언로드 리스너가 지우는지 본다.

using namespace sw;

namespace
{
    struct OnlineStoreDriverTableTestInternal
    {
        static unique_ptr<IServiceStore> createOldImageStore( string_view driverName, string_view connection, string_view secret, int32 workerCount, int64 nowMs,
                                                              string& outError )
        {
            (void)driverName;
            (void)connection;
            (void)secret;
            (void)workerCount;
            (void)nowMs;
            outError = "old image";
            return nullptr;
        }

        static unique_ptr<IServiceStore> createNewImageStore( string_view driverName, string_view connection, string_view secret, int32 workerCount, int64 nowMs,
                                                              string& outError )
        {
            (void)driverName;
            (void)connection;
            (void)secret;
            (void)workerCount;
            (void)nowMs;
            outError = "new image";
            return nullptr;
        }

        static unique_ptr<IEphemeralStore> createOldImageCache( string_view driverName, string_view endpoint, string_view secret, string& outError )
        {
            (void)driverName;
            (void)endpoint;
            (void)secret;
            outError = "old image";
            return nullptr;
        }

        static unique_ptr<IEphemeralStore> createNewImageCache( string_view driverName, string_view endpoint, string_view secret, string& outError )
        {
            (void)driverName;
            (void)endpoint;
            (void)secret;
            outError = "new image";
            return nullptr;
        }
    };
} // namespace

SW_TEST_CASE( OnlineStoreDriverTableTest, HotReloadOfTheStorageKitKeepsTheNewImageFactory )
{
    using Internal                                                        = OnlineStoreDriverTableTestInternal;
    const OnlineStoreDriverTable::CreateServiceStoreFunction   pLoaded    = OnlineStoreDriverTable::findServiceStoreFactory();
    const OnlineStoreDriverTable::CreateEphemeralStoreFunction pLoadedEph = OnlineStoreDriverTable::findEphemeralStoreFactory();
    {
        unique_ptr<OnlineStoreDriverBinding> oldStore = make_unique<OnlineStoreDriverBinding>( &Internal::createOldImageStore, nullptr );
        unique_ptr<OnlineStoreDriverBinding> oldCache = make_unique<OnlineStoreDriverBinding>( &Internal::createOldImageCache, nullptr );
        // 핫 리로드 — 새 이미지가 먼저 묶고, 옛 이미지의 정적 묶음은 미뤄진 언로드 때 사라진다.
        unique_ptr<OnlineStoreDriverBinding> newStore = make_unique<OnlineStoreDriverBinding>( &Internal::createNewImageStore, nullptr );
        unique_ptr<OnlineStoreDriverBinding> newCache = make_unique<OnlineStoreDriverBinding>( &Internal::createNewImageCache, nullptr );
        oldStore.reset();
        oldCache.reset();
        SW_EXPECT_TRUE( OnlineStoreDriverTable::findServiceStoreFactory() == &Internal::createNewImageStore );
        SW_EXPECT_TRUE( OnlineStoreDriverTable::findEphemeralStoreFactory() == &Internal::createNewImageCache );
        newStore.reset();
        newCache.reset();
        SW_EXPECT_TRUE( OnlineStoreDriverTable::findServiceStoreFactory() == nullptr ); // 자기 함수라 풀린다
        SW_EXPECT_TRUE( OnlineStoreDriverTable::findEphemeralStoreFactory() == nullptr );
    }
    OnlineStoreDriverTable::bindServiceStoreFactory( pLoaded ); // 이 프로세스에 실린 저장 키트의 함수를 되돌린다(없었으면 빈 칸)
    OnlineStoreDriverTable::bindEphemeralStoreFactory( pLoadedEph );
}

SW_TEST_CASE( OnlineStoreDriverTableTest, UnloadListenerClearsOnlyTheUnloadingImage )
{
    using Internal                                                     = OnlineStoreDriverTableTestInternal;
    const OnlineStoreDriverTable::CreateServiceStoreFunction pLoaded   = OnlineStoreDriverTable::findServiceStoreFactory();
    const uint8*                                             pNewBegin = reinterpret_cast<const uint8*>( &Internal::createNewImageStore );
    const uint8*                                             pOldBegin = reinterpret_cast<const uint8*>( &Internal::createOldImageStore );
    vector<IModuleUnloadListener::ReleaseResult>             listResult;

    OnlineStoreDriverTable::bindServiceStoreFactory( &Internal::createNewImageStore );
    // 다른 이미지(옛 함수 한 바이트)를 내리면 새 함수는 남는다.
    IModuleUnloadListener::removeAllWithin( pOldBegin, pOldBegin + 1, listResult );
    SW_EXPECT_TRUE( OnlineStoreDriverTable::findServiceStoreFactory() == &Internal::createNewImageStore );
    // 새 함수가 든 이미지를 내리면 칸이 빈다 — 미뤄진 언로드 뒤에도 내려간 코드를 부르지 않는다.
    IModuleUnloadListener::removeAllWithin( pNewBegin, pNewBegin + 1, listResult );
    SW_EXPECT_TRUE( OnlineStoreDriverTable::findServiceStoreFactory() == nullptr );

    OnlineStoreDriverTable::bindServiceStoreFactory( pLoaded );
}
