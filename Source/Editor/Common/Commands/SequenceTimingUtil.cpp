#include "pch.h"

#include "Editor/Common/Commands/SequenceTimingUtil.h"

namespace sw::editor
{
    void SequenceTimingUtil::captureTiming( const vector<SequenceTrackItem>& listItem, vector<SequenceClipTiming>& outListTiming )
    {
        outListTiming.clear();
        outListTiming.reserve( listItem.size() );
        for ( const SequenceTrackItem& item : listItem )
        {
            outListTiming.push_back( SequenceClipTiming{ item._start, item._end, item._kind } );
        }
    }

    bool SequenceTimingUtil::hasTimingChanged( const vector<SequenceClipTiming>& listTimingBefore, const vector<SequenceTrackItem>& listItem )
    {
        if ( listTimingBefore.size() != listItem.size() )
            return true;
        for ( size_t itemIndex = 0; itemIndex < listItem.size(); ++itemIndex )
        {
            const SequenceClipTiming& before = listTimingBefore[itemIndex];
            const SequenceTrackItem&  after  = listItem[itemIndex];
            const bool                bSame  = before._start == after._start && before._end == after._end && before._kind == after._kind;
            if ( bSame == false )
                return true;
        }
        return false;
    }
} // namespace sw::editor
