#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/Undo/DocumentEditRecorder.h"
#include "Editor/Panels/AudioEvents/Model/AudioEventEditModel.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    void addEvent( AudioEventEditModel& model, const utf8* pName )
    {
        AudioEventDef event;
        event._name = hashed_string( pName );
        model.editLibrary()._listEvent.push_back( event );
    }

    /** @brief 편집 알림을 세는 창구입니다(문서 패널 대신). */
    struct AudioEventEditModelTestRecorder
    {
        uint32               _count{ 0 };
        DocumentEditRecorder _recorder{ SW_DELEGATE_LAMBDA( DocumentEditRecorder::RecordDelegate, [this]( string_view )
        { ++_count; } ) };
    };
} // namespace

SW_TEST_CASE( AudioEventEditModelTest, RemoveSelectsTheFirstEventAndNotifiesTheEdit )
{
    AudioEventEditModelTestRecorder recorder;
    AudioEventEditModel             model{ recorder._recorder };
    addEvent( model, "a" );
    addEvent( model, "b" );
    model.selectEvent( hashed_string( "b" ) );
    SW_EXPECT_TRUE( model.removeEvent() );
    SW_EXPECT_TRUE( model.getEventName() == hashed_string( "a" ) );
    SW_EXPECT_EQUAL( recorder._count, 1u );
}

SW_TEST_CASE( AudioEventEditModelTest, RemovingAnUnknownEventChangesNothing )
{
    AudioEventEditModelTestRecorder recorder;
    AudioEventEditModel             model{ recorder._recorder };
    addEvent( model, "a" );
    model.selectEvent( hashed_string( "missing" ) );
    SW_EXPECT_FALSE( model.removeEvent() );
    SW_EXPECT_EQUAL( recorder._count, 0u );
}

SW_TEST_CASE( AudioEventEditModelTest, DuplicateSelectsTheCopy )
{
    AudioEventEditModelTestRecorder recorder;
    AudioEventEditModel             model{ recorder._recorder };
    addEvent( model, "a" );
    model.selectEvent( hashed_string( "a" ) );
    model.duplicateEvent();
    SW_EXPECT_FALSE( model.getEventName() == hashed_string( "a" ) );
    SW_EXPECT_TRUE( model.findEvent() != nullptr );
    SW_EXPECT_EQUAL( recorder._count, 1u );
}

SW_TEST_CASE( AudioEventEditModelTest, EndDropsTheCopy )
{
    AudioEventEditModelTestRecorder recorder;
    AudioEventEditModel             model{ recorder._recorder };
    addEvent( model, "a" );
    model.end();
    SW_EXPECT_FALSE( model.isEditingAny() );
    SW_EXPECT_TRUE( model.getLibrary()._listEvent.empty() );
    SW_EXPECT_TRUE( model.captureText().empty() ); // 고치는 중이 아니면 되돌릴 글이 없다
    SW_EXPECT_FALSE( model.applyText( "<AudioEventLibrary/>" ) );
}
