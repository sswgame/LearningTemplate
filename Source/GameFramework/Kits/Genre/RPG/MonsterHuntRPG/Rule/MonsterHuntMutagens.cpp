#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Rule/MonsterHuntMutagens.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Catalog/MonsterHuntCatalog.h"
#include "GameFramework/Runtime/Foundation/GameData/StatBlock.h"

namespace sw
{
    MonsterHuntMutagens::MonsterHuntMutagens()
        : _listGroup{}
        , _pCatalog{ nullptr }
        , _characterLevel{ 1 }
    {
    }

    void MonsterHuntMutagens::initialize( const MonsterHuntCatalog* pCatalog, int32 characterLevel )
    {
        _pCatalog       = pCatalog;
        _characterLevel = characterLevel;
        _listGroup.clear();
        if ( pCatalog == nullptr )
            return;
        for ( const MonsterHuntSlotGroupDef& def : pCatalog->getSlotGroups() )
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

    void MonsterHuntMutagens::computeStats( StatBlock& outStats ) const
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
            outStats.addValue( pMutagen->_statID, pMutagen->_value + static_cast<float32>( countMatches( group ) ) * pMutagen->_matchValue );
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
        io.value( _characterLevel );
        // 그룹 수 · 그룹마다 칸 수는 카탈로그(설정)가 정한다 — 저장한 수로 읽어 같은 자리에 맞춘다(감사 A-6). 줄어 밖에 남은 칸의 장착은 풀린다 —
        // 장착은 가진 스킬 · 변이를 가리키는 선택일 뿐이라 풀어도 잃는 것이 없다(가진 것은 이 상태 밖에 있다). 늘어난 칸은 비어 있다.
        uint32 groupCount = static_cast<uint32>( _listGroup.size() );
        io.count( sizeof( uint32 ) * 2, groupCount ); // 그룹마다 칸 수(4) + 변이 이름(4) 이상
        if ( io.isOk() == false )
            return;
        vector<Group> listFitted = _listGroup;
        if ( io.isReading() )
        {
            for ( Group& group : listFitted )
            {
                group = Group{ vector<hashed_string>( group._listSkill.size() ), hashed_string{} };
            }
        }
        vector<hashed_string> listSeenSkill;
        for ( uint32 groupIndex = 0; groupIndex < groupCount; ++groupIndex )
        {
            Group  saved     = io.isWriting() ? _listGroup[groupIndex] : Group{};
            uint32 slotCount = static_cast<uint32>( saved._listSkill.size() );
            io.count( sizeof( uint32 ), slotCount );
            if ( io.isOk() == false )
                return;
            if ( io.isReading() )
                saved._listSkill.resize( slotCount );
            for ( hashed_string& skillID : saved._listSkill )
            {
                io.value( skillID );
                if ( io.isWriting() || skillID.empty() )
                    continue;
                if ( io.check( VectorUtil::contains( listSeenSkill, skillID ) == false ) == false ) // `equipSkill` 의 `AlreadyEquipped` 와 같은 약속 — 겹치면 색 맞춤을 두 번 센다
                    return;
                listSeenSkill.push_back( skillID );
            }
            io.value( saved._mutagenID );
            if ( io.isWriting() )
                continue;
            if ( io.check( saved._mutagenID.empty() || _pCatalog->findMutagen( saved._mutagenID ) != nullptr ) == false )
                return;
            if ( groupIndex >= listFitted.size() )
                continue; // 줄어든 그룹 — 장착이 풀린다
            Group& target = listFitted[groupIndex];
            for ( size_t slot = 0; slot < saved._listSkill.size() && slot < target._listSkill.size(); ++slot )
            {
                target._listSkill[slot] = saved._listSkill[slot];
            }
            target._mutagenID = saved._mutagenID;
        }
        if ( io.isReading() )
            _listGroup = std::move( listFitted );
    }

} // namespace sw
