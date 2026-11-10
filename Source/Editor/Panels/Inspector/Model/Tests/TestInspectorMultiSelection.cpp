#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/Scene/EditorMultiEdit.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Panels/Inspector/Model/InspectorMultiSelection.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Scene/SceneManager.h"

// InspectorMultiSelectionTest — 여러 오브젝트를 고른 인스펙터의 활성 칸 · 공통 아닌 컴포넌트 수 · 혼합 판정. 씬 밖의 지역 매니저로 본다.

namespace
{
    struct InspectorMultiSelectionTestInternal
    {
        static sw::GameObject* spawn( sw::GameObjectManager& manager, const utf8* pName, bool bActive )
        {
            sw::GameObject* pObject = manager.createGameObject( sw::hashed_string( pName ) );
            if ( pObject != nullptr )
                pObject->setActive( bActive );
            return pObject;
        }
    };
} // namespace

/**
 * @brief [InspectorMultiSelectionTest] 활성 칸은 주 선택의 값을 보이고, 값이 다르면 혼합이다 — 주 선택이 어느 쪽이든
 */
SW_TEST_CASE( InspectorMultiSelectionTest, ActiveStateFollowsPrimaryAndMarksMixed )
{
    using Internal = InspectorMultiSelectionTestInternal;
    using Multi    = sw::editor::InspectorMultiSelection;

    sw::GameObjectManager manager;
    sw::GameObject*       pOn      = Internal::spawn( manager, "On", true );
    sw::GameObject*       pOnOther = Internal::spawn( manager, "OnOther", true );
    sw::GameObject*       pOff     = Internal::spawn( manager, "Off", false );
    SW_REQUIRE_NOT_NULL( pOn );
    SW_REQUIRE_NOT_NULL( pOnOther );
    SW_REQUIRE_NOT_NULL( pOff );

    const Multi::ActiveState same = Multi::readActiveState( { pOn, pOnOther } );
    SW_EXPECT_TRUE( same._bActive );
    SW_EXPECT_FALSE( same._bMixed );

    const Multi::ActiveState mixedOn = Multi::readActiveState( { pOn, pOnOther, pOff } );
    SW_EXPECT_TRUE( mixedOn._bActive );
    SW_EXPECT_TRUE( mixedOn._bMixed );

    const Multi::ActiveState mixedOff = Multi::readActiveState( { pOff, pOn } );
    SW_EXPECT_FALSE( mixedOff._bActive );
    SW_EXPECT_TRUE( mixedOff._bMixed );

    const Multi::ActiveState empty = Multi::readActiveState( {} );
    SW_EXPECT_FALSE( empty._bActive );
    SW_EXPECT_FALSE( empty._bMixed );
}

/**
 * @brief [InspectorMultiSelectionTest] 활성 칸은 자기 비트다 — 꺼진 부모 아래의 켜진 자식과 켜진 오브젝트는 혼합이 아니다
 */
SW_TEST_CASE( InspectorMultiSelectionTest, ActiveStateReadsTheSelfBitNotTheHierarchy )
{
    using Internal = InspectorMultiSelectionTestInternal;
    using Multi    = sw::editor::InspectorMultiSelection;

    sw::GameObjectManager manager;
    sw::GameObject*       pParent = Internal::spawn( manager, "Parent", false );
    sw::GameObject*       pChild  = Internal::spawn( manager, "Child", true );
    sw::GameObject*       pOther  = Internal::spawn( manager, "Other", true );
    SW_REQUIRE_NOT_NULL( pParent );
    SW_REQUIRE_NOT_NULL( pChild );
    SW_REQUIRE_NOT_NULL( pOther );
    (void)pParent->addComponent<sw::SceneComponent>(); // 부모 붙이기는 장면 컴포넌트끼리의 일이다
    (void)pChild->addComponent<sw::SceneComponent>();
    SW_REQUIRE_TRUE( pChild->attachToParent( pParent ) );
    SW_REQUIRE_FALSE( pChild->isActiveInHierarchy() );

    const Multi::ActiveState state = Multi::readActiveState( { pChild, pOther } );
    SW_EXPECT_TRUE( state._bActive );
    SW_EXPECT_FALSE( state._bMixed );
}

/**
 * @brief [InspectorMultiSelectionTest] 활성 적용은 모두에게 입히고 바뀐 것만 되돌리기 한 번으로 묶는다 — 이미 그 값이면 기록이 없다
 * @details 에디터 안에서 돌므로 메인 스택을 더럽히지 않게 지역 스택을 범위에 건다. 기록은 씬 dirty 도 표시하므로 끝에 되돌린다. 되돌리기 실행은 씬의 오브젝트를 찾으므로
 *          여기서 하지 않는다(스냅샷 복원은 시나리오 `editor/multiedit` 의 길이다).
 */
SW_TEST_CASE( InspectorMultiSelectionTest, ApplyActiveGroupsTheChangedObjectsIntoOneUndo )
{
    using Internal = InspectorMultiSelectionTestInternal;
    using Multi    = sw::editor::InspectorMultiSelection;

    SW_REQUIRE_NOT_NULL( sw::editor::findSubsystem<sw::SceneManager>() ); // 없으면 기록이 남지 않아 아래가 뜻이 없다
    sw::editor::EditorContext* pContext = sw::editor::findSubsystem<sw::editor::EditorContext>();
    SW_REQUIRE_NOT_NULL( pContext );
    sw::editor::EditorWorkspace& workspace = pContext->getSubsystem<sw::editor::EditorWorkspace>();
    const bool                   bWasDirty = workspace.isSceneDirty();

    sw::CommandStack   stack;
    sw::SubsystemScope scope;
    SW_REQUIRE_TRUE( scope.bind( &stack ) );
    {
        const sw::editor::ScopedEditorScope scopedEditor( scope );

        sw::GameObjectManager manager;
        sw::GameObject*       pFirst  = Internal::spawn( manager, "First", true );
        sw::GameObject*       pSecond = Internal::spawn( manager, "Second", false );
        sw::GameObject*       pThird  = Internal::spawn( manager, "Third", true );
        SW_REQUIRE_NOT_NULL( pFirst );
        SW_REQUIRE_NOT_NULL( pSecond );
        SW_REQUIRE_NOT_NULL( pThird );
        const sw::vector<sw::GameObject*> listObject{ pFirst, pSecond, pThird };

        // 이미 꺼진 Second 는 기록하지 않는다 — 안쪽 명령은 둘, 스택에는 하나
        Multi::applyActive( listObject, false );
        SW_EXPECT_FALSE( pFirst->isActive() );
        SW_EXPECT_FALSE( pSecond->isActive() );
        SW_EXPECT_FALSE( pThird->isActive() );
        SW_REQUIRE_EQUAL( stack.getCommandCount(), size_t{ 1 } );
        const sw::CommandStack::Command& group = stack.getCommand( 0 );
        SW_EXPECT_TRUE( group._label == "Set Active (multiple)" );
        SW_REQUIRE_NOT_NULL( group._listChild.get() );
        SW_EXPECT_EQUAL( group._listChild->size(), size_t{ 2 } );

        // 모두 이미 그 값이면 기록이 늘지 않는다
        Multi::applyActive( listObject, false );
        SW_EXPECT_EQUAL( stack.getCommandCount(), size_t{ 1 } );

        // 하나만 바뀌어도 이름은 묶음의 것이다
        pSecond->setActive( true );
        Multi::applyActive( listObject, false );
        SW_EXPECT_FALSE( pSecond->isActive() );
        SW_REQUIRE_EQUAL( stack.getCommandCount(), size_t{ 2 } );
        SW_EXPECT_TRUE( stack.getCommand( 1 )._label == "Set Active (multiple)" );
    }
    if ( bWasDirty == false )
        workspace.clearSceneDirty();
}

/**
 * @brief [InspectorMultiSelectionTest] 공통 아닌 컴포넌트 수 — 결함: 공통 타입의 두 번째 컴포넌트까지 "모든 오브젝트에 있지 않다" 로 셌다
 */
SW_TEST_CASE( InspectorMultiSelectionTest, UnsharedCountIgnoresSecondComponentOfASharedType )
{
    using Internal = InspectorMultiSelectionTestInternal;
    using Multi    = sw::editor::InspectorMultiSelection;

    sw::GameObjectManager manager;
    sw::GameObject*       pFirst  = Internal::spawn( manager, "First", true );
    sw::GameObject*       pSecond = Internal::spawn( manager, "Second", true );
    SW_REQUIRE_NOT_NULL( pFirst );
    SW_REQUIRE_NOT_NULL( pSecond );
    const sw::vector<sw::GameObject*>                listObject{ pFirst, pSecond };
    sw::vector<sw::editor::EditorMultiEditComponent> listCommon;

    // 둘 다 같은 타입을 둘씩 가졌다 — 공통 하나, 공통 아닌 것 없음
    SW_REQUIRE_NOT_NULL( pFirst->addComponent<sw::TagComponent>() );
    SW_REQUIRE_NOT_NULL( pFirst->addComponent<sw::TagComponent>() );
    SW_REQUIRE_NOT_NULL( pSecond->addComponent<sw::TagComponent>() );
    SW_REQUIRE_NOT_NULL( pSecond->addComponent<sw::TagComponent>() );
    sw::editor::EditorMultiEditComponent::collectCommonComponents( listObject, listCommon );
    SW_REQUIRE_EQUAL( listCommon.size(), size_t{ 1 } );
    SW_EXPECT_EQUAL( Multi::countUnsharedComponents( *pFirst, listCommon ), size_t{ 0 } );

    // 주 선택에만 있는 타입은 센다. 주 선택에 없는 타입은 세지 않는다
    SW_REQUIRE_NOT_NULL( pFirst->addComponent<sw::CameraComponent>() );
    sw::editor::EditorMultiEditComponent::collectCommonComponents( listObject, listCommon );
    SW_REQUIRE_EQUAL( listCommon.size(), size_t{ 1 } );
    SW_EXPECT_EQUAL( Multi::countUnsharedComponents( *pFirst, listCommon ), size_t{ 1 } );
    SW_EXPECT_EQUAL( Multi::countUnsharedComponents( *pSecond, listCommon ), size_t{ 0 } );
}

/**
 * @brief [InspectorMultiSelectionTest] 혼합 판정 — 값이 같으면 아니고 하나라도 다르면 혼합이다. 컴포넌트가 하나면 혼합이 아니다
 */
SW_TEST_CASE( InspectorMultiSelectionTest, MixedValuesCompareTheComponentsProperty )
{
    using Internal = InspectorMultiSelectionTestInternal;
    using Multi    = sw::editor::InspectorMultiSelection;

    const sw::TypeInfo* pType = sw::CameraComponent::StaticType();
    SW_REQUIRE_NOT_NULL( pType );
    const sw::PropertyInfo* pPriority = pType->findPropertyInHierarchy( sw::hashed_string( "_priority" ) );
    SW_REQUIRE_NOT_NULL( pPriority );

    sw::GameObjectManager manager;
    sw::GameObject*       pFirst  = Internal::spawn( manager, "First", true );
    sw::GameObject*       pSecond = Internal::spawn( manager, "Second", true );
    SW_REQUIRE_NOT_NULL( pFirst );
    SW_REQUIRE_NOT_NULL( pSecond );
    sw::CameraComponent* pFirstCamera  = pFirst->addComponent<sw::CameraComponent>();
    sw::CameraComponent* pSecondCamera = pSecond->addComponent<sw::CameraComponent>();
    SW_REQUIRE_NOT_NULL( pFirstCamera );
    SW_REQUIRE_NOT_NULL( pSecondCamera );

    const sw::vector<sw::Component*> listComponent{ pFirstCamera, pSecondCamera };
    SW_EXPECT_FALSE( Multi::hasMixedValues( *pPriority, listComponent ) );
    pSecondCamera->setPriority( pFirstCamera->getPriority() + 1 );
    SW_EXPECT_TRUE( Multi::hasMixedValues( *pPriority, listComponent ) );
    SW_EXPECT_FALSE( Multi::hasMixedValues( *pPriority, { pFirstCamera } ) );
}
