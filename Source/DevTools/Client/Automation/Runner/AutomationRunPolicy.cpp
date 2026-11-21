#include "pch.h"

#include "DevTools/Client/Automation/Runner/AutomationRunPolicy.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"

#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    expected<AutomationRunPolicy> AutomationRunPolicy::parse( string_view jsonText, string_view sourceName )
    {
        JSONDocument document;
        if ( document.parse( jsonText, sourceName ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the scenario run policy is not JSON", sourceName } );
        const JSONValue        root             = document.getRoot();
        const optional<uint32> stallFrames      = root.readRequired<uint32>( "_stallFrames" );
        const optional<uint32> stallWallSeconds = root.readRequired<uint32>( "_stallWallSeconds" );
        const optional<uint32> repeatedLimit    = root.readRequired<uint32>( "_repeatedFailureLimit" );
        const bool             bHasConditions   = root.has( "_conditions" ) && root.get( "_conditions" ).getType() == JSONType::Object;
        if ( stallFrames.has_value() == false || stallWallSeconds.has_value() == false || repeatedLimit.has_value() == false || bHasConditions == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the scenario run policy needs _stallFrames, _stallWallSeconds, _repeatedFailureLimit and the _conditions object", sourceName } );
        if ( *stallFrames == 0 || *stallWallSeconds == 0 || *repeatedLimit == 0 )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the scenario run policy is out of range (_stallFrames, _stallWallSeconds and _repeatedFailureLimit are 1 or more)", sourceName } );
        AutomationRunPolicy policy;
        policy._stallFrames          = *stallFrames;
        policy._stallWallSeconds     = *stallWallSeconds;
        policy._repeatedFailureLimit = *repeatedLimit;
        return policy;
    }

    expected<AutomationRunPolicy> AutomationRunPolicy::load()
    {
        const AbsolutePath path = ProcessRoots::get()._project.join( kPolicyFile );
        string             text;
        if ( FileUtil::readTextFile( path, text ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "could not read the scenario run policy", path.str() } );
        expected<AutomationRunPolicy> policy = parse( text, path.str() );
        if ( policy.has_value() == false )
            return policy;
        JSONDocument document;
        if ( document.parse( text, path.str() ) == false ) // 위 `parse` 가 이미 읽었다 — 조건 칸을 넘기려고 한 번 더 연다
            return unexpected( Error{ ErrorKind::InvalidArgument, "the scenario run policy is not JSON", path.str() } );
        if ( expected<void> configured = AutomationStepRegistry::configureConditions( document.getRoot().get( "_conditions" ) ); configured.has_value() == false )
            return unexpected( configured.error() );
        return policy;
    }
} // namespace sw
