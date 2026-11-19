#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Respawn/RespawnRules.h"

#include "Core/Foundation/Constants.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"
#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"

namespace sw
{
    namespace
    {
        struct RespawnRulesInternal
        {
            static void appendError( string& inoutText, string_view error )
            {
                if ( inoutText.empty() == false )
                    inoutText += "; ";
                inoutText += error;
            }
        };
    } // namespace

    int32 RespawnRules::findPolicyIndex( RespawnPolicyKind kind ) const
    {
        for ( size_t index = 0; index < _listPolicy.size(); ++index )
        {
            if ( _listPolicy[index]._kind == kind )
                return static_cast<int32>( index );
        }
        return constant::invalid_index::kInt32;
    }

    string RespawnRules::describeErrors() const
    {
        string errors = ConfigRangeUtil::describeRangeErrors( *this );
        if ( _listPolicy.empty() )
            RespawnRulesInternal::appendError( errors, "no <Policy> - a death would never end" );
        bool bAnyAutomatic = false;
        for ( size_t index = 0; index < _listPolicy.size(); ++index )
        {
            const RespawnPolicyEntry& entry = _listPolicy[index];
            const string              range = ConfigRangeUtil::describeRangeErrors( entry );
            if ( range.empty() == false )
                RespawnRulesInternal::appendError( errors, range );
            if ( entry._kind >= RespawnPolicyKind::Count )
                RespawnRulesInternal::appendError( errors, "unknown policy kind" );
            // 요청은 정책 종류로 항목을 찾는다 — 같은 종류가 둘이면 뒤의 것의 횟수 · 비용에 영영 닿지 않는다.
            if ( findPolicyIndex( entry._kind ) != static_cast<int32>( index ) )
                RespawnRulesInternal::appendError( errors, "a policy kind is listed twice" );
            if ( entry._costAmount > 0 && entry._costCurrency.empty() )
                RespawnRulesInternal::appendError( errors, "a policy has costAmount without costCurrency" );
            if ( entry._bRequestOnly && entry._kind == RespawnPolicyKind::PlayerChoice )
                RespawnRulesInternal::appendError( errors, "PlayerChoice cannot be requestOnly (it is the step that takes requests)" );
            bAnyAutomatic = bAnyAutomatic || entry._bRequestOnly == false;
        }
        if ( _listPolicy.empty() == false && bAnyAutomatic == false )
            RespawnRulesInternal::appendError( errors, "every policy is requestOnly - nothing would ever be tried" );
        if ( ( _penaltyCurrencyRatio > 0.0f || _penaltyCurrencyAmount > 0 ) && _penaltyCurrency.empty() )
            RespawnRulesInternal::appendError( errors, "a currency penalty without penaltyCurrency" );
        return errors;
    }

    bool RespawnRules::readXML( const XMLNode& node, string_view sourceName, RespawnRules& outRules )
    {
        RespawnRules rules;
        bool         bValid = XMLFields::readProperties( node, rules, sourceName );
        bValid              = GameDataXML::readChildrenInto( node, "Policy", sourceName, rules._listPolicy ) && bValid;
        if ( bValid == false )
            return false;
        const string errors = rules.describeErrors();
        if ( errors.empty() == false )
        {
            SW_LOG_ERROR_ID( "RespawnRules.Invalid", "%#: <Respawn> is invalid - %#", sourceName, errors.c_str() );
            return false;
        }
        outRules = std::move( rules );
        return true;
    }
} // namespace sw
