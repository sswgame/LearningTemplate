#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/GameObject/Serialization/ObjectValidation.h"
#include "Engine/Object/GameObject/Serialization/Tests/ValidationTestComponent.h"
#include "Engine/Reflection/ReflectionValidation.h"

// ObjectValidationTest — 오브젝트 로드 · 저장 · 인스펙터 편집이 컴포넌트 검증 함수를 돌려 결과를 월드의 표(`SceneValidationIssues`)에 두는 길.

/**
 * @brief [ObjectValidationTest] 오브젝트 로드 · 글 저장이 검증해 결과를 `ValidationIssueLog` 에 두고, 고치면 그 오브젝트의 결과가 바뀐다
 */
SW_TEST_CASE( ObjectValidationTest, LoadSaveAndEditReportToTheIssueLog )
{
    SW_EXPECT_LOG( "ObjectValidation.Issue" ); // 저장 · 로드가 음수 속도를 경고로 남긴다
    sw::GameObjectManager   manager;
    sw::ValidationIssueLog& issueLog = manager.getSubsystem<sw::SceneValidationIssues>().getLog();
    sw::GameObject*         pObject  = manager.createGameObject( sw::hashed_string( "SlowMover" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    sw::ValidatedTestComponent* pComp = pObject->addComponent<sw::ValidatedTestComponent>();
    SW_REQUIRE_NOT_NULL( pComp );
    pComp->_speed = -2.0f;

    // 저장 — 결과를 남기고 저장은 한다
    sw::vector<sw::ValidationIssue> listIssue;
    const sw::string                state = sw::ObjectStateSerializer::saveToXMLString( pObject, sw::ObjectSaveOptions{ sw::SerializePurpose::Authoring } );
    SW_EXPECT_FALSE( state.empty() );
    issueLog.collectIssues( listIssue );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), listIssue.size() );
    SW_EXPECT_EQUAL( pObject->getObjectID(), listIssue[0]._sourceID );
    SW_EXPECT_EQUAL( sw::string( "SlowMover" ), listIssue[0]._sourceLabel );
    SW_EXPECT_TRUE( listIssue[0]._severity == sw::ValidationSeverity::Error );

    // 로드 — 묶음이 값을 다 읽은 뒤 검증한다
    sw::GameObject* pLoaded = manager.createGameObject( sw::hashed_string( "LoadedMover" ) );
    SW_REQUIRE_NOT_NULL( pLoaded );
    sw::ObjectStateBatch  batch( sw::ObjectIDSpace::Live );
    sw::ObjectLoadContext context{ sw::SerializePurpose::Snapshot };
    context._pBatch = &batch;
    SW_REQUIRE_TRUE( sw::ObjectStateSerializer::loadFromXMLString( pLoaded, state, context ) );
    SW_EXPECT_TRUE( batch.finish() );
    issueLog.collectIssues( listIssue );
    SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listIssue.size() ) );

    // 고치고 다시 검증(인스펙터 편집 길) — 그 오브젝트의 결과만 사라진다
    pComp->_speed = 3.0f;
    SW_EXPECT_EQUAL( 0u, sw::ObjectValidation::reportGameObject( *pObject, false ) );
    issueLog.collectIssues( listIssue );
    SW_REQUIRE_EQUAL( static_cast<size_t>( 1 ), listIssue.size() );
    SW_EXPECT_TRUE( listIssue[0]._sourceID != pObject->getObjectID() );
}

/**
 * @brief [ObjectValidationTest] 오브젝트가 파괴되면 그 결과가 자기 월드의 표에서 빠지고, 다른 월드(씬)의 표에는 결과가 섞이지 않으며, 씬을 비우면 표도 빈다
 * @details 결과는 오브젝트 ID 로 들어간다. 오브젝트가 사라진 뒤에도 남으면 맵 검사 패널이 없는 오브젝트의 줄을 보인다.
 *          표는 월드마다 하나다(`SceneValidationIssues`) — 에디터 미리보기 창 · 씬 둘이 한 표를 나누면 결과가 섞인다.
 */
SW_TEST_CASE( ObjectValidationTest, DestroyedObjectLeavesTheIssueLog )
{
    sw::vector<sw::ValidationIssue> listIssue;
    sw::GameObjectManager           otherWorld;
    sw::GameObjectManager           manager;
    sw::ValidationIssueLog&         issueLog   = manager.getSubsystem<sw::SceneValidationIssues>().getLog();
    sw::GameObject*                 pDestroyed = manager.createGameObject( sw::hashed_string( "DestroyedMover" ) );
    sw::GameObject*                 pKept      = manager.createGameObject( sw::hashed_string( "KeptMover" ) );
    SW_REQUIRE_NOT_NULL( pDestroyed );
    SW_REQUIRE_NOT_NULL( pKept );
    sw::ValidatedTestComponent* pDestroyedComp = pDestroyed->addComponent<sw::ValidatedTestComponent>();
    sw::ValidatedTestComponent* pKeptComp      = pKept->addComponent<sw::ValidatedTestComponent>();
    SW_REQUIRE_NOT_NULL( pDestroyedComp );
    SW_REQUIRE_NOT_NULL( pKeptComp );
    pDestroyedComp->_speed = -1.0f;
    pKeptComp->_speed      = -1.0f;
    SW_EXPECT_EQUAL( 1u, sw::ObjectValidation::reportGameObject( *pDestroyed, false ) );
    SW_EXPECT_EQUAL( 1u, sw::ObjectValidation::reportGameObject( *pKept, false ) );
    SW_EXPECT_EQUAL( 2u, issueLog.getIssueCount() );

    const uint64 keptID = pKept->getObjectID();
    manager.destroyObject( pDestroyed );
    manager.processDeferredDestruction();
    issueLog.collectIssues( listIssue );
    SW_REQUIRE_TRUE_MSG( listIssue.size() == 1, "The result of the destroyed object remains in the table" );
    SW_EXPECT_EQUAL( keptID, listIssue[0]._sourceID );
    SW_EXPECT_TRUE_MSG( otherWorld.getSubsystem<sw::SceneValidationIssues>().getLog().getIssueCount() == 0, "Another world's results leaked into this world's table" );

    manager.clear();
    SW_EXPECT_TRUE_MSG( issueLog.getIssueCount() == 0, "Object results remain in the table after the scene was cleared" );
}
