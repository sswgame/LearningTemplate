#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Panels/Inspector/Model/PropertyGridModel.h"
#include "Editor/Panels/Inspector/Model/PropertyGridViewState.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/TagComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

// PropertyGridModelTest — 프로퍼티 그리드 대상이 값(범위 · 오브젝트 id · 컴포넌트 id)이라 그리는 사이 사라진 대상은 묶이지 않고, 씬 밖 대상은 주소 그대로 묶인다.
// 값 행은 사본을 들고 명령은 행 번호로 쓰며, 화면 밖 행은 높이만 낸다.

/**
 * @brief [PropertyGridModelTest] 씬 밖 대상은 인스턴스 · 타입 그대로 묶이고, 대상 자신(즐겨찾기 키의 주인)은 씬 대상만이다
 */
SW_TEST_CASE( PropertyGridModelTest, FreeTargetBindsItsInstanceAndIsNotAFavoriteOwner )
{
    sw::GameObjectManager manager;
    sw::GameObject*       pObject = manager.createGameObject( sw::hashed_string( "Owner" ) );
    SW_REQUIRE_NOT_NULL( pObject );
    sw::TagComponent* pTag = pObject->addComponent<sw::TagComponent>();
    SW_REQUIRE_NOT_NULL( pTag );

    sw::editor::PropertyGridModel        model;
    sw::editor::EditorPropertyGridTarget target{};
    target._pInstance = pTag;
    target._pType     = pTag->getTypeInfo();
    SW_REQUIRE_TRUE( model.beginTarget( target ) );
    SW_EXPECT_TRUE( model.getInstance() == static_cast<void*>( pTag ) );
    SW_EXPECT_TRUE( model.getType() == pTag->getTypeInfo() );
    SW_EXPECT_FALSE( model.isTargetInstance( pTag ) );
    SW_EXPECT_FALSE( model.isMultiEdit() );
    SW_EXPECT_TRUE( model.findEditOwnerManager() == nullptr );
    model.endTarget();
    SW_EXPECT_TRUE( model.getInstance() == nullptr );
}

/**
 * @brief [PropertyGridModelTest] 인스턴스나 타입이 빈 씬 밖 대상, 찾을 수 없는 씬 대상은 묶이지 않는다 — 매달린 포인터 대신 그리지 않는다
 */
SW_TEST_CASE( PropertyGridModelTest, MissingTargetDoesNotBind )
{
    sw::editor::PropertyGridModel        model;
    sw::editor::EditorPropertyGridTarget empty{};
    SW_EXPECT_FALSE( model.beginTarget( empty ) );
    SW_EXPECT_TRUE( model.getInstance() == nullptr );

    // 어떤 씬에도 없는 id — 미리보기 범위는 겨눈 미리보기가 없으면 씬이 없다.
    const sw::editor::EditorPropertyGridTarget gone = sw::editor::EditorPropertyGridTarget::makeSceneTarget( sw::editor::EditorPropertyGridScope::FocusedPreview, 0xFFFFFFFF0ull, 0 );
    SW_EXPECT_FALSE( model.beginTarget( gone ) );
    SW_EXPECT_TRUE( model.getInstance() == nullptr );
    SW_EXPECT_TRUE( sw::editor::PropertyGridModel::findSceneObject( sw::editor::EditorPropertyGridScope::None, 1 ) == nullptr );
}

namespace
{
    struct PropertyGridModelTestInternal
    {
        /** @brief 이름이 @p pName 인 깊이 0 프로퍼티 행(화면 밖 행 포함)입니다. 없으면 nullptr 입니다. */
        static const sw::editor::PropertyValueRow* findRow( const sw::editor::PropertyGridViewState& state, const utf8* pName )
        {
            const sw::hashed_string name( pName );
            for ( uint32 index = 0; index < state._rowCount; ++index )
            {
                const sw::editor::PropertyValueRow& row = state._listRow[index];
                if ( row._depth == 0 && row._kind != sw::editor::PropertyRowKind::Category && row._propName == name )
                    return &row;
            }
            return nullptr;
        }
    };
} // namespace

/**
 * @brief [PropertyGridModelTest] 값 행은 멤버 값의 사본을 들고, 명령은 행 번호로 쓴다 — 숫자는 허용 범위(`Max`)로 잘리고 대상에 한 번 알린다
 */
SW_TEST_CASE( PropertyGridModelTest, RowsCarryValueCopiesAndCommandsWriteByRowID )
{
    using Internal = PropertyGridModelTestInternal;
    sw::CameraRenderOutput output{}; // 씬 밖 대상(환경설정 · 문서와 같은 길)

    uint32                               editCount = 0;
    sw::editor::PropertyGridModel        model;
    sw::editor::EditorPropertyGridTarget target{};
    target._pInstance = &output;
    target._pType     = sw::editor::PropertyGridModel::getTypeRegistry().findType<sw::CameraRenderOutput>();
    SW_REQUIRE_NOT_NULL( target._pType );
    target._onEdited = SW_DELEGATE_LAMBDA( sw::Delegate<void( const sw::PropertyInfo& )>, [&editCount]( const sw::PropertyInfo& )
    { ++editCount; } );
    SW_REQUIRE_TRUE( model.beginTarget( target ) );

    sw::editor::PropertyGridGatherParams params{};
    params._window._bSkipOffscreen = SW_FALSE;
    sw::editor::PropertyGridViewState state;
    model.gather( params, state );
    const sw::editor::PropertyValueRow* pRow = Internal::findRow( state, "_renderTextureWidth" );
    SW_REQUIRE_NOT_NULL( pRow );
    SW_EXPECT_TRUE( pRow->_kind == sw::editor::PropertyRowKind::Builtin );
    SW_EXPECT_TRUE( pRow->_widget == sw::editor::InspectorValueWidget::Number );
    SW_REQUIRE_NOT_NULL( pRow->_value.getPtr<uint32>() );
    SW_EXPECT_EQUAL( 256u, *pRow->_value.getPtr<uint32>() );

    // `Max = 8192` — 직접 친 값이 범위 밖이면 모델이 자른다.
    model.setRowNumber( pRow->_rowID, 100000.0 );
    SW_EXPECT_EQUAL( 1u, editCount );
    SW_EXPECT_EQUAL( 8192u, output._renderTextureWidth );
    model.gather( params, state );
    pRow = Internal::findRow( state, "_renderTextureWidth" );
    SW_REQUIRE_NOT_NULL( pRow );
    SW_EXPECT_EQUAL( 8192u, *pRow->_value.getPtr<uint32>() );

    // 체크박스 사본을 고쳐 돌려준다.
    const sw::editor::PropertyValueRow* pShadows = Internal::findRow( state, "_bShadows" );
    SW_REQUIRE_NOT_NULL( pShadows );
    SW_REQUIRE_NOT_NULL( pShadows->_value.getPtr<bool>() );
    SW_EXPECT_TRUE( *pShadows->_value.getPtr<bool>() );
    model.setRowValue( pShadows->_rowID, sw::TaskValue{ false } );
    SW_EXPECT_EQUAL( 2u, editCount );
    SW_EXPECT_FALSE( output._bShadows );
    model.gather( params, state );
    pShadows = Internal::findRow( state, "_bShadows" );
    SW_REQUIRE_NOT_NULL( pShadows );
    SW_EXPECT_FALSE( *pShadows->_value.getPtr<bool>() );
    model.endTarget();
}

/**
 * @brief [PropertyGridModelTest] 보이는 영역 밖의 행은 값을 채우지 않고 높이만 낸다 — 그 행 번호로 부른 명령은 아무것도 쓰지 않는다
 */
SW_TEST_CASE( PropertyGridModelTest, OffscreenRowsCarryHeightOnly )
{
    using Internal = PropertyGridModelTestInternal;
    sw::CameraRenderOutput output{}; // 씬 밖 대상(환경설정 · 문서와 같은 길)

    uint32                               editCount = 0;
    sw::editor::PropertyGridModel        model;
    sw::editor::EditorPropertyGridTarget target{};
    target._pInstance = &output;
    target._pType     = sw::editor::PropertyGridModel::getTypeRegistry().findType<sw::CameraRenderOutput>();
    SW_REQUIRE_NOT_NULL( target._pType );
    target._onEdited = SW_DELEGATE_LAMBDA( sw::Delegate<void( const sw::PropertyInfo& )>, [&editCount]( const sw::PropertyInfo& )
    { ++editCount; } );
    SW_REQUIRE_TRUE( model.beginTarget( target ) );

    // 그리드는 0 에서 시작하고 보이는 영역은 한참 아래다 — 어떤 행도 닿지 않는다.
    sw::editor::PropertyGridGatherParams params{};
    params._window._originY      = 0.0f;
    params._window._clipMinY     = 100000.0f;
    params._window._clipMaxY     = 100100.0f;
    params._window._rowHeight    = 20.0f;
    params._window._headerHeight = 20.0f;
    sw::editor::PropertyGridViewState state;
    model.gather( params, state );
    SW_EXPECT_EQUAL( 0u, state._filledCount );
    const sw::editor::PropertyValueRow* pRow = Internal::findRow( state, "_renderTextureWidth" );
    SW_REQUIRE_NOT_NULL( pRow );
    SW_EXPECT_TRUE( pRow->_kind == sw::editor::PropertyRowKind::Skipped );
    SW_EXPECT_NEAR( 20.0f, pRow->_height, 0.001f );
    SW_EXPECT_FALSE( pRow->_value.hasValue() );

    model.setRowNumber( pRow->_rowID, 1.0 );
    SW_EXPECT_EQUAL( 0u, editCount );
    SW_EXPECT_EQUAL( 256u, output._renderTextureWidth );
    model.endTarget();
}
