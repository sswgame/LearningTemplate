#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Ability/TagCountContainer.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    TagCountContainer::TagCountContainer()
        : _mapCount{}
        , _tags{}
        , _changedMulticast{}
    {
    }

    TagCountContainer::~TagCountContainer()
    {
        _changedMulticast.removeAll();
    }

    bool TagCountContainer::hasMatchingTag( TagID tag ) const
    {
        return tag.isValid() && _tags.hasTag( tag, false );
    }

    bool TagCountContainer::hasAllMatchingTags( const TagContainer& tags ) const
    {
        for ( const TagID& tag : tags.getTags() )
        {
            if ( hasMatchingTag( tag ) == false )
                return false;
        }
        return true;
    }

    bool TagCountContainer::hasAnyMatchingTags( const TagContainer& tags ) const
    {
        for ( const TagID& tag : tags.getTags() )
        {
            if ( _tags.hasTag( tag, false ) )
                return true;
        }
        return false;
    }

    int32 TagCountContainer::getCount( TagID tag ) const
    {
        const auto mapIter = _mapCount.find( tag._id );
        return mapIter != _mapCount.end() ? mapIter->second._count : 0;
    }

    void TagCountContainer::updateCount( TagID tag, int32 delta )
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

    void TagCountContainer::updateCounts( const TagContainer& tags, int32 delta )
    {
        for ( const TagID& tag : tags.getTags() )
        {
            updateCount( tag, delta );
        }
    }

    Subscription TagCountContainer::registerChanged( const TagChangedDelegate& delegate )
    {
        return _changedMulticast.add( delegate );
    }

    void TagCountContainer::unregisterAllChanged()
    {
        _changedMulticast.removeAll();
    }

} // namespace sw
