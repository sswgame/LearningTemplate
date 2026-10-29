#include "pch.h"

#include "GameFramework/Base/Gameplay/Ability/TagCountTable.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    TagCountTable::TagCountTable()
        : _mapCount{}
        , _tags{}
        , _changedMulticast{}
    {
    }

    TagCountTable::~TagCountTable()
    {
        _changedMulticast.removeAll();
    }

    bool TagCountTable::hasMatchingTag( TagID tag ) const
    {
        return tag.isValid() && _tags.hasTag( tag, false );
    }

    bool TagCountTable::hasAllMatchingTags( const TagContainer& tags ) const
    {
        for ( const TagID& tag : tags.getTags() )
        {
            if ( hasMatchingTag( tag ) == false )
                return false;
        }
        return true;
    }

    bool TagCountTable::hasAnyMatchingTags( const TagContainer& tags ) const
    {
        for ( const TagID& tag : tags.getTags() )
        {
            if ( _tags.hasTag( tag, false ) )
                return true;
        }
        return false;
    }

    int32 TagCountTable::getCount( TagID tag ) const
    {
        const auto mapIter = _mapCount.find( tag._id );
        return mapIter != _mapCount.end() ? mapIter->second._count : 0;
    }

    void TagCountTable::updateCount( TagID tag, int32 delta )
    {
        if ( tag.isValid() == false || delta == 0 )
            return;
        const auto  mapIter  = _mapCount.find( tag._id );
        const int32 oldCount = mapIter != _mapCount.end() ? mapIter->second._count : 0;
        const int32 newCount = MathUtil::max( 0, oldCount + delta );
        if ( newCount == oldCount )
            return;
        if ( newCount == 0 )
            _mapCount.erase( tag._id );
        else
            _mapCount.insert_or_assign( tag._id, CountEntry{ tag, newCount } );

        if ( oldCount == 0 )
        {
            _tags.addTag( tag );
            _changedMulticast.broadcast( tag, newCount );
        }
        else if ( newCount == 0 )
        {
            _tags.removeTag( tag );
            _changedMulticast.broadcast( tag, 0 );
        }
    }

    void TagCountTable::updateCounts( const TagContainer& tags, int32 delta )
    {
        for ( const TagID& tag : tags.getTags() )
        {
            updateCount( tag, delta );
        }
    }

    Subscription TagCountTable::registerChanged( const TagChangedDelegate& delegate )
    {
        return _changedMulticast.add( delegate );
    }

    void TagCountTable::unregisterAllChanged()
    {
        _changedMulticast.removeAll();
    }

} // namespace sw
