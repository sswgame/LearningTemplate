#include "pch.h"

#include "Core/File/FileUtil.h"

#include "EngineTest/GameFramework/Online/LocalStoreContract.h"

#include "GameFramework/Base/Online/Local/FileLocalSlotStorage.h"
#include "GameFramework/Base/Online/Local/LocalStoreFactory.h"
#include "GameFramework/Base/Online/Local/ThreadedLocalStore.h"

// 파일 로컬 저장(전용 스레드 + 파일 바닥) — 계약 아홉 + 꺼진 쓰기의 임시 파일 지우기 · 슬롯 → 파일 경로 · 공장(file · memory · 모르는 이름).

using namespace sw;

namespace
{
    struct FileLocalFixture
    {
        test::LocalStoreTestKey        _key;
        string                         _root;
        FileLocalSlotStorage           _directStorage; ///< 같은 폴더의 두 번째 바닥 — 저장소가 쉬는 동안 봉투를 직접 본다
        unique_ptr<ThreadedLocalStore> _store;

        FileLocalFixture()
            : _key{}
            , _root{ test::makeTempDirectory( "local_store_file" ) }
            , _directStorage{ _root }
            , _store{}
        {
            restart();
        }

        ILocalStore&             getStore() { return *_store; }
        ILocalSlotStorage&       getStorage() { return _directStorage; }
        test::LocalStoreTestKey& getKey() { return _key; }

        void restart()
        {
            _store.reset();
            _store = sw::make_unique<ThreadedLocalStore>( sw::make_unique<FileLocalSlotStorage>( _root ), test::LocalStoreContract::makeSealContext( _key ) );
        }
    };
} // namespace

SW_LOCAL_STORE_CONTRACT_SUITE( LocalStoreFileTest, FileLocalFixture )

SW_TEST_CASE( LocalStoreFileTest, SlotIsAFileUnderTheRootAndLeftoverTempFilesAreRemoved )
{
    const string root = test::makeTempDirectory( "local_store_layout" );
    {
        FileLocalSlotStorage storage{ root };
        const vector<uint8>  envelopeBytes{ 1, 2, 3 };
        SW_ASSERT_TRUE( storage.writeSlot( "save/slot0", envelopeBytes ) == LocalStoreResult::Ok );
        SW_EXPECT_TRUE( FileUtil::isRegularFile( FileUtil::joinPath( root, "save/slot0.swls" ) ) );
    }
    const string leftover = FileUtil::joinPath( root, "save/slot0.swls.tmp1234_0" ); // 꺼진 쓰기의 찌꺼기
    SW_ASSERT_TRUE( FileUtil::writeFile( leftover, reinterpret_cast<const uint8*>( "x" ), 1 ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "a leftover temp file is reported when removed" );
        FileLocalSlotStorage restarted{ root };
        SW_EXPECT_FALSE( FileUtil::exists( leftover ) );
        vector<LocalSlotInfo> listSlotInfo;
        SW_EXPECT_TRUE( restarted.listSlots( "", listSlotInfo ) == LocalStoreResult::Ok );
        SW_ASSERT_EQUAL( size_t( 1 ), listSlotInfo.size() );
        SW_EXPECT_TRUE( listSlotInfo[0]._slot == "save/slot0" );
        SW_EXPECT_EQUAL( int64( 3 ), listSlotInfo[0]._byteCount );
    }
}

SW_TEST_CASE( LocalStoreFileTest, FactoryBuildsFileAndMemoryAndRefusesUnknownBackends )
{
    test::LocalStoreTestKey key;
    const LocalSealContext  context = test::LocalStoreContract::makeSealContext( key );
    string                  error;
    LocalStoreSettings      settings;
    settings._backend                 = LocalStoreFactory::kFileBackendName;
    settings._root                    = test::makeTempDirectory( "local_store_factory" ); // 절대 경로는 그대로
    unique_ptr<ILocalStore> fileStore = LocalStoreFactory::createLocalStore( "UnitGame", settings, context, error );
    SW_ASSERT_TRUE_MSG( fileStore != nullptr, error.c_str() );
    SW_EXPECT_TRUE( test::LocalStoreContract::write( *fileStore, "cfg/a", test::LocalStoreContract::makeText( "1" ) ) == LocalStoreResult::Ok );
    SW_EXPECT_TRUE( FileUtil::isRegularFile( FileUtil::joinPath( settings._root, "cfg/a.swls" ) ) );

    settings._backend                   = LocalStoreFactory::kMemoryBackendName;
    unique_ptr<ILocalStore> memoryStore = LocalStoreFactory::createLocalStore( "UnitGame", settings, context, error );
    SW_ASSERT_TRUE( memoryStore != nullptr );
    SW_EXPECT_TRUE( test::LocalStoreContract::read( *memoryStore, "cfg/a" )._result == LocalStoreResult::NotFound ); // 따로 사는 메모리

    settings._backend = "cloud";
    error.clear();
    SW_EXPECT_TRUE( LocalStoreFactory::createLocalStore( "UnitGame", settings, context, error ) == nullptr );
    SW_EXPECT_TRUE( error.find( "cloud" ) != string::npos );
    SW_EXPECT_FALSE( LocalStoreFactory::registerBackend( LocalStoreFactory::kFileBackendName, nullptr ) );
}
