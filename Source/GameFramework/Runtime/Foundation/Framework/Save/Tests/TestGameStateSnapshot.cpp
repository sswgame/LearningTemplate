// GameStateSnapshotTest — 상태 스냅샷 봉투: 왕복, 머리(SWST) · 판이 다른 버퍼, 잘린 버퍼, 실패한 읽기가 실어 둔 상태를 남기지 않고 씬을 건드리지 않는 것, 머리의 콘텐츠 구성(켠 모드) · 씬 정체(nogpu, 씬 테스트만 지역 씬 매니저).
#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Foundation/Framework/Flow/IContentConfiguration.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/GameStateSnapshot.h"

namespace
{
    /** @brief 씬 · 리플렉션 상태 없이 찍은 봉투입니다(커스텀 상태 없이도 지원하는 사용법). */
    sw::vector<uint8> captureEmptyEnvelope( sw::GameStateSnapshot& snapshot )
    {
        sw::vector<uint8> bytes;
        snapshot.beginCapture();
        if ( snapshot.writeEnvelope( sw::SerializePurpose::Snapshot, nullptr, nullptr, bytes ) == false )
            bytes.clear();
        return bytes;
    }

    /** @brief 글 하나로 구성을 흉내 냅니다 — 같은 글의 세이브만 받는다(모드 키트의 판정은 TestModManifest 가 본다). */
    struct FakeContentConfiguration final : public sw::IContentConfiguration
    {
        sw::string _text;

        uint64     getContentConfigHash() const override { return _text.empty() ? 0u : 1u; }
        sw::string makeContentConfigText() const override { return _text; }
        bool       checkSaveCompatible( sw::string_view savedText ) const override
        {
            if ( savedText == _text )
                return true;
            SW_LOG_ERROR_ID( "GameStateSnapshotTest.ContentMismatch", "saved content '%#' is not '%#'", sw::string( savedText ).c_str(), _text.c_str() );
            return false;
        }
    };

    /** @brief @p manager 의 살아 있는 오브젝트 수입니다. */
    uint32 countObjects( const sw::GameObjectManager& manager )
    {
        uint32 count = 0;
        manager.forEachGameObject( [&count]( sw::GameObject* )
        { ++count; } );
        return count;
    }

    /** @brief 지역 씬 매니저에 리소스 id @p pSceneID 의 빈 씬을 활성으로 세우고 오브젝트 하나를 둡니다. 실패하면 nullptr 입니다. */
    sw::GameObjectManager* openSceneWithOneObject( sw::SceneManager& sceneManager, const utf8* pSceneID )
    {
        sw::Scene* const pScene = sceneManager.createEmptyActiveScene( pSceneID );
        if ( pScene == nullptr )
            return nullptr;
        pScene->setSourcePath( pSceneID );
        sw::GameObjectManager* const pManager = pScene->getObjectManager();
        if ( pManager == nullptr || pManager->createGameObject( sw::hashed_string( "Marker" ) ) == nullptr )
            return nullptr;
        return pManager;
    }
} // namespace

using namespace sw;

/** @brief [GameStateSnapshotTest] 같은 목적으로 쓴 봉투는 다시 읽히고, 읽은 뒤 저장소가 비어 있다 */
SW_TEST_CASE( GameStateSnapshotTest, EnvelopeRoundTrips )
{
    GameStateSnapshot   snapshot;
    const vector<uint8> bytes = captureEmptyEnvelope( snapshot );
    SW_REQUIRE_TRUE( bytes.size() >= 16 ); // 머리(4 + 4 + 8) 뒤에 섹션 셋
    SW_EXPECT_TRUE( snapshot.readEnvelope( SerializePurpose::Snapshot, nullptr, nullptr, bytes.data(), static_cast<uint32>( bytes.size() ) ) );
    snapshot.endRestore();
    SW_EXPECT_TRUE( snapshot.getComponentStateStore().isEmpty() );
}

/** @brief [GameStateSnapshotTest] 머리가 없거나 판이 다른 버퍼는 이유를 대며 거절한다 — 옛 형식 리더를 두지 않는다 */
SW_TEST_CASE( GameStateSnapshotTest, RejectsForeignHeaderAndOtherVersion )
{
    GameStateSnapshot   snapshot;
    const vector<uint8> bytes = captureEmptyEnvelope( snapshot );
    SW_REQUIRE_TRUE( bytes.size() >= 16 );

    vector<uint8> foreign = bytes;
    foreign[0]            = static_cast<uint8>( foreign[0] ^ 0xFFu );
    SW_EXPECT_LOG( "GameStateSnapshot.StateSnapshotNoSWST" );
    SW_EXPECT_FALSE( snapshot.readEnvelope( SerializePurpose::Snapshot, nullptr, nullptr, foreign.data(), static_cast<uint32>( foreign.size() ) ) );

    vector<uint8> otherVersion = bytes;
    otherVersion[4]            = static_cast<uint8>( otherVersion[4] + 1u ); // 판 번호(머리 둘째 칸)의 낮은 바이트
    SW_EXPECT_LOG( "GameStateSnapshot.StateSnapshotVersionNot" );
    SW_EXPECT_FALSE( snapshot.readEnvelope( SerializePurpose::Snapshot, nullptr, nullptr, otherVersion.data(), static_cast<uint32>( otherVersion.size() ) ) );
    SW_EXPECT_TRUE( snapshot.getComponentStateStore().isEmpty() );
}

/** @brief [GameStateSnapshotTest] 잘린 버퍼는 어디서 잘렸든 거절하고 실어 둔 상태를 남기지 않는다 */
SW_TEST_CASE( GameStateSnapshotTest, RejectsTruncatedBuffersAndKeepsTheStoreEmpty )
{
    GameStateSnapshot   snapshot;
    const vector<uint8> bytes = captureEmptyEnvelope( snapshot );
    SW_REQUIRE_TRUE( bytes.size() >= 16 );
    SW_EXPECT_FALSE( snapshot.readEnvelope( SerializePurpose::Snapshot, nullptr, nullptr, nullptr, 0 ) );
    SW_EXPECT_FALSE( snapshot.readEnvelope( SerializePurpose::Snapshot, nullptr, nullptr, bytes.data(), 8 ) ); // 머리만 반
    for ( uint32 size = 16; size < static_cast<uint32>( bytes.size() ); ++size )
    {
        SW_EXPECT_FALSE( snapshot.readEnvelope( SerializePurpose::Snapshot, nullptr, nullptr, bytes.data(), size ) );
        SW_EXPECT_TRUE( snapshot.getComponentStateStore().isEmpty() );
    }
}

/**
 * @brief [GameStateSnapshotTest] 봉투 머리는 이 세이브를 만든 콘텐츠 구성(켠 모드)을 싣고, 읽을 때 구성이 맞지 않으면 씬을 건드리기 전에 거절한다
 * @details 구성이 없는 실행은 모드를 켠 세이브를 받지 않는다(없는 리소스를 가리킨다). 모드 없는 세이브는 구성이 있는 실행에서도 구성의 판정을 지난다.
 */
SW_TEST_CASE( GameStateSnapshotTest, HeaderCarriesTheContentConfiguration )
{
    FakeContentConfiguration modded;
    modded._text = "lantern@1.0.0#000000000000abcd";
    SubsystemScope moddedScope;
    SW_REQUIRE_TRUE( moddedScope.bind<IContentConfiguration>( &modded ) );
    GameStateSnapshot snapshot;
    vector<uint8>     moddedSave;
    {
        const game::ScopedGameScope scoped{ moddedScope };
        moddedSave = captureEmptyEnvelope( snapshot );
        SW_REQUIRE_TRUE( moddedSave.empty() == false );
        SW_EXPECT_TRUE( snapshot.readEnvelope( SerializePurpose::Snapshot, nullptr, nullptr, moddedSave.data(), static_cast<uint32>( moddedSave.size() ) ) ); // 같은 구성
        snapshot.endRestore();
    }

    FakeContentConfiguration other;
    other._text = "lantern@2.0.0#000000000000abcd";
    SubsystemScope otherScope;
    SW_REQUIRE_TRUE( otherScope.bind<IContentConfiguration>( &other ) );
    {
        const game::ScopedGameScope scoped{ otherScope };
        SW_EXPECT_LOG( "GameStateSnapshotTest.ContentMismatch" );
        SW_EXPECT_FALSE( snapshot.readEnvelope( SerializePurpose::Snapshot, nullptr, nullptr, moddedSave.data(), static_cast<uint32>( moddedSave.size() ) ) );
        SW_EXPECT_TRUE( snapshot.getComponentStateStore().isEmpty() );
    }

    SubsystemScope plainScope; // 구성 없음 — 모드를 켠 세이브를 받지 않는다
    {
        const game::ScopedGameScope scoped{ plainScope };
        SW_EXPECT_LOG( "GameStateSnapshot.SaveNeedsContent" );
        SW_EXPECT_FALSE( snapshot.readEnvelope( SerializePurpose::Snapshot, nullptr, nullptr, moddedSave.data(), static_cast<uint32>( moddedSave.size() ) ) );
    }
}

/**
 * @brief [GameStateSnapshotTest] 봉투 머리는 찍은 씬의 리소스 id 를 싣고, 다른 씬 위에서 읽으면 그 씬을 비우지 않고 거절한다 — 같은 씬이면 지금처럼 복원한다
 * @details 언리얼 게임은 세이브의 맵을 연 뒤 적용한다. 씬 전환 뒤 적용 흐름이 들어오기 전까지 다른 씬의 세이브는 섞지 않고 거절한다.
 */
SW_TEST_CASE( GameStateSnapshotTest, RestoreInAnotherSceneDoesNotMix )
{
    SubsystemScope scope;
    SceneManager&  sceneManager = scope.emplaceSubsystem<SceneManager>();
    SW_REQUIRE_TRUE( sceneManager.initializeSubsystem() == SubsystemInitResult::Succeeded );
    const game::ScopedGameScope scoped{ scope };
    GameStateSnapshot           snapshot;

    // 같은 씬: 찍은 씬 위에서 읽으면 복원한다.
    GameObjectManager* pSceneA = openSceneWithOneObject( sceneManager, "scenes/a.scene" );
    SW_REQUIRE_NOT_NULL( pSceneA );
    const vector<uint8> saveA = captureEmptyEnvelope( snapshot );
    SW_REQUIRE_TRUE( saveA.empty() == false );
    SW_EXPECT_TRUE( snapshot.readEnvelope( SerializePurpose::Snapshot, nullptr, nullptr, saveA.data(), static_cast<uint32>( saveA.size() ) ) );
    snapshot.endRestore();
    SW_EXPECT_EQUAL( 1u, countObjects( *pSceneA ) );

    // 다른 씬: 씬 A 의 세이브를 씬 B 에서 읽으면 거절하고 B 의 오브젝트는 그대로다.
    GameObjectManager* pSceneB = openSceneWithOneObject( sceneManager, "scenes/b.scene" );
    SW_REQUIRE_NOT_NULL( pSceneB );
    SW_EXPECT_LOG( "GameStateSnapshot.SceneMismatch" );
    SW_EXPECT_FALSE( snapshot.readEnvelope( SerializePurpose::Snapshot, nullptr, nullptr, saveA.data(), static_cast<uint32>( saveA.size() ) ) );
    SW_EXPECT_EQUAL( 1u, countObjects( *pSceneB ) );
    SW_EXPECT_NOT_NULL( pSceneB->findGameObjectByName( hashed_string( "Marker" ) ) );
    SW_EXPECT_TRUE( snapshot.getComponentStateStore().isEmpty() );
}

/**
 * @brief [GameStateSnapshotTest] 머리만 읽기(`readSceneID`)는 찍은 씬의 리소스 id 를 돌려주고 활성 씬을 건드리지 않는다 — 머리가 틀리거나 잘린 버퍼는 없음이다(감사 G-7)
 * @details 슬롯 불러오기는 이 값으로 그 씬을 먼저 연 뒤 적용한다(언리얼 게임이 세이브의 맵으로 `OpenLevel` 한 뒤 적용하는 것과 같다).
 */
SW_TEST_CASE( GameStateSnapshotTest, ReadSceneIDPeeksTheHeaderOnly )
{
    SubsystemScope scope;
    SceneManager&  sceneManager = scope.emplaceSubsystem<SceneManager>();
    SW_REQUIRE_TRUE( sceneManager.initializeSubsystem() == SubsystemInitResult::Succeeded );
    const game::ScopedGameScope scoped{ scope };
    GameStateSnapshot           snapshot;

    SW_REQUIRE_NOT_NULL( openSceneWithOneObject( sceneManager, "scenes/a.scene" ) );
    const vector<uint8> saveA = captureEmptyEnvelope( snapshot );
    SW_REQUIRE_TRUE( saveA.empty() == false );

    // 다른 씬 위에서 머리만 읽는다 — 값은 찍은 씬이고 지금 씬의 오브젝트는 그대로다.
    GameObjectManager* pSceneB = openSceneWithOneObject( sceneManager, "scenes/b.scene" );
    SW_REQUIRE_NOT_NULL( pSceneB );
    const optional<string> sceneID = GameStateSnapshot::readSceneID( saveA.data(), static_cast<uint32>( saveA.size() ) );
    SW_REQUIRE_TRUE( sceneID.has_value() );
    SW_EXPECT_TRUE( *sceneID == "scenes/a.scene" );
    SW_EXPECT_EQUAL( 1u, countObjects( *pSceneB ) );

    // 머리가 없는 버퍼 · 머리 앞에서 잘린 버퍼는 없음이다.
    vector<uint8> foreign = saveA;
    foreign[0]            = static_cast<uint8>( foreign[0] ^ 0xFFu );
    SW_EXPECT_LOG( "GameStateSnapshot.StateSnapshotNoSWST" );
    SW_EXPECT_FALSE( GameStateSnapshot::readSceneID( foreign.data(), static_cast<uint32>( foreign.size() ) ).has_value() );
    SW_EXPECT_FALSE( GameStateSnapshot::readSceneID( nullptr, 0 ).has_value() );
    SW_EXPECT_FALSE( GameStateSnapshot::readSceneID( saveA.data(), 18 ).has_value() ); // 씬 정체 글의 길이 칸에서 잘렸다
}

/**
 * @brief [GameStateSnapshotTest] 진 복원은 씬을 건드리지 않는다 — 끝이 잘린 봉투는 씬 섹션이 온전해도 활성 씬을 비우기 전에 거절된다(감사 P0-6)
 * @details 예전에는 씬 오브젝트를 먼저 풀고(활성 씬을 비우고) 뒤 섹션을 읽다 졌다. 지금은 섹션을 모두 읽고 분리된 매니저 · 사본에 풀어 본 뒤에만 교체한다.
 */
SW_TEST_CASE( GameStateSnapshotTest, FailedRestoreKeepsScene )
{
    SubsystemScope scope;
    SceneManager&  sceneManager = scope.emplaceSubsystem<SceneManager>();
    SW_REQUIRE_TRUE( sceneManager.initializeSubsystem() == SubsystemInitResult::Succeeded );
    const game::ScopedGameScope scoped{ scope };
    GameStateSnapshot           snapshot;

    GameObjectManager* pScene = openSceneWithOneObject( sceneManager, "scenes/a.scene" );
    SW_REQUIRE_NOT_NULL( pScene );
    const vector<uint8> save = captureEmptyEnvelope( snapshot );
    SW_REQUIRE_TRUE( save.size() > 16 );
    SW_REQUIRE_NOT_NULL( pScene->createGameObject( hashed_string( "AfterSave" ) ) ); // 찍은 뒤에 선 오브젝트 — 복원이 씬을 갈았다면 사라진다

    // 마지막 섹션(컴포넌트 상태)의 끝이 잘린 봉투 — 앞의 씬 섹션은 온전하다.
    const expected<void, SnapshotRestoreFailure> result = snapshot.readEnvelope( SerializePurpose::Snapshot, nullptr, nullptr, save.data(), static_cast<uint32>( save.size() - 1 ) );
    SW_REQUIRE_TRUE( result.has_value() == false );
    SW_EXPECT_TRUE( result.error() == SnapshotRestoreFailure::Rejected );
    SW_EXPECT_EQUAL( 2u, countObjects( *pScene ) );
    SW_EXPECT_NOT_NULL( pScene->findGameObjectByName( hashed_string( "Marker" ) ) );
    SW_EXPECT_NOT_NULL( pScene->findGameObjectByName( hashed_string( "AfterSave" ) ) );
    SW_EXPECT_TRUE( snapshot.getComponentStateStore().isEmpty() );

    // 온전한 봉투는 지금처럼 교체한다 — 찍은 뒤에 선 오브젝트가 사라진다.
    SW_EXPECT_TRUE( snapshot.readEnvelope( SerializePurpose::Snapshot, nullptr, nullptr, save.data(), static_cast<uint32>( save.size() ) ) );
    snapshot.endRestore();
    SW_EXPECT_EQUAL( 1u, countObjects( *pScene ) );
    SW_EXPECT_NULL( pScene->findGameObjectByName( hashed_string( "AfterSave" ) ) );
}
