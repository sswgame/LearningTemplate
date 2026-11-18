// MaterialViewModelTest — 문서 편집 알림은 모델 연산이 이름과 함께 낸다(감사 E-16): 연산 한 번 → 알림 한 번, 이름은 모델이 정한다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"
#include "Editor/Panels/Material/Model/MaterialViewModel.h"

using namespace sw;
using namespace sw::editor;

/** @brief [MaterialViewModelTest] 이름 확정 한 번은 알림 한 번("Edit Material Name")이고, 없는 프로퍼티 쓰기는 알리지 않는다 */
SW_TEST_CASE( MaterialViewModelTest, CommitNameNotifiesOnceWithItsName )
{
    uint32                     count{ 0 };
    string                     lastLabel;
    const DocumentEditRecorder recorder{ DocumentEditRecorder::RecordDelegate::create( [&count, &lastLabel]( string_view label )
    {
        ++count;
        lastLabel = string{ label };
    } ) };
    MaterialViewModel          model;
    model.bindDocumentEditRecorder( recorder );
    model.editNameBuffer() = "Probe";
    model.commitName();
    SW_EXPECT_EQUAL( count, 1u );
    SW_EXPECT_TRUE( lastLabel == "Edit Material Name" );

    MaterialProperty missing{};
    missing._name = "NoSuchProperty";
    SW_EXPECT_FALSE( model.setProperty( missing ) );
    SW_EXPECT_EQUAL( count, 1u );
}
