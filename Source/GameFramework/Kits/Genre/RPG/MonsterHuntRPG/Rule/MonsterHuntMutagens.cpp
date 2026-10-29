#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Rule/MonsterHuntMutagens.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/GameData/StatBlock.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/Catalog/MonsterHuntCatalog.h"

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

    void MonsterHuntMutagens::writeState( Archive& outArchive ) const
    {
        outArchive << _characterLevel;
        outArchive << static_cast<uint32>( _listGroup.size() );
        for ( const Group& group : _listGroup )
        {
            outArchive << static_cast<uint32>( group._listSkill.size() );
            for ( const hashed_string& skillID : group._listSkill )
            {
                StateArchiveUtil::writeName( outArchive, skillID );
            }
            StateArchiveUtil::writeName( outArchive, group._mutagenID );
        }
    }

    bool MonsterHuntMutagens::readState( Archive& archive )
    {
        int32  characterLevel = 0;
        uint32 count          = 0;
        archive >> characterLevel;
        // 그룹마다 슬롯 수(4) + 변이원(4) 바이트 이상
        const bool bInvalidMutagenData = _pCatalog == nullptr || archive.isError() || StateArchiveUtil::readCount( archive, 8, count ) == false || count != _listGroup.size();
        if ( bInvalidMutagenData )
            return false;
        vector<Group> listGroup = _listGroup;
        for ( Group& group : listGroup )
        {
            if ( StateArchiveUtil::readCount( archive, 4, count ) == false || count != group._listSkill.size() )
                return false;
            for ( hashed_string& skillID : group._listSkill )
            {
                if ( StateArchiveUtil::readName( archive, skillID ) == false )
                    return false;
            }
            if ( StateArchiveUtil::readName( archive, group._mutagenID ) == false )
                return false;
            if ( group._mutagenID.empty() == false && _pCatalog->findMutagen( group._mutagenID ) == nullptr )
                return false;
        }
        _characterLevel = characterLevel;
        _listGroup      = std::move( listGroup );
        return true;
    }
} // namespace sw
