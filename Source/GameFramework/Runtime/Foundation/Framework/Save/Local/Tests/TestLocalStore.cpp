#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/UserDataPath.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/Local/FileLocalSlotStorage.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Local/LocalStoreBackend.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Local/MemoryLocalStore.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Local/Tests/LocalStoreContract.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/Local/ThreadedLocalStore.h"

// LocalStoreMemoryTest · LocalStoreFileTest — 로컬 저장: 메모리 저장소와 파일 저장소(전용 스레드 + 파일 바닥)가 같은 계약 아홉(왕복 · 나열 · 압축 · 봉인 셋 · 이름 ·
// 순서 · 내리기)을 지킨다. 메모리는 쓰기 도중 꺼짐 · 슬롯 이름 규칙, 파일은 슬롯 → 파일 경로와 꺼진 쓰기의 임시 파일 지우기, 공장(file · memory · 모르는 이름 · 기반 이름 덮기)(nogpu).

using namespace sw;

namespace
{
    struct MemoryLocalFixture
    {
        test::LocalStoreSealParts    _seal{};
        MemoryLocalDatabase          _database{};
        unique_ptr<MemoryLocalStore> _store{};

        MemoryLocalFixture() { restart(); }

        ILocalStore&             getStore() { return *_store; }
        ILocalSlotStorage&       getStorage() { return _database; }
        test::LocalStoreTestKey& getKey() { return _seal._key; }
        void                     restart() { _store = make_unique<MemoryLocalStore>( &_database, _seal.makeSealContext() ); }
    };

    struct FileLocalFixture
    {
        test::LocalStoreSealParts      _seal{};
        string                         _root{ test::makeScratchPath( "local_store_file" ) };
        FileLocalSlotStorage           _directStorage{ _root }; ///< 같은 폴더의 두 번째 바닥 — 저장소가 쉬는 동안 봉투를 직접 본다
        unique_ptr<ThreadedLocalStore> _store{};

        FileLocalFixture() { restart(); }

        ILocalStore&             getStore() { return *_store; }
        ILocalSlotStorage&       getStorage() { return _directStorage; }
        test::LocalStoreTestKey& getKey() { return _seal._key; }

        void restart()
        {
            _store.reset();
            _store = make_unique<ThreadedLocalStore>( make_unique<FileLocalSlotStorage>( _root ), _seal.makeSealContext() );
        }
    };

    /** @brief 기반 이름을 덮으려는 등록에 쓰는 만들기 함수입니다. 등록이 거절되므로 불리지 않습니다. */
    expected<unique_ptr<ILocalSlotStorage>> createNeverUsedLocalStorage( const LocalStoreLocation& /*location*/, string_view /*gameName*/ )
    {
        return sw::unexpected( Error{ ErrorKind::Unsupported, "never used" } );
    }
} // namespace

SW_LOCAL_STORE_CONTRACT_SUITE( LocalStoreMemoryTest, MemoryLocalFixture )
SW_LOCAL_STORE_CONTRACT_SUITE( LocalStoreFileTest, FileLocalFixture )

/** @brief [LocalStoreMemoryTest] 쓰기 도중 꺼지면 앞 내용이 그대로 남는다(원자적 바꿔치기) */
SW_TEST_CASE( LocalStoreMemoryTest, WriteInterruptedMidwayLeavesTheOldContent )
{
    MemoryLocalFixture fixture;
    SW_EXPECT_TRUE( test::LocalStoreContract::write( fixture.getStore(), "save/slot0", test::LocalStoreContract::makeText( "old" ) ) == LocalStoreResult::Ok );
    fixture._database.failNextWrite(); // 다음 쓰기 도중 꺼짐
    SW_EXPECT_TRUE( test::LocalStoreContract::write( fixture.getStore(), "save/slot0", test::LocalStoreContract::makeText( "new" ) ) == LocalStoreResult::IOError );
    fixture.restart();
    SW_EXPECT_TRUE( test::LocalStoreContract::read( fixture.getStore(), "save/slot0" )._bytes == test::LocalStoreContract::makeText( "old" ) );
}

/** @brief [LocalStoreMemoryTest] 슬롯은 `[0-9a-z._-]` 마디 하나나 둘(`묶음/이름`), 64 B 이하이고, 그룹 접두는 빈 글이나 `묶음/` 이다 */
SW_TEST_CASE( LocalStoreMemoryTest, SlotNameRules )
{
    SW_EXPECT_TRUE( ILocalStore::isValidSlotName( "save/slot0" ) );
    SW_EXPECT_TRUE( ILocalStore::isValidSlotName( "profile.v2-a_b" ) );
    SW_EXPECT_TRUE( ILocalStore::isValidSlotName( string( ILocalStore::kMaxSlotNameSize, 'a' ) ) );
    SW_EXPECT_FALSE( ILocalStore::isValidSlotName( string( ILocalStore::kMaxSlotNameSize + 1, 'a' ) ) );
    SW_EXPECT_FALSE( ILocalStore::isValidSlotName( "save/Slot0" ) );
    SW_EXPECT_FALSE( ILocalStore::isValidSlotName( "save\\slot0" ) );
    SW_EXPECT_FALSE( ILocalStore::isValidSlotName( "/slot" ) );
    SW_EXPECT_FALSE( ILocalStore::isValidSlotName( "save/.." ) );
    SW_EXPECT_TRUE( ILocalStore::isValidGroupPrefix( "" ) );
    SW_EXPECT_TRUE( ILocalStore::isValidGroupPrefix( "save/" ) );
    SW_EXPECT_FALSE( ILocalStore::isValidGroupPrefix( "save" ) );
    SW_EXPECT_FALSE( ILocalStore::isValidGroupPrefix( "a/b/" ) );
}

/** @brief [LocalStoreFileTest] 슬롯은 뿌리 아래 `<슬롯>.swls` 파일이고, 다시 열 때 꺼진 쓰기가 남긴 임시 파일은 지우고 알린다 */
SW_TEST_CASE( LocalStoreFileTest, SlotIsAFileUnderTheRootAndLeftoverTempFilesAreRemoved )
{
    const string root = test::makeScratchPath( "local_store_layout" );
    {
        FileLocalSlotStorage storage{ root };
        const vector<uint8>  envelopeBytes{ 1, 2, 3 };
        SW_REQUIRE_TRUE( storage.writeSlot( "save/slot0", envelopeBytes ) == LocalStoreResult::Ok );
        SW_EXPECT_TRUE( FileUtil::isRegularFile( FileUtil::joinPath( root, "save/slot0.swls" ) ) );
    }
    const string leftover = FileUtil::joinPath( root, "save/slot0.swls.tmp2147483644_0" ); // 꺼진 쓰기의 찌꺼기(있을 수 없는 pid — 죽은 쓰는 쪽)
    SW_REQUIRE_TRUE( FileUtil::writeFile( leftover, reinterpret_cast<const uint8*>( "x" ), 1 ) );
    // 살아 있는 프로세스(이 프로세스)의 임시 파일은 다른 인스턴스가 쓰는 중인 것이다 — 남긴다(병렬 백엔드 실행이 서로의 쓰기를 지우던 결함)
    StringBuilder<constant::file::kMaxPathSize> inFlightBuilder;
    inFlightBuilder.append( FileUtil::joinPath( root, "save/slot0.swls.tmp" ).c_str() ).append( static_cast<uint64>( Process::getCurrentProcessID() ) ).append( "_0" );
    const string inFlight{ inFlightBuilder.c_str() };
    SW_REQUIRE_TRUE( FileUtil::writeFile( inFlight, reinterpret_cast<const uint8*>( "y" ), 1 ) );
    SW_EXPECT_LOG( "LocalStore.LeftoverTempRemoved" );
    FileLocalSlotStorage restarted{ root };
    SW_EXPECT_FALSE( FileUtil::exists( leftover ) );
    SW_EXPECT_TRUE( FileUtil::exists( inFlight ) );
    SW_REQUIRE_TRUE( FileUtil::removeFile( inFlight ) );
    vector<LocalSlotInfo> listSlotInfo;
    SW_EXPECT_TRUE( restarted.listSlots( "", listSlotInfo ) == LocalStoreResult::Ok );
    SW_REQUIRE_EQUAL( size_t( 1 ), listSlotInfo.size() );
    SW_EXPECT_EQUAL( listSlotInfo[0]._slot, string( "save/slot0" ) );
    SW_EXPECT_EQUAL( int64( 3 ), listSlotInfo[0]._byteCount );
}

/** @brief [LocalStoreFileTest] 공장은 file · memory 를 만들고 모르는 백엔드는 이름을 담은 오류로 거절한다. 기반 이름은 키트가 덮지 못한다 */
SW_TEST_CASE( LocalStoreFileTest, FactoryBuildsFileAndMemoryAndRefusesUnknownBackends )
{
    test::LocalStoreSealParts    seal;
    const LocalSealContext       context  = seal.makeSealContext();
    const string                 root     = test::makeScratchPath( "local_store_factory" );
    const optional<AbsolutePath> rootPath = AbsolutePath::tryMake( root );
    SW_REQUIRE_TRUE( rootPath.has_value() );
    LocalStoreConfig                  settings{ LocalStoreBackend::kFileBackendName, LocalStoreLocation::absolute( *rootPath ) };
    expected<unique_ptr<ILocalStore>> fileStore = LocalStoreBackend::createLocalStore( "UnitGame", settings, context );
    SW_REQUIRE_TRUE_MSG( fileStore.has_value(), fileStore.has_value() ? string_view{} : fileStore.error().message() );
    SW_EXPECT_TRUE( test::LocalStoreContract::write( **fileStore, "cfg/a", test::LocalStoreContract::makeText( "1" ) ) == LocalStoreResult::Ok );
    SW_EXPECT_TRUE( FileUtil::isRegularFile( FileUtil::joinPath( root, "cfg/a.swls" ) ) );

    LocalStoreConfig                  memorySettings{ LocalStoreBackend::kMemoryBackendName, LocalStoreLocation::memory() };
    expected<unique_ptr<ILocalStore>> memoryStore = LocalStoreBackend::createLocalStore( "UnitGame", memorySettings, context );
    SW_REQUIRE_TRUE( memoryStore.has_value() );
    SW_EXPECT_TRUE( test::LocalStoreContract::read( **memoryStore, "cfg/a" )._result == LocalStoreResult::NotFound ); // 따로 사는 메모리

    LocalStoreConfig                        cloudSettings{ "cloud", LocalStoreLocation::memory() };
    const expected<unique_ptr<ILocalStore>> cloudStore = LocalStoreBackend::createLocalStore( "UnitGame", cloudSettings, context );
    SW_REQUIRE_FALSE( cloudStore.has_value() );
    SW_EXPECT_TRUE( cloudStore.error().detail() == "cloud" );
    {
        // 기반 이름은 키트가 덮지 못한다 — 기반 백엔드도 같은 표에 있어 이름 중복으로 거절되고, 등록은 단언으로 멈춘다.
        test::ScopedAssertCapture assertCapture;
        SW_EXPECT_LOG( "Registration.DuplicateName" );
        const LocalStoreBackendRegistration reserved{ LocalStoreBackend::kFileBackendName, createNeverUsedLocalStorage };
        if constexpr ( test::ScopedAssertCapture::kAssertsAreActive )
            SW_EXPECT_EQUAL( 1u, assertCapture.getCount() );
        SW_EXPECT_TRUE( LocalStoreBackend::createLocalStore( "UnitGame", settings, context ) ); // 기반 파일 백엔드가 그대로 만든다
    }
    SW_EXPECT_TRUE( LocalStoreBackend::createLocalStore( "UnitGame", settings, context ) ); // 거절된 등록이 풀려도 기반 백엔드는 남는다
}
