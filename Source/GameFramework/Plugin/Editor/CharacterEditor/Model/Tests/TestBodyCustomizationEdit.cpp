#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/AppearanceEditModel.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/Model/BodyCustomizationViewModel.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceDatabase.h"

using namespace sw;
using namespace sw::editor;

// BodyCustomizationEditTest — 캐릭터 편집 창 몸 탭의 명령: 피부 층 더하기(종류의 기본 색) · 순서 · 숨김 · 지우기와 슬롯 숨김이 되돌리기 한 단계씩이고 미저장이 된다, ViewModel 이 층 행을 모은다(nogpu).

namespace
{
    struct BodyCustomizationEditTest
    {
        AppearanceDatabase  _database;
        AppearanceEditModel _model;

        BodyCustomizationEditTest()
            : _database{}
            , _model{}
        {
            SW_REQUIRE_TRUE( _database.loadSectionFromXMLText( R"(<SkinLayerCatalog>
                <SkinLayer id="lipstick" category="Makeup" mask="lips.png" defaultColor="0.7 0.1 0.1 1" defaultOpacity="0.8"/>
                <SkinLayer id="rose" category="Tattoo" mask="rose.png" placeable="true" smearResistance="1"/>
            </SkinLayerCatalog>)",
                                                               "test" ) );
            AppearanceSelection   selection;
            AppearanceSlotRequest head;
            head._slot   = hashed_string( "Head" );
            head._itemID = hashed_string( "helm" );
            selection._listSlot.push_back( head );
            _model.setSelection( selection );
        }
    };
} // namespace

/**
 * @brief [BodyCustomizationEditTest] 층을 더하면 종류의 기본 색 · 불투명도로 맨 위에 놓이고, 되돌리면 사라진다
 */
SW_TEST_CASE_F( BodyCustomizationEditTest, AddLayerUsesDefaultsAndUndoes )
{
    SW_REQUIRE_TRUE( BodyCustomizationViewModel::addSkinLayer( _model, _database, hashed_string( "lipstick" ) ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, _model.getSelection()._listSkinLayer.size() );
    SW_EXPECT_NEAR( 0.8f, _model.getSelection()._listSkinLayer[0]._opacity, 1e-6f );
    SW_EXPECT_NEAR( 0.7f, _model.getSelection()._listSkinLayer[0]._color._x, 1e-6f );
    SW_EXPECT_TRUE( _model.isDirty() );
    SW_EXPECT_FALSE( BodyCustomizationViewModel::addSkinLayer( _model, _database, hashed_string( "unknown" ) ) );
    SW_REQUIRE_TRUE( _model.undo() );
    SW_EXPECT_TRUE( _model.getSelection()._listSkinLayer.empty() );
    SW_EXPECT_FALSE( _model.isDirty() );
}

/**
 * @brief [BodyCustomizationEditTest] 순서 바꾸기 · 숨김 · 지우기와 슬롯 숨김, 범위 밖 명령은 아무것도 하지 않는다
 */
SW_TEST_CASE_F( BodyCustomizationEditTest, ReorderHideRemoveAndHideSlot )
{
    (void)BodyCustomizationViewModel::addSkinLayer( _model, _database, hashed_string( "lipstick" ) );
    (void)BodyCustomizationViewModel::addSkinLayer( _model, _database, hashed_string( "rose" ) );
    SW_REQUIRE_TRUE( BodyCustomizationViewModel::moveSkinLayer( _model, 1, -1 ) );
    SW_EXPECT_TRUE( _model.getSelection()._listSkinLayer[0]._layer == hashed_string( "rose" ) );
    SW_EXPECT_FALSE( BodyCustomizationViewModel::moveSkinLayer( _model, 0, -1 ) );

    SkinLayerValue hidden = _model.getSelection()._listSkinLayer[1];
    hidden._bHidden       = SW_TRUE;
    SW_REQUIRE_TRUE( BodyCustomizationViewModel::setSkinLayer( _model, 1, hidden, false ) );
    SW_EXPECT_EQUAL( SW_TRUE, _model.getSelection()._listSkinLayer[1]._bHidden );
    SW_EXPECT_FALSE( BodyCustomizationViewModel::setSkinLayer( _model, 1, hidden, false ) ); // 같은 값은 기록하지 않는다

    SW_REQUIRE_TRUE( BodyCustomizationViewModel::setSlotHidden( _model, hashed_string( "Head" ), true ) );
    SW_EXPECT_EQUAL( SW_TRUE, _model.getSelection()._listSlot[0]._bHiddenByPlayer );
    SW_EXPECT_FALSE( BodyCustomizationViewModel::setSlotHidden( _model, hashed_string( "Feet" ), true ) );

    SW_REQUIRE_TRUE( BodyCustomizationViewModel::removeSkinLayer( _model, 0 ) );
    SW_EXPECT_FALSE( BodyCustomizationViewModel::removeSkinLayer( _model, 5 ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, _model.getSelection()._listSkinLayer.size() );

    BodyCustomizationViewState state;
    BodyCustomizationViewModel::gather( _model, _database, nullptr, state );
    SW_REQUIRE_EQUAL( size_t{ 1 }, state._listLayer.size() );
    SW_EXPECT_TRUE( state._listLayer[0]._category == hashed_string( "Makeup" ) );
    SW_EXPECT_EQUAL( size_t{ 2 }, state._listAvailableLayer.size() );
    SW_EXPECT_EQUAL( SW_FALSE, state._bHasBody );
}
