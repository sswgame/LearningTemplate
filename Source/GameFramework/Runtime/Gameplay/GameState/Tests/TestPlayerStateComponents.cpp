#include "pch.h"

#include "Core/Container/span.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Character/Control/Controller/PlayerControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"
#include "GameFramework/Runtime/Gameplay/Inventory/IItemReceiver.h"
#include "GameFramework/Runtime/Gameplay/Inventory/InventoryComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/WalletComponent.h"
#include "GameFramework/Runtime/Gameplay/Progression/ReputationComponent.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestCatalog.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLogComponent.h"

// PlayerStateComponentsTest: 플레이어 상태 컴포넌트(조종자 오브젝트의 지갑 · 가방 · 퀘스트 일지 · 평판, 기능마다 하나): 각자의 저장 왕복과 열기 전에 받은 복원 바이트,
// 가방 없음 = 컴포넌트 없음(아이템 받기 계약도 가방 컴포넌트가 내보낸다), 폰에서 찾기, 씬 하나를 읽는 쪽의 "하나뿐" 규칙(nogpu, 헤드리스).

using namespace sw;

namespace
{
    /** @brief @p component 의 상태를 바이트로 떠 냅니다(`ComponentStateCodec::captureObject` 와 같은 길). */
    template <typename TComponent>
    vector<uint8> captureState( const TComponent& component )
    {
        Archive written;
        component.writeState( written );
        return vector<uint8>( written.getData(), written.getData() + written.getSize() );
    }
} // namespace

/** @brief [PlayerStateComponentsTest] 지갑은 열기 없이 늘 열려 있고, 받은 복원 바이트를 바로 적용해 복원한 판(`isFreshGame` false)이 된다 */
SW_TEST_CASE( PlayerStateComponentsTest, WalletRoundTripsAndRestoresAtOnce )
{
    GameObjectManager manager;
    WalletComponent*  pWallet = manager.createGameObject( hashed_string( "Player" ) )->addComponent<WalletComponent>();
    SW_REQUIRE_NOT_NULL( pWallet );
    SW_EXPECT_TRUE( pWallet->isOpen() );
    SW_EXPECT_TRUE( pWallet->isFreshGame() );
    pWallet->getWallet().add( Wallet::getDefaultCurrency(), 120 );

    WalletComponent* pRestored = manager.createGameObject( hashed_string( "Restored" ) )->addComponent<WalletComponent>();
    SW_REQUIRE_NOT_NULL( pRestored );
    pRestored->restoreState( captureState( *pWallet ) );
    SW_EXPECT_FALSE( pRestored->isFreshGame() );
    SW_EXPECT_EQUAL( int64{ 120 }, pRestored->getWallet().getBalance( Wallet::getDefaultCurrency() ) );
}

/** @brief [PlayerStateComponentsTest] 가방은 열기 전에 받은 복원 바이트를 들고 있다가 `initialize` 가 적용해 복원한 판(`Restored`)으로 연다. 열기 전 캡처는 들고 있던 바이트 그대로다 */
SW_TEST_CASE( PlayerStateComponentsTest, InventoryRestoresHeldBytesWhenOpened )
{
    GameObjectManager   manager;
    InventoryComponent* pBag = manager.createGameObject( hashed_string( "Player" ) )->addComponent<InventoryComponent>();
    SW_REQUIRE_NOT_NULL( pBag );
    const ItemCatalog itemCatalog{};
    InventorySettings settings;
    settings._slotCount = 4;
    SW_REQUIRE_TRUE( pBag->initialize( itemCatalog, settings ) == GameStateInitResult::Fresh );
    SW_EXPECT_TRUE( pBag->initialize( itemCatalog, settings ) == GameStateInitResult::AlreadyInitialized );
    const hashed_string seedItem( "seed" );
    SW_REQUIRE_EQUAL( 2, pBag->getInventory().addItem( seedItem, 2 ) );
    const vector<uint8> saved = captureState( *pBag );

    InventoryComponent* pRestored = manager.createGameObject( hashed_string( "Restored" ) )->addComponent<InventoryComponent>();
    SW_REQUIRE_NOT_NULL( pRestored );
    pRestored->restoreState( vector<uint8>( saved ) );     // 열기 전 — 들고 있다
    SW_EXPECT_TRUE( captureState( *pRestored ) == saved ); // 두 번째 캡처가 빈 판으로 세이브를 덮지 않는다
    SW_EXPECT_TRUE( pRestored->initialize( itemCatalog, settings ) == GameStateInitResult::Restored );
    SW_EXPECT_FALSE( pRestored->isFreshGame() );
    SW_EXPECT_EQUAL( 2, pRestored->getInventory().getItemCount( seedItem ) );
}

/** @brief [PlayerStateComponentsTest] 가방 슬롯 수는 반드시 적는다 — 기본 설정(슬롯 0)은 "가방 없음" 이 아니라 범위 밖 설정으로 거절한다 */
SW_TEST_CASE( PlayerStateComponentsTest, InventoryRefusesMissingSlotCount )
{
    SW_EXPECT_LOG( "InventoryComponent.RefusedOpenSettings" );
    GameObjectManager   manager;
    InventoryComponent* pBag = manager.createGameObject( hashed_string( "Player" ) )->addComponent<InventoryComponent>();
    SW_REQUIRE_NOT_NULL( pBag );
    SW_EXPECT_TRUE( pBag->initialize( ItemCatalog{}, InventorySettings{} ) == GameStateInitResult::Refused );
    SW_EXPECT_FALSE( pBag->isOpen() );
}

/** @brief [PlayerStateComponentsTest] 퀘스트 일지는 카탈로그를 받아 열고, 열기 전에 받은 일지를 그 카탈로그로 읽는다 */
SW_TEST_CASE( PlayerStateComponentsTest, QuestLogRoundTripsWithItsCatalog )
{
    QuestCatalog catalog;
    SW_REQUIRE_TRUE( catalog.loadFromXMLText( R"(<QuestCatalog>
  <Quest id="intro"><Stage id="talk" next="end"><Objective kind="Talk" target="elder"/></Stage><Stage id="end" complete="true"/></Quest>
</QuestCatalog>)",
                                              "PlayerStateComponentsTest" ) );
    const hashed_string intro( "intro" );
    GameObjectManager   manager;
    QuestLogComponent*  pQuestLog = manager.createGameObject( hashed_string( "Player" ) )->addComponent<QuestLogComponent>();
    SW_REQUIRE_NOT_NULL( pQuestLog );
    SW_REQUIRE_TRUE( pQuestLog->initialize( catalog ) == GameStateInitResult::Fresh );
    (void)pQuestLog->getQuestLog().start( intro, 1 );
    SW_REQUIRE_TRUE( pQuestLog->getQuestLog().getStatus( intro ) == QuestStatus::Active );

    QuestLogComponent* pRestored = manager.createGameObject( hashed_string( "Restored" ) )->addComponent<QuestLogComponent>();
    SW_REQUIRE_NOT_NULL( pRestored );
    pRestored->restoreState( captureState( *pQuestLog ) );
    SW_EXPECT_TRUE( pRestored->initialize( catalog ) == GameStateInitResult::Restored );
    SW_EXPECT_TRUE( pRestored->getQuestLog().getStatus( intro ) == QuestStatus::Active );
}

/** @brief [PlayerStateComponentsTest] 평판은 열린 뒤에 받은 복원 바이트를 바로 적용한다(핫 리로드 · 같은 씬 불러오기) */
SW_TEST_CASE( PlayerStateComponentsTest, ReputationRestoresAfterOpen )
{
    const hashed_string     town( "Town" );
    const ReputationCatalog reputationCatalog{};
    GameObjectManager       manager;
    ReputationComponent*    pReputation = manager.createGameObject( hashed_string( "Player" ) )->addComponent<ReputationComponent>();
    SW_REQUIRE_NOT_NULL( pReputation );
    SW_REQUIRE_TRUE( pReputation->initialize( reputationCatalog ) == GameStateInitResult::Fresh );
    pReputation->getReputation().setValue( town, 40 );

    ReputationComponent* pRestored = manager.createGameObject( hashed_string( "Restored" ) )->addComponent<ReputationComponent>();
    SW_REQUIRE_NOT_NULL( pRestored );
    SW_REQUIRE_TRUE( pRestored->initialize( reputationCatalog ) == GameStateInitResult::Fresh );
    pRestored->restoreState( captureState( *pReputation ) );
    SW_EXPECT_FALSE( pRestored->isFreshGame() );
    SW_EXPECT_EQUAL( 40, pRestored->getReputation().getValue( town ) );
}

/** @brief [PlayerStateComponentsTest] 가방이 없는 조종자는 가방 컴포넌트가 없는 것이다 — 아이템 받는 쪽도 없고, 가방 컴포넌트를 붙이면 그것이 계약을 내보낸다 */
SW_TEST_CASE( PlayerStateComponentsTest, NoBagMeansNoInventoryComponent )
{
    GameObjectManager manager;
    GameObject*       pController = manager.createGameObject( hashed_string( "Controller" ) );
    WalletComponent*  pWallet     = pController->addComponent<WalletComponent>();
    SW_REQUIRE_NOT_NULL( pWallet );
    SW_EXPECT_NULL( PlayerComponentUtil::findOnOwner<InventoryComponent>( *pWallet ) );
    SW_EXPECT_NULL( IItemReceiver::findForTarget( *pController ) );

    InventoryComponent* pBag = pController->addComponent<InventoryComponent>();
    SW_REQUIRE_NOT_NULL( pBag );
    const ItemCatalog itemCatalog{};
    InventorySettings settings;
    settings._slotCount = 1;
    SW_REQUIRE_TRUE( pBag->initialize( itemCatalog, settings ) == GameStateInitResult::Fresh );
    IItemReceiver* const pReceiver = IItemReceiver::findForTarget( *pController );
    SW_REQUIRE_NOT_NULL( pReceiver );
    const hashed_string gem( "Gem" );
    SW_EXPECT_TRUE( pReceiver->receiveItem( gem, 1 ) == ItemGrantResult::Granted );
    SW_EXPECT_EQUAL( 1, pBag->getInventory().getItemCount( gem ) );
    SW_EXPECT_TRUE( pReceiver->receiveItem( hashed_string( "Ore" ), 1 ) == ItemGrantResult::NoRoom ); // 슬롯 하나가 찼다 — 아무것도 넣지 않는다
}

/** @brief [PlayerStateComponentsTest] 플레이어 상태는 폰을 쥔 조종자 오브젝트에서 찾힌다(형제 `PlayerStateComponent` 와 같은 길) */
SW_TEST_CASE( PlayerStateComponentsTest, FoundFromThePawn )
{
    SW_EXPECT_LOG( "Control.AxisActionNotInInputMap" ); // 기본 폰의 Look 축이 빈 입력 맵에 없다 — 이 테스트는 찾는 길만 본다
    GameObjectManager          manager;
    GameObject*                pPawnObject = manager.createGameObject( hashed_string( "Pawn" ) );
    PawnComponent*             pPawn       = pPawnObject->addComponent<PawnComponent>();
    GameObject*                pController = manager.createGameObject( hashed_string( "Controller" ) );
    WalletComponent*           pWallet     = pController->addComponent<WalletComponent>();
    PlayerControllerComponent* pPlayer     = pController->addComponent<PlayerControllerComponent>();
    manager.beginPlay();

    SW_EXPECT_NULL( PawnComponent::findControllerObject( manager, *pPawnObject ) ); // 아직 쥐지 않았다
    pPlayer->possess( *pPawn );
    GameObject* const pFound = PawnComponent::findControllerObject( manager, *pPawnObject );
    SW_REQUIRE_NOT_NULL( pFound );
    SW_EXPECT_TRUE( pFound->getComponent<WalletComponent>() == pWallet );
}

/** @brief [PlayerStateComponentsTest] 씬 하나를 읽는 쪽(`findOpen`: 감시 값 · 치트 · 목표 표시)은 열린 것만 보고, 둘이면 오류로 멈춘다. 어느 플레이어인지 등록 순서로 고르지 않는다 */
SW_TEST_CASE( PlayerStateComponentsTest, SecondOpenComponentStopsTheSingleReader )
{
    SW_EXPECT_LOG( "PlayerComponent.SeveralOpen" );
    GameObjectManager  manager;
    QuestLogComponent* pFirst  = manager.createGameObject( hashed_string( "PlayerA" ) )->addComponent<QuestLogComponent>();
    QuestLogComponent* pSecond = manager.createGameObject( hashed_string( "PlayerB" ) )->addComponent<QuestLogComponent>();
    const QuestCatalog questCatalog{};
    SW_REQUIRE_NOT_NULL( pFirst );
    SW_REQUIRE_NOT_NULL( pSecond );
    SW_EXPECT_NULL( PlayerComponentUtil::findOpen<QuestLogComponent>( &manager ) ); // 아직 아무것도 열지 않았다
    SW_REQUIRE_TRUE( pFirst->initialize( questCatalog ) == GameStateInitResult::Fresh );
    SW_EXPECT_TRUE( PlayerComponentUtil::findOpen<QuestLogComponent>( &manager ) == pFirst );
    SW_REQUIRE_TRUE( pSecond->initialize( questCatalog ) == GameStateInitResult::Fresh );
    SW_EXPECT_NULL( PlayerComponentUtil::findOpen<QuestLogComponent>( &manager ) );
}

/** @brief [PlayerStateComponentsTest] 키트가 선택으로 빌리는 월드 상태는 설정에서 열지 않으면 없다 — 날씨 카탈로그가 없으면 날씨가 nullptr 이다 */
SW_TEST_CASE( PlayerStateComponentsTest, OptionalWorldStateIsNullWhenNotOpened )
{
    GameObjectManager   manager;
    GameStateComponent* pWorld = manager.createGameObject( hashed_string( "Director" ) )->addComponent<GameStateComponent>();
    SW_REQUIRE_NOT_NULL( pWorld );
    SW_REQUIRE_TRUE( pWorld->initialize( GameStateSettings{} ) == GameStateInitResult::Fresh );
    SW_EXPECT_NULL( pWorld->findWeather() );
}

/**
 * @brief [PlayerStateComponentsTest] 스트리밍 셀에 든 오브젝트의 PROPERTY 밖 상태(지갑)는 셀을 내렸다 올려도, 세이브를 지나 비운 매니저에 다시 지어도 남는다
 * @details 세이브의 컴포넌트 저장소(`ComponentStateCodec`)는 컴포넌트 id 로 짝짓는데 셀은 복원 뒤에 지어져 찾을 컴포넌트가 없다 — 떠 둔 엔티티가 함께 싣는다(`ComponentStateCodec`).
 */
SW_TEST_CASE( PlayerStateComponentsTest, StreamingCellKeepsWalletAcrossUnloadAndSave )
{
    SceneDocument doc{};
    doc._name                                        = "Cells";
    doc._sceneSettings._bStreaming                   = true;
    doc._sceneSettings._cellGrid._cellSize           = 10.0f;
    doc._sceneSettings._cellGrid._loadingRadius      = 6.0f;
    doc._sceneSettings._unloadMargin                 = 0.0f;
    doc._sceneSettings._maxCellLoadsPerFrame         = 4;
    doc._sceneSettings._addToWorldBudgetMicroseconds = 1'000'000'000u;
    doc._bValid                                      = true;
    SceneDocument::StreamingCell cell{};
    cell._cell = StreamingCellCoord{ 0, 0 };
    SceneDocument::SceneObjectNode node{};
    node._fileID      = 1;
    node._name        = "Chest";
    node._embeddedXML = "<GameObject _schemaVersion=\"0\" _name=\"Chest\" _bActive=\"true\"><_listComponent><SceneComponent _localID=\"1\" _localPosition=\"2,0,2\" />"
                        "<WalletComponent _localID=\"2\" /></_listComponent></GameObject>";
    cell._listSceneObjectNode.push_back( node );
    doc._listStreamingCell.push_back( std::move( cell ) );

    Scene scene{ "Cells" };
    SW_REQUIRE_TRUE( scene.instantiate( doc ) );
    GameObjectManager&       objects   = *scene.getObjectManager();
    SceneStreamingSubsystem& streaming = objects.getSubsystem<SceneStreamingSubsystem>();
    const auto               moveTo    = [&objects, &streaming]( const float3& position )
    {
        const StreamingSource source{ position, streaming.getLoadingRadius(), StreamingSourcePriority::Normal };
        SW_EXPECT_TRUE( streaming.setExternalSources( span<const StreamingSource>( &source, 1 ) ) );
        streaming.update( 0.0f );
        objects.processDeferredDestruction();
        objects.mergePendingAdds();
    };
    const auto findWallet = [&objects]() -> WalletComponent*
    {
        GameObject* pChest = objects.findGameObjectByName( hashed_string( "Chest" ) );
        return pChest != nullptr ? pChest->getComponent<WalletComponent>() : nullptr;
    };
    const float3 nearPosition{ 2.0f, 0.0f, 2.0f };
    const float3 farPosition{ 500.0f, 0.0f, 500.0f };
    moveTo( nearPosition );
    SW_REQUIRE_NOT_NULL( findWallet() );
    findWallet()->getWallet().add( Wallet::getDefaultCurrency(), 70 );

    moveTo( farPosition );
    SW_REQUIRE_TRUE( findWallet() == nullptr );
    moveTo( nearPosition );
    SW_REQUIRE_NOT_NULL( findWallet() );
    SW_EXPECT_EQUAL( int64{ 70 }, findWallet()->getWallet().getBalance( Wallet::getDefaultCurrency() ) );

    findWallet()->getWallet().add( Wallet::getDefaultCurrency(), 5 );
    vector<uint8> bytes;
    streaming.writeSaveState( bytes ); // 올라 있는 셀을 지금 떠서 싣는다
    SceneStreamingSubsystem::SaveState state;
    SW_REQUIRE_TRUE( SceneStreamingSubsystem::readSaveState( bytes.data(), bytes.size(), state ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, state._listCell.size() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, state._listCell[0]._listNode.size() );
    SW_EXPECT_FALSE( state._listCell[0]._listNode[0]._componentStateBytes.empty() );
    findWallet()->getWallet().add( Wallet::getDefaultCurrency(), 1000 ); // 저장 뒤의 것 — 복원이 덮는다

    objects.clear();
    scene.rebindRestoredFileIDs( {} );
    streaming.applySaveState( std::move( state ) );
    moveTo( nearPosition );
    SW_REQUIRE_NOT_NULL( findWallet() );
    SW_EXPECT_EQUAL( int64{ 75 }, findWallet()->getWallet().getBalance( Wallet::getDefaultCurrency() ) );
}

/**
 * @brief [PlayerStateComponentsTest] 셀 엔티티의 중첩 프리팹 부품 · 플레이 중 붙인 자식도 저마다 떠서, 셀을 내렸다 올리거나 세이브를 지나도 부품의 지갑 · 부품 기록 · 부착이 남고 지운 부품은 되살아나지 않는다
 * @details 예전에는 셀의 엔티티만 떴다 — 부품은 프리팹에서 다시 스폰돼 처음 상태가 되고, 붙인 자식은 부모와 함께 지워졌다.
 */
SW_TEST_CASE( PlayerStateComponentsTest, StreamingCellKeepsNestedPartsAndAttachedChildren )
{
    SceneDocument doc{};
    doc._name                                        = "Parts";
    doc._sceneSettings._bStreaming                   = true;
    doc._sceneSettings._cellGrid._cellSize           = 10.0f;
    doc._sceneSettings._cellGrid._loadingRadius      = 6.0f;
    doc._sceneSettings._unloadMargin                 = 0.0f;
    doc._sceneSettings._maxCellLoadsPerFrame         = 4;
    doc._sceneSettings._addToWorldBudgetMicroseconds = 1'000'000'000u;
    doc._bValid                                      = true;
    SceneDocument::StreamingCell cell{};
    cell._cell = StreamingCellCoord{ 0, 0 };
    SceneDocument::SceneObjectNode node{};
    node._fileID      = 1;
    node._name        = "Cart";
    node._embeddedXML = "<GameObject _schemaVersion=\"0\" _name=\"Cart\" _bActive=\"true\"><_listComponent><SceneComponent _localID=\"1\" _localPosition=\"2,0,2\" /></_listComponent></GameObject>";
    cell._listSceneObjectNode.push_back( node );
    doc._listStreamingCell.push_back( std::move( cell ) );

    Scene scene{ "Parts" };
    SW_REQUIRE_TRUE( scene.instantiate( doc ) );
    GameObjectManager&       objects   = *scene.getObjectManager();
    SceneStreamingSubsystem& streaming = objects.getSubsystem<SceneStreamingSubsystem>();
    PrefabInstanceRegistry&  parts     = objects.getSubsystem<PrefabInstanceRegistry>();
    const auto               moveTo    = [&objects, &streaming]( const float3& position )
    {
        const StreamingSource source{ position, streaming.getLoadingRadius(), StreamingSourcePriority::Normal };
        SW_EXPECT_TRUE( streaming.setExternalSources( span<const StreamingSource>( &source, 1 ) ) );
        streaming.update( 0.0f );
        objects.processDeferredDestruction();
        objects.mergePendingAdds();
    };
    const auto find = [&objects]( const utf8* pName )
    { return objects.findGameObjectByName( hashed_string( pName ) ); };
    const auto addChild = [&objects]( GameObject& parent, const utf8* pName )
    {
        GameObject* pChild = objects.createGameObject( hashed_string( pName ) );
        SW_EXPECT_TRUE( pChild->addComponent<SceneComponent>()->attachToComponent( parent.getPrimarySceneComponent() ) );
        return pChild;
    };
    const auto expectCart = [&]( const int64 balance )
    {
        GameObject* pCart  = find( "Cart" );
        GameObject* pWheel = find( "Wheel" );
        GameObject* pFlag  = find( "Flag" );
        const bool  bFound = pCart != nullptr && pWheel != nullptr && pFlag != nullptr && pWheel->getComponent<WalletComponent>() != nullptr && parts.isAttachedNestedPart( pWheel );
        SW_EXPECT_TRUE( bFound );
        if ( bFound == false )
            return;
        SW_EXPECT_TRUE( pWheel->getParent() == pCart );
        SW_EXPECT_TRUE( pFlag->getParent() == pWheel );
        SW_EXPECT_NULL( find( "Lantern" ) );
        SW_EXPECT_EQUAL( balance, pWheel->getComponent<WalletComponent>()->getWallet().getBalance( Wallet::getDefaultCurrency() ) );
        SW_EXPECT_EQUAL( string( "#00000000000000aa" ), parts.findNestedPart( pWheel->getObjectID() )->_partKey );
        SW_EXPECT_FALSE( parts.isAttachedNestedPart( pFlag ) );
    };
    const float3 nearPosition{ 2.0f, 0.0f, 2.0f };
    const float3 farPosition{ 500.0f, 0.0f, 500.0f };
    moveTo( nearPosition );
    SW_REQUIRE_NOT_NULL( find( "Cart" ) );
    GameObject* pWheel   = addChild( *find( "Cart" ), "Wheel" );
    GameObject* pLantern = addChild( *find( "Cart" ), "Lantern" );
    parts.registerNestedPart( pWheel->getObjectID(), find( "Cart" )->getObjectID(), "prefabs/wheel.prefab.xml", "#00000000000000aa" );
    parts.registerNestedPart( pLantern->getObjectID(), find( "Cart" )->getObjectID(), "prefabs/lantern.prefab.xml", "#00000000000000bb" );
    SW_REQUIRE_NOT_NULL( pWheel->addComponent<WalletComponent>() );
    pWheel->getComponent<WalletComponent>()->getWallet().add( Wallet::getDefaultCurrency(), 70 );
    (void)addChild( *pWheel, "Flag" );
    objects.destroyGameObject( pLantern );
    objects.processDeferredDestruction();
    objects.mergePendingAdds();

    moveTo( farPosition );
    SW_EXPECT_NULL( find( "Cart" ) );
    SW_EXPECT_NULL( find( "Wheel" ) );
    SW_EXPECT_NULL( find( "Flag" ) );
    moveTo( nearPosition );
    expectCart( 70 );

    find( "Wheel" )->getComponent<WalletComponent>()->getWallet().add( Wallet::getDefaultCurrency(), 5 );
    vector<uint8> bytes;
    streaming.writeSaveState( bytes );
    SceneStreamingSubsystem::SaveState state;
    SW_REQUIRE_TRUE( SceneStreamingSubsystem::readSaveState( bytes.data(), bytes.size(), state ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, state._listCell.size() );
    SW_EXPECT_EQUAL( size_t{ 3 }, state._listCell[0]._listNode.size() );
    objects.clear();
    scene.rebindRestoredFileIDs( {} );
    streaming.applySaveState( std::move( state ) );
    moveTo( nearPosition );
    expectCart( 75 );
}
