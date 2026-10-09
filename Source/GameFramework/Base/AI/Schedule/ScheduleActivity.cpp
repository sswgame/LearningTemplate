#include "pch.h"

#include "GameFramework/Base/AI/Schedule/ScheduleActivity.h"

namespace sw
{
    const utf8* toString( ScheduleActivityTarget target )
    {
        switch ( target )
        {
            case ScheduleActivityTarget::Place:
                return "Place";
            case ScheduleActivityTarget::Home:
                return "Home";
            case ScheduleActivityTarget::SmartObject:
                return "SmartObject";
            case ScheduleActivityTarget::Appointment:
                return "Appointment";
            case ScheduleActivityTarget::Wander:
                return "Wander";
        }
        return "Unknown";
    }

    ScheduleActivityRegistry::ScheduleActivityRegistry()
        : _listActivity{}
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
        if ( def._name.empty() || findActivity( def._name ) != nullptr )
            return false;
        _listActivity.push_back( def );
        return true;
    }

    const ScheduleActivityDef* ScheduleActivityRegistry::findActivity( const hashed_string& name ) const
    {
        for ( const ScheduleActivityDef& def : _listActivity )
        {
            if ( def._name == name )
                return &def;
        }
        return nullptr;
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
            (void)registerActivity( def );
        }
    }
} // namespace sw
