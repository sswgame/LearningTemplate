#include "pch.h"

#include "Engine/Automation/AutomationLogPolicy.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    namespace
    {
        struct AutomationLogPolicyInternal
        {
            /** @brief id 에 쓸 수 있는 글자입니다(`"영역.사건"` 꼴). `*` · `?` 같은 와일드카드는 여기에 없다. */
            static bool isIDCharacter( utf8 character )
            {
                const bool bLetter = ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' );
                const bool bDigit  = '0' <= character && character <= '9';
                return bLetter || bDigit || character == '_' || character == '.';
            }

            static bool isKnownKey( const string& key )
            {
                return key == "id" || key == "scenario" || key == "suite" || key == "reason" || key == "plan";
            }

            /** @brief 사람이 채울 칸이 비었거나 초안의 `TODO` 가 남았으면 true 입니다. */
            static bool isUnfilled( const string& text )
            {
                return text.empty() || StringUtil::startsWith( text, AutomationLogPolicy::kPlaceholderText );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool AutomationLogPolicy::loadAllowList( string& outError )
    {
        JSONDocument document;
        string       absolutePath;
        if ( document.loadResource( kAllowListPath, &absolutePath ) == false )
        {
            outError = string( kAllowListPath ) + " cannot be read";
            return false;
        }
        return readAllowList( document.getRoot(), kAllowListPath, outError );
    }

    bool AutomationLogPolicy::parseAllowList( string_view jsonText, string_view sourceName, string& outError )
    {
        JSONDocument document;
        if ( document.parse( FileUtil::skipUtf8Bom( jsonText ), sourceName ) == false )
        {
            outError = string( sourceName ) + ": not valid JSON";
            return false;
        }
        return readAllowList( document.getRoot(), sourceName, outError );
    }

    bool AutomationLogPolicy::readEntry( const JSONValue& entry, string_view sourceName, size_t index, AutomationAllowedLog& outEntry, string& outError )
    {
        using Internal      = AutomationLogPolicyInternal;
        const string where  = string( sourceName ) + ": entry " + to_string( index );
        const string fixHow = " - every entry needs id, exactly one of scenario / suite, reason and plan (a filled-in sentence, not TODO)";
        if ( entry.isObject() == false )
        {
            outError = where + " is not an object" + fixHow;
            return false;
        }
        for ( const string& member : entry.getMemberNames() )
        {
            if ( Internal::isKnownKey( member ) == false )
            {
                outError = where + " has an unknown key '" + member + "'" + fixHow;
                return false;
            }
        }
        outEntry._id           = entry.get( "id", false ).asString();
        const string scenario  = entry.get( "scenario", false ).asString();
        const string suite     = entry.get( "suite", false ).asString();
        outEntry._reason       = entry.get( "reason", false ).asString();
        outEntry._plan         = entry.get( "plan", false ).asString();
        const bool bBothScopes = scenario.empty() == false && suite.empty() == false;
        const bool bNoScope    = scenario.empty() && suite.empty();
        if ( bBothScopes || bNoScope )
        {
            outError = where + " must name exactly one scope, scenario or suite (there are no global entries)" + fixHow;
            return false;
        }
        outEntry._scopeKind = scenario.empty() ? AutomationAllowScopeKind::Suite : AutomationAllowScopeKind::Scenario;
        outEntry._scope     = scenario.empty() ? suite : scenario;
        struct FieldRef
        {
            const utf8*   _pName;
            const string* _pValue;
        };
        const FieldRef arrField[] = {
            {            "id",     &outEntry._id},
            {"scenario/suite",  &outEntry._scope},
            {        "reason", &outEntry._reason},
            {          "plan",   &outEntry._plan}
        };
        for ( const FieldRef& field : arrField )
        {
            if ( Internal::isUnfilled( *field._pValue ) )
            {
                outError = where + " field '" + field._pName + "' is empty or still TODO" + fixHow;
                return false;
            }
        }
        for ( const utf8 character : outEntry._id )
        {
            if ( Internal::isIDCharacter( character ) == false )
            {
                outError = where + " id '" + outEntry._id + "' may only hold letters, digits, '_' and '.' (no wildcards)";
                return false;
            }
        }
        return true;
    }

    bool AutomationLogPolicy::readAllowList( const JSONValue& root, string_view sourceName, string& outError )
    {
        const JSONValue listAllowed = root.get( "allowed", false );
        if ( listAllowed.isArray() == false )
        {
            outError = string( sourceName ) + ": needs an \"allowed\" array";
            return false;
        }
        vector<AutomationAllowedLog> listRead;
        for ( size_t index = 0; index < listAllowed.size(); ++index )
        {
            AutomationAllowedLog entry;
            if ( readEntry( listAllowed.at( index ), sourceName, index, entry, outError ) == false )
                return false;
            const auto isSameEntry = [&entry]( const AutomationAllowedLog& other )
            { return other._id == entry._id && other._scope == entry._scope && other._scopeKind == entry._scopeKind; };
            if ( VectorUtil::containsIf( listRead, isSameEntry ) || VectorUtil::containsIf( _listAllowed, isSameEntry ) )
            {
                outError = string( sourceName ) + ": entry " + to_string( index ) + " repeats id '" + entry._id + "' for scope '" + entry._scope + "'";
                return false;
            }
            listRead.push_back( std::move( entry ) );
        }
        for ( AutomationAllowedLog& entry : listRead )
        {
            _listAllowed.push_back( std::move( entry ) );
        }
        _bLoaded = true;
        return true;
    }

    void AutomationLogPolicy::selectScenario( string_view name )
    {
        _listScenario.push_back( string( name ) );
    }

    void AutomationLogPolicy::selectSuite( string_view name )
    {
        _listSuite.push_back( string( name ) );
    }

    void AutomationLogPolicy::declareExpected( string_view contains )
    {
        if ( contains.empty() )
            return;
        _listExpected.push_back( string( contains ) );
    }

    void AutomationLogPolicy::declareExpectedID( string_view id )
    {
        if ( id.empty() )
            return;
        _listExpectedID.push_back( string( id ) );
    }

    void AutomationLogPolicy::clearScope()
    {
        _listExpectedID.clear();
        _listScenario.clear();
        _listSuite.clear();
        _listExpected.clear();
    }

    bool AutomationLogPolicy::isExemptMessage( string_view message )
    {
        return StringUtil::startsWith( message, kRunnerPrefix ) || StringUtil::startsWith( message, kDefensivePrefix );
    }

    bool AutomationLogPolicy::isDeclared( string_view text ) const
    {
        return VectorUtil::containsIf( _listExpected, [&]( const string& expected )
        { return text.find( string_view( expected ) ) != string_view::npos; } );
    }

    bool AutomationLogPolicy::isDeclaredID( string_view id ) const
    {
        return id.empty() == false && VectorUtil::containsIf( _listExpectedID, [&]( const string& expected )
        { return string_view( expected ) == id; } );
    }

    const AutomationAllowedLog* AutomationLogPolicy::findAllowed( string_view id ) const
    {
        return VectorUtil::findIf( _listAllowed, [&]( const AutomationAllowedLog& allowed )
        {
            if ( string_view( allowed._id ) != id )
                return false;
            const vector<string>& listScope = allowed._scopeKind == AutomationAllowScopeKind::Scenario ? _listScenario : _listSuite;
            return VectorUtil::contains( listScope, allowed._scope );
        } );
    }

    AutomationLogVerdict AutomationLogPolicy::classify( LogLevel level, string_view id, string_view message ) const
    {
        if ( isGatedLevel( level ) == false || isExemptMessage( message ) )
            return AutomationLogVerdict::NotGated;
        if ( isDeclaredID( id ) || isDeclared( message ) )
            return AutomationLogVerdict::Declared;
        if ( id.empty() )
            return AutomationLogVerdict::MissingID;
        return findAllowed( id ) != nullptr ? AutomationLogVerdict::Allowed : AutomationLogVerdict::NotAllowed;
    }

    void AutomationLogPolicy::judge( LogLevel level, string_view caller, string_view id, string_view message, AutomationLogJudgement& inoutJudgement ) const
    {
        const AutomationLogVerdict verdict = classify( level, id, message );
        if ( verdict == AutomationLogVerdict::Allowed )
        {
            const string idText{ id };
            if ( VectorUtil::contains( inoutJudgement._listAllowedID, idText ) == false )
                inoutJudgement._listAllowedID.push_back( idText );
            return;
        }
        if ( verdict != AutomationLogVerdict::MissingID && verdict != AutomationLogVerdict::NotAllowed )
            return;
        const bool bMissingID = verdict == AutomationLogVerdict::MissingID;
        if ( bMissingID )
            ++inoutJudgement._missingIDCount;
        else
            ++inoutJudgement._notAllowedCount;
        const string idText = bMissingID ? string( "(no log id)" ) : "(id=" + string( id ) + ")";
        inoutJudgement._listUnhandled.push_back( string( level == LogLevel::Error ? "[Error] [" : "[Warning] [" ) + string( caller ) + "] " + idText + " " + string( message ) );
    }
} // namespace sw
