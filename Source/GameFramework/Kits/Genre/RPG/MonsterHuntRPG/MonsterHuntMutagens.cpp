#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/MonsterHuntMutagens.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Reflection/TypeFqn.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/MonsterHuntCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/StatBlock.h"

namespace sw
{
    MonsterHuntMutagens::MonsterHuntMutagens()
        : _listGroup{}
        , _pCatalog{ nullptr }
        , _characterLevel{ 1 }
    {
    }

    void MonsterHuntMutagens::initialize( const MonsterHuntCatalog& catalog, int32 characterLevel )
    {
        _pCatalog       = &catalog;
        _characterLevel = characterLevel;
        _listGroup.clear();
        for ( const MonsterHuntSlotGroupDef& def : catalog.getSlotGroups() )
        {
            Group group;
            group._listSkill.resize( static_cast<size_t>( def._slotCount ) );
            _listGroup.push_back( group );
        }
    }

    MonsterHuntSlotResult MonsterHuntMutagens::equipSkill( int32 group, int32 slot, const hashed_string& skillID )
    {
        if ( isValidSlot( group, slot ) == false )
            return MonsterHuntSlotResult::InvalidSlot;
        if ( isGroupOpen( group ) == false )
            return MonsterHuntSlotResult::Locked;
        for ( int32 groupIndex = 0; groupIndex < getGroupCount(); ++groupIndex )
        {
            const vector<hashed_string>& listSkill = _listGroup[static_cast<size_t>( groupIndex )]._listSkill;
            for ( int32 slotIndex = 0; slotIndex < static_cast<int32>( listSkill.size() ); ++slotIndex )
            {
                const bool bSameSlot          = groupIndex == group && slotIndex == slot;
                const bool bDuplicateEquipped = bSameSlot == false && skillID.empty() == false && listSkill[static_cast<size_t>( slotIndex )] == skillID;
                if ( bDuplicateEquipped )
                    return MonsterHuntSlotResult::AlreadyEquipped;
            }
        }
        _listGroup[static_cast<size_t>( group )]._listSkill[static_cast<size_t>( slot )] = skillID;
        return MonsterHuntSlotResult::Ok;
    }

    MonsterHuntSlotResult MonsterHuntMutagens::equipMutagen( int32 group, const hashed_string& mutagenID )
    {
        if ( group < 0 || group >= getGroupCount() )
            return MonsterHuntSlotResult::InvalidSlot;
        if ( isGroupOpen( group ) == false )
            return MonsterHuntSlotResult::Locked;
        if ( mutagenID.empty() == false && ( _pCatalog == nullptr || _pCatalog->findMutagen( mutagenID ) == nullptr ) )
            return MonsterHuntSlotResult::UnknownMutagen;
        _listGroup[static_cast<size_t>( group )]._mutagenID = mutagenID;
        return MonsterHuntSlotResult::Ok;
    }

    void MonsterHuntMutagens::clearSlot( int32 group, int32 slot )
    {
        if ( isValidSlot( group, slot ) )
            _listGroup[static_cast<size_t>( group )]._listSkill[static_cast<size_t>( slot )] = hashed_string{};
    }

    bool MonsterHuntMutagens::isGroupOpen( int32 group ) const
    {
        const bool bGroupOutOfRange = _pCatalog == nullptr || group < 0 || group >= static_cast<int32>( _pCatalog->getSlotGroups().size() );
        if ( bGroupOutOfRange )
            return false;
        return _characterLevel >= _pCatalog->getSlotGroups()[static_cast<size_t>( group )]._requiredLevel;
    }

    hashed_string MonsterHuntMutagens::getSkill( int32 group, int32 slot ) const
    {
        return isValidSlot( group, slot ) ? _listGroup[static_cast<size_t>( group )]._listSkill[static_cast<size_t>( slot )] : hashed_string{};
    }

    hashed_string MonsterHuntMutagens::getMutagen( int32 group ) const
    {
        return group >= 0 && group < getGroupCount() ? _listGroup[static_cast<size_t>( group )]._mutagenID : hashed_string{};
    }

    int32 MonsterHuntMutagens::countMatches( int32 group ) const
    {
        const hashed_string          mutagenID = getMutagen( group );
        const MonsterHuntMutagenDef* pMutagen  = mutagenID.empty() == false && _pCatalog != nullptr ? _pCatalog->findMutagen( mutagenID ) : nullptr;
        if ( pMutagen == nullptr || pMutagen->_color.empty() )
            return 0;
        int32 matches = 0;
        for ( const hashed_string& skillID : _listGroup[static_cast<size_t>( group )]._listSkill )
        {
            matches += skillID.empty() == false && _pCatalog->getSkillColor( skillID ) == pMutagen->_color ? 1 : 0;
        }
        return matches;
    }

    void MonsterHuntMutagens::addStats( StatBlock& inoutStats ) const
    {
        if ( _pCatalog == nullptr )
            return;
        for ( int32 group = 0; group < getGroupCount(); ++group )
        {
            const hashed_string          mutagenID    = getMutagen( group );
            const MonsterHuntMutagenDef* pMutagen     = mutagenID.empty() ? nullptr : _pCatalog->findMutagen( mutagenID );
            const bool                   bSkipMutagen = pMutagen == nullptr || pMutagen->_statID.empty() || isGroupOpen( group ) == false;
            if ( bSkipMutagen )
                continue;
            inoutStats.addValue( pMutagen->_statID, pMutagen->_value + static_cast<float32>( countMatches( group ) ) * pMutagen->_matchValue );
        }
    }

    void MonsterHuntMutagens::collectEquippedSkills( vector<hashed_string>& outListSkill ) const
    {
        outListSkill.clear();
        for ( int32 group = 0; group < getGroupCount(); ++group )
        {
            if ( isGroupOpen( group ) == false )
                continue;
            for ( const hashed_string& skillID : _listGroup[static_cast<size_t>( group )]._listSkill )
            {
                if ( skillID.empty() == false )
                    outListSkill.push_back( skillID );
            }
        }
    }

    bool MonsterHuntMutagens::isValidSlot( int32 group, int32 slot ) const
    {
        return group >= 0 && group < getGroupCount() && slot >= 0 && slot < static_cast<int32>( _listGroup[static_cast<size_t>( group )]._listSkill.size() );
    }

    void MonsterHuntMutagens::serializeState( StateIO& io )
    {
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        // 그룹 수 · 그룹마다 칸 수는 카탈로그(설정)가 정한다 — 저장한 그룹을 읽은 뒤 지금 구성의 같은 자리에 맞춘다(감사 A-6). 줄어 밖에 남은 칸의 장착은 풀린다 —
        // 장착은 가진 스킬 · 변이를 가리키는 선택일 뿐이라 풀어도 잃는 것이 없다(가진 것은 이 상태 밖에 있다). 늘어난 칸은 비어 있다.
        const vector<Group> listLayout = io.isReading() ? _listGroup : vector<Group>{};
        io.reflected( *this );
        if ( io.isWriting() || io.isOk() == false )
            return;
        vector<hashed_string> listSeenSkill;
        for ( const Group& saved : _listGroup )
        {
            for ( const hashed_string& skillID : saved._listSkill )
            {
                if ( skillID.empty() )
                    continue;
                if ( io.check( VectorUtil::contains( listSeenSkill, skillID ) == false ) == false ) // `equipSkill` 의 `AlreadyEquipped` 와 같은 약속 — 겹치면 색 맞춤을 두 번 센다
                    return;
                listSeenSkill.push_back( skillID );
            }
            if ( io.check( saved._mutagenID.empty() || _pCatalog->findMutagen( saved._mutagenID ) != nullptr ) == false )
                return;
        }
        vector<Group> listFitted;
        listFitted.reserve( listLayout.size() );
        for ( size_t groupIndex = 0; groupIndex < listLayout.size(); ++groupIndex )
        {
            Group fitted{ vector<hashed_string>( listLayout[groupIndex]._listSkill.size() ), hashed_string{} };
            if ( groupIndex < _listGroup.size() )
            {
                const Group& saved = _listGroup[groupIndex];
                for ( size_t slot = 0; slot < saved._listSkill.size() && slot < fitted._listSkill.size(); ++slot )
                {
                    fitted._listSkill[slot] = saved._listSkill[slot];
                }
                fitted._mutagenID = saved._mutagenID;
            }
            listFitted.push_back( std::move( fitted ) );
        }
        _listGroup = std::move( listFitted );
    }

} // namespace sw
