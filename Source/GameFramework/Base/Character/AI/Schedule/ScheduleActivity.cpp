#include "pch.h"

#include "GameFramework/Base/Character/AI/Schedule/ScheduleActivity.h"

namespace sw
{
    ScheduleActivityRegistry::ScheduleActivityRegistry()
        : _registry{}
    {
        registerBuiltinActivities();
    }

    const ScheduleActivityRegistry& ScheduleActivityRegistry::getBuiltin()
    {
        static const ScheduleActivityRegistry s_builtin;
        return s_builtin;
    }

    bool ScheduleActivityRegistry::registerActivity( const ScheduleActivityDef& def )
    {
        return _registry.addLogged( def._name, def, "Schedule activity" );
    }

    const ScheduleActivityDef* ScheduleActivityRegistry::findActivity( const hashed_string& name ) const
    {
        return _registry.find( name );
    }

    void ScheduleActivityRegistry::registerBuiltinActivities()
    {
        const ScheduleActivityDef arrBuiltin[] = {
            {     hashed_string( "GoTo" ),  hashed_string( "Idle" ),       ScheduleActivityTarget::Place},
            {   hashed_string( "WorkAt" ),  hashed_string( "Work" ),       ScheduleActivityTarget::Place},
            {   hashed_string( "Attend" ), hashed_string( "Cheer" ),       ScheduleActivityTarget::Place},
            { hashed_string( "StayHome" ),  hashed_string( "Idle" ),        ScheduleActivityTarget::Home},
            {    hashed_string( "Sleep" ), hashed_string( "Sleep" ),        ScheduleActivityTarget::Home},
            {hashed_string( "UseObject" ),   hashed_string( "Use" ), ScheduleActivityTarget::SmartObject},
            {     hashed_string( "Meet" ),  hashed_string( "Talk" ), ScheduleActivityTarget::Appointment},
            {   hashed_string( "Wander" ),  hashed_string( "Walk" ),      ScheduleActivityTarget::Wander},
        };
        for ( const ScheduleActivityDef& def : arrBuiltin )
        {
            (void)registerActivity( def ); // 실패(이름 없음 · 중복)는 등록이 오류로 남긴다
        }
    }
} // namespace sw
