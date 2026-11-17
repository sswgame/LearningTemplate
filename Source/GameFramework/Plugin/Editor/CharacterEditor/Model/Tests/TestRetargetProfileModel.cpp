// RetargetProfileModelTest — 리타깃 프로파일 창의 편집 연산은 모델이 이름과 함께 알린다(감사 E-16): 연산 한 번 → 알림 한 번, 문서 바꾸기(replace)는 알리지 않는다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/RetargetProfileModel.h"

using namespace sw;
using namespace sw::editor;

/** @brief [RetargetProfileModelTest] 같은 이름 짝짓기 켜기 한 번은 알림 한 번("Retarget same names")이고, 문서를 통째로 바꾸는 것은 편집이 아니다 */
SW_TEST_CASE( RetargetProfileModelTest, SetterNotifiesOnceAndReplaceDoesNot )
{
    uint32                     count{ 0 };
    string                     lastLabel;
    const DocumentEditRecorder recorder{ DocumentEditRecorder::RecordDelegate::create( [&count, &lastLabel]( string_view label )
    {
        ++count;
        lastLabel = string{ label };
    } ) };
    RetargetProfileModel       model;
    model.bindDocumentEditRecorder( recorder );
    model.replace( RetargetProfile{} );
    SW_EXPECT_EQUAL( count, 0u );
    model.setMapSameNames( true );
    SW_EXPECT_EQUAL( count, 1u );
    SW_EXPECT_TRUE( lastLabel == "Retarget same names" );
    SW_EXPECT_TRUE( model.getProfile().isMappingSameNames() );
}
