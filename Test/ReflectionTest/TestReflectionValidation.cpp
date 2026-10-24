#include "pch.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/GameObject/Serialization/ObjectValidation.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionValidation.h"

#include "ReflectionTest/TestSampleActor.h"

#include "TestFramework/TestFramework.h"

// 검증 함수 — PROPERTY( Validate = fn ) · REFLECT( Validate = fn ) 를 돌려 결과를 모으는 길.

namespace
{
    /** @brief 결과 중 메시지에 @p pText 가 든 것의 수입니다. */
    uint32 countIssues( const sw::vector<sw::ValidationIssue>& listIssue, const utf8* pText )
    {
        uint32 count = 0;
        for ( const sw::ValidationIssue& issue : listIssue )
        {
            if ( issue._message.find( pText ) != sw::string::npos )
                ++count;
        }
        return count;
    }
} // namespace

/**
 * @brief [ReflectionValidationTest] 프로퍼티 · 타입 검증이 돌고, 값 구조체와 그 시퀀스의 원소까지 내려간다 — 결과는 타입 · 프로퍼티 이름을 단다
 */
SW_TEST_CASE( ReflectionValidationTest, ValidatorsRunOnPropertiesTypesAndNestedValues )
{
    const sw::TypeInfo* pType = sw::ValidatedActor::StaticType();
    SW_ASSERT_NOT_NULL( pType );
    SW_EXPECT_TRUE( sw::ReflectionValidation::hasValidator( *pType ) );

    sw::ValidatedActor clean;
    clean._listPart.resize( 2 );
    sw::ValidationContext cleanContext;
    SW_EXPECT_EQUAL( 0u, sw::ReflectionValidation::validateObject( *pType, &clean, cleanContext ) );

    sw::ValidatedActor broken;
    broken._name         = "";
    broken._min          = 20;
    broken._part._weight = -1.0f;
    broken._listPart.resize( 3 );
    broken._listPart[2]._weight = -5.0f;
    sw::ValidationContext context;
    context.setSource( 7, "BrokenActor" );
    SW_EXPECT_EQUAL( 4u, sw::ReflectionValidation::validateObject( *pType, &broken, context ) );
    SW_EXPECT_TRUE( context.hasError() );

    const sw::vector<sw::ValidationIssue>& listIssue = context.getIssues();
    SW_EXPECT_EQUAL( 1u, countIssues( listIssue, "name is empty" ) );
    SW_EXPECT_EQUAL( 2u, countIssues( listIssue, "weight is negative" ) ); // 값 구조체 하나 + 시퀀스 원소 하나
    SW_EXPECT_EQUAL( 1u, countIssues( listIssue, "min is greater than max" ) );
    for ( const sw::ValidationIssue& issue : listIssue )
    {
        SW_EXPECT_EQUAL( sw::string( "BrokenActor" ), issue._sourceLabel );
        if ( issue._message == "name is empty" )
        {
            SW_EXPECT_TRUE( issue._severity == sw::ValidationSeverity::Warning );
            SW_EXPECT_TRUE( issue._typeName == sw::hashed_string( "sw::ValidatedActor" ) );
            SW_EXPECT_TRUE( issue._propertyName == sw::hashed_string( "_name" ) );
        }
        if ( issue._message == "min is greater than max" )
            SW_EXPECT_TRUE( issue._propertyName.empty() );
    }

    // 검증 함수는 상속된다
    const sw::TypeInfo* pChild = sw::ValidatedChildActor::StaticType();
    SW_ASSERT_NOT_NULL( pChild );
    sw::ValidatedChildActor child;
    child._min = 99;
    sw::ValidationContext childContext;
    SW_EXPECT_EQUAL( 1u, sw::ReflectionValidation::validateObject( *pChild, &child, childContext ) );

    // 검증 함수가 없는 타입은 돌 것이 없다
    const sw::TypeInfo* pPlain = sw::SampleTestActor::StaticType();
    SW_ASSERT_NOT_NULL( pPlain );
    SW_EXPECT_FALSE( sw::ReflectionValidation::hasValidator( *pPlain ) );
}

/**
 * @brief [ReflectionValidationTest] 오브젝트 로드 · 글 저장이 검증해 결과를 `ValidationIssueLog` 에 두고, 고치면 그 오브젝트의 결과가 바뀐다
 */
SW_TEST_CASE( ReflectionValidationTest, LoadSaveAndEditReportToTheIssueLog )
{
    sw::ValidationIssueLog::get().clear();
    sw::GameObjectManager manager;
    sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "SlowMover" ) );
    SW_ASSERT_NOT_NULL( pObject );
    sw::ValidatedComponent* pComp = pObject->addComponent<sw::ValidatedComponent>();
    SW_ASSERT_NOT_NULL( pComp );
    pComp->_speed = -2.0f;

    sw::vector<sw::ValidationIssue> listIssue;
    {
        SW_TEST_DEFENSIVE_SCOPE( "validation warning while saving a component with a negative speed" );
        const sw::string state = sw::ObjectStateSerializer::saveToXMLString( pObject, sw::ObjectSaveOptions{ sw::SerializePurpose::Authoring } ); // 저장 — 결과를 남기고 저장은 한다
        SW_EXPECT_FALSE( state.empty() );
        sw::ValidationIssueLog::get().collectIssues( listIssue );
        SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listIssue.size() );
        SW_EXPECT_EQUAL( pObject->getObjectID(), listIssue[0]._sourceID );
        SW_EXPECT_EQUAL( sw::string( "SlowMover" ), listIssue[0]._sourceLabel );
        SW_EXPECT_TRUE( listIssue[0]._severity == sw::ValidationSeverity::Error );

        // 로드 — 묶음이 값을 다 읽은 뒤 검증한다
        sw::GameObject* pLoaded = manager.createGameObject( sw::hashed_string( "LoadedMover" ) );
        SW_ASSERT_NOT_NULL( pLoaded );
        sw::ObjectStateBatch  batch( sw::ObjectIDSpace::Live );
        sw::ObjectLoadContext context{ sw::SerializePurpose::Snapshot };
        context._pBatch = &batch;
        SW_ASSERT_TRUE( sw::ObjectStateSerializer::loadFromXMLString( pLoaded, state, context ) );
        SW_EXPECT_TRUE( batch.finish() );
        sw::ValidationIssueLog::get().collectIssues( listIssue );
        SW_EXPECT_EQUAL( 2u, static_cast<uint32>( listIssue.size() ) );
    }

    // 고치고 다시 검증(인스펙터 편집 길) — 그 오브젝트의 결과만 사라진다
    pComp->_speed = 3.0f;
    SW_EXPECT_EQUAL( 0u, sw::ObjectValidation::reportGameObject( *pObject, false ) );
    sw::ValidationIssueLog::get().collectIssues( listIssue );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listIssue.size() );
    SW_EXPECT_TRUE( listIssue[0]._sourceID != pObject->getObjectID() );
    sw::ValidationIssueLog::get().clear();
}

/**
 * @brief [ReflectionValidationTest] 오브젝트가 파괴되면 그 결과가 표에서 빠지고, 매니저(씬)가 내려가면 남은 오브젝트의 결과도 빠진다
 * @details 결과는 오브젝트 ID 로 들어간다. 오브젝트가 사라진 뒤에도 남으면 맵 검사 패널이 없는 오브젝트의 줄을 보인다.
 */
SW_TEST_CASE( ReflectionValidationTest, DestroyedObjectLeavesTheIssueLog )
{
    sw::ValidationIssueLog::get().clear();
    sw::vector<sw::ValidationIssue> listIssue;
    {
        sw::GameObjectManager manager;
        sw::GameObject*       pDestroyed = manager.createGameObject( sw::hashed_string( "DestroyedMover" ) );
        sw::GameObject*       pKept      = manager.createGameObject( sw::hashed_string( "KeptMover" ) );
        SW_ASSERT_NOT_NULL( pDestroyed );
        SW_ASSERT_NOT_NULL( pKept );
        sw::ValidatedComponent* pDestroyedComp = pDestroyed->addComponent<sw::ValidatedComponent>();
        sw::ValidatedComponent* pKeptComp      = pKept->addComponent<sw::ValidatedComponent>();
        SW_ASSERT_NOT_NULL( pDestroyedComp );
        SW_ASSERT_NOT_NULL( pKeptComp );
        pDestroyedComp->_speed = -1.0f;
        pKeptComp->_speed      = -1.0f;
        SW_EXPECT_EQUAL( 1u, sw::ObjectValidation::reportGameObject( *pDestroyed, false ) );
        SW_EXPECT_EQUAL( 1u, sw::ObjectValidation::reportGameObject( *pKept, false ) );
        SW_EXPECT_EQUAL( 2u, sw::ValidationIssueLog::get().getIssueCount() );

        const uint64 keptID = pKept->getObjectID();
        manager.destroyObject( pDestroyed );
        manager.processDeferredDestruction();
        sw::ValidationIssueLog::get().collectIssues( listIssue );
        SW_ASSERT_TRUE_MSG( listIssue.size() == 1, "The result of the destroyed object remains in the table" );
        SW_EXPECT_EQUAL( keptID, listIssue[0]._sourceID );
    }
    SW_EXPECT_TRUE_MSG( sw::ValidationIssueLog::get().getIssueCount() == 0, "매니저가 내려간 뒤에도 그 오브젝트의 결과가 표에 남았습니다" );
    sw::ValidationIssueLog::get().clear();
}
