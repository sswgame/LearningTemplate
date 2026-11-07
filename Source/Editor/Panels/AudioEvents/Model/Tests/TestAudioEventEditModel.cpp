#include "pch.h"

#include "Core/Testing/TestFramework.h"

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
} // namespace

SW_TEST_CASE( AudioEventEditModelTest, RemoveSelectsTheFirstEventAndMarksDirty )
{
    AudioEventEditModel model;
    addEvent( model, "a" );
    addEvent( model, "b" );
    model.selectEvent( hashed_string( "b" ) );
    SW_EXPECT_TRUE( model.removeEvent() );
    SW_EXPECT_TRUE( model.getEventName() == hashed_string( "a" ) );
    SW_EXPECT_TRUE( model.isDirty() );
}

SW_TEST_CASE( AudioEventEditModelTest, RemovingAnUnknownEventChangesNothing )
{
    AudioEventEditModel model;
    addEvent( model, "a" );
    model.selectEvent( hashed_string( "missing" ) );
    SW_EXPECT_FALSE( model.removeEvent() );
    SW_EXPECT_FALSE( model.isDirty() );
}

SW_TEST_CASE( AudioEventEditModelTest, DuplicateSelectsTheCopy )
{
    AudioEventEditModel model;
    addEvent( model, "a" );
    model.selectEvent( hashed_string( "a" ) );
    model.duplicateEvent();
    SW_EXPECT_FALSE( model.getEventName() == hashed_string( "a" ) );
    SW_EXPECT_TRUE( model.findEvent() != nullptr );
    SW_EXPECT_TRUE( model.isDirty() );
}

SW_TEST_CASE( AudioEventEditModelTest, EndDropsTheCopy )
{
    AudioEventEditModel model;
    addEvent( model, "a" );
    model.markEdited();
    model.end();
    SW_EXPECT_FALSE( model.isDirty() );
    SW_EXPECT_FALSE( model.isEditingAny() );
    SW_EXPECT_TRUE( model.getLibrary()._listEvent.empty() );
}
