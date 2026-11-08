#include "pch.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Ability/AbilityDebugSnapshot.h"

#include "Engine/Reflection/EnumName.h"

#include "GameFramework/Runtime/Gameplay/Ability/Ability.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityComponent.h"

namespace sw::editor
{
    void AbilityDebugSnapshot::clear()
    {
        _listAttribute.clear();
        _listEffect.clear();
        _listTag.clear();
        _listAbility.clear();
        _listLog.clear();
        _time = 0.0f;
    }

    void AbilityDebugSnapshot::collect( const AbilityComponent& abilitySystem, AbilityDebugSnapshot& outSnapshot )
    {
        outSnapshot.clear();
        outSnapshot._time = abilitySystem.getTime();

        for ( const unique_ptr<AttributeSet>& pSet : abilitySystem.getAttributeSets() )
        {
            if ( pSet == nullptr )
                continue;
            for ( const hashed_string& name : pSet->getAttributeNames() )
            {
                const AttributeData* pData = pSet->findAttribute( name );
                if ( pData == nullptr )
                    continue;
                AbilityDebugAttributeRow row;
                row._name         = name.c_str();
                row._baseValue    = pData->_baseValue;
                row._currentValue = pData->_currentValue;
                outSnapshot._listAttribute.push_back( std::move( row ) );
            }
        }

        for ( const unique_ptr<ActiveGameplayEffect>& pEffect : abilitySystem.getActiveEffects() )
        {
            if ( pEffect == nullptr || pEffect->_bPendingRemove != SW_FALSE )
                continue;
            AbilityDebugEffectRow row;
            row._name          = pEffect->_spec._pDef != nullptr ? pEffect->_spec._pDef->_id.c_str() : "(no definition)";
            row._bInfinite     = pEffect->_duration <= 0.0f;
            row._remainingTime = row._bInfinite ? 0.0f : pEffect->_remainingTime;
            row._stackCount    = pEffect->_stackCount;
            row._source        = pEffect->_spec._context._sourceName.c_str();
            outSnapshot._listEffect.push_back( std::move( row ) );
        }

        for ( const TagID& tag : abilitySystem.getOwnedTagCounts().getTags().getTags() )
        {
            AbilityDebugTagRow row;
            const utf8*        pName = tag.getString();
            row._tag                 = pName != nullptr ? pName : "(unnamed)";
            row._count               = abilitySystem.getOwnedTagCounts().getCount( tag );
            outSnapshot._listTag.push_back( std::move( row ) );
        }

        vector<AbilitySpecHandle> listHandle;
        abilitySystem.getAbilitySpecHandles( listHandle );
        for ( const AbilitySpecHandle& handle : listHandle )
        {
            const Ability* pAbility = abilitySystem.findAbility( handle );
            if ( pAbility == nullptr )
                continue;
            AbilityDebugAbilityRow row;
            const hashed_string&   id = pAbility->getConfig()._id;
            row._name                 = id.size() > 0 ? id.c_str() : "(unnamed)";
            row._bActive              = pAbility->isActive();
            row._cooldownRemaining    = pAbility->getCooldownRemaining();
            outSnapshot._listAbility.push_back( std::move( row ) );
        }

        vector<AbilityEffectLogEntry> listLog;
        abilitySystem.collectEffectLog( listLog );
        for ( auto it = listLog.rbegin(); it != listLog.rend(); ++it )
        {
            AbilityDebugLogRow row;
            row._event     = getEnumName( it->_kind );
            row._effect    = it->_effectID.c_str();
            row._source    = it->_sourceName.c_str();
            row._time      = it->_time;
            row._bRejected = it->_kind == AbilityEffectLogKind::RejectedMissingTag || it->_kind == AbilityEffectLogKind::RejectedBlockedTag;
            outSnapshot._listLog.push_back( std::move( row ) );
        }
    }
} // namespace sw::editor
