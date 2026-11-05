#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/AppearanceEditModel.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CustomizationSchema.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    CustomizationParamDef makeSlider( const utf8* pName, const utf8* pSymmetry, float32 maxValue )
    {
        CustomizationParamDef param;
        param._name     = hashed_string( pName );
        param._symmetry = hashed_string( pSymmetry );
        param._kind     = CustomizationKind::Slider;
        param._min      = 0.0f;
        param._max      = maxValue;
        return param;
    }
} // namespace

// 대칭 그룹의 짝은 같은 값으로 함께 바뀌고, 자기 범위로 잘린다. 그룹 밖은 그대로다.
SW_TEST_CASE( AppearanceEditModelTest, SymmetricEditMovesThePartnerWithinItsRange )
{
    const vector<CustomizationParamDef> listParameter = { makeSlider( "earL", "ear", 1.0f ), makeSlider( "earR", "ear", 0.5f ), makeSlider( "nose", "", 1.0f ) };
    AppearanceEditModel                 model;
    model.setNumberSymmetric( listParameter, listParameter[0], 0.8f, false );
    const CustomizationValueSet& values = model.getSelection()._customization;
    SW_REQUIRE_TRUE( values.findValue( hashed_string( "earR" ) ) != nullptr );
    SW_EXPECT_EQUAL( 0.8f, values.findValue( hashed_string( "earL" ) )->_number._x );
    SW_EXPECT_EQUAL( 0.5f, values.findValue( hashed_string( "earR" ) )->_number._x );
    SW_EXPECT_TRUE( values.findValue( hashed_string( "nose" ) ) == nullptr );
}

SW_TEST_CASE( AppearanceEditModelTest, AParamWithoutAGroupHasNoPartner )
{
    const CustomizationParamDef nose = makeSlider( "nose", "", 1.0f );
    const CustomizationParamDef chin = makeSlider( "chin", "", 1.0f );
    SW_EXPECT_FALSE( AppearanceEditModel::isSymmetryPartner( nose, chin ) );
}
