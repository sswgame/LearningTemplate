#include "pch.h"

#include "GameFramework/Kits/Casual/Rhythm/RhythmChart.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/Xml/XmlDocument.h"

#include "GameFramework/Base/Data/GameDataXml.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "RhythmChart" );

    namespace
    {
        struct RhythmChartInternal
        {
            static constexpr float32 kDefaultBpm   = 120.0f;
            static constexpr float32 kMinBpm       = 1.0f;
            static constexpr int32   kMaxLaneCount = 16;

            static bool isNoteBefore( const RhythmNote& lhs, const RhythmNote& rhs )
            {
                if ( lhs._beat != rhs._beat )
                    return lhs._beat < rhs._beat;
                return lhs._lane < rhs._lane;
            }

            static bool isBpmChangeBefore( const RhythmBpmChange& lhs, const RhythmBpmChange& rhs ) { return lhs._beat < rhs._beat; }
            static bool isStopBefore( const RhythmStop& lhs, const RhythmStop& rhs ) { return lhs._beat < rhs._beat; }
            static bool isMeasureChangeBefore( const RhythmMeasureChange& lhs, const RhythmMeasureChange& rhs ) { return lhs._beat < rhs._beat; }

            /** @brief `_beat` 가 @p beat 보다 작은 점의 수입니다(이분 탐색). */
            static size_t countPointsBeforeBeat( const vector<RhythmTimingPoint>& listPoint, float32 beat )
            {
                size_t low  = 0;
                size_t high = listPoint.size();
                while ( low < high )
                {
                    const size_t middle = low + ( high - low ) / 2;
                    if ( listPoint[middle]._beat < beat )
                        low = middle + 1;
                    else
                        high = middle;
                }
                return low;
            }

            /** @brief `_time` 이 @p seconds 이하인 점의 수입니다(이분 탐색). */
            static size_t countPointsUpToTime( const vector<RhythmTimingPoint>& listPoint, float32 seconds )
            {
                size_t low  = 0;
                size_t high = listPoint.size();
                while ( low < high )
                {
                    const size_t middle = low + ( high - low ) / 2;
                    if ( listPoint[middle]._time <= seconds )
                        low = middle + 1;
                    else
                        high = middle;
                }
                return low;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    RhythmChart::RhythmChart()
        : _listNote{}
        , _listTimingPoint{}
        , _listMeasureChange{}
        , _title{}
        , _artist{}
        , _offset{ 0.0f }
        , _baseBpm{ RhythmChartInternal::kDefaultBpm }
        , _endTime{ 0.0f }
        , _level{ 0 }
        , _laneCount{ 7 }
        , _judgmentCount{ 0 }
    {
        makeTimingPoints( {}, {} );
    }

    bool RhythmChart::loadFromResource( string_view path )
    {
        return GameDataXml::loadFile( *this, &RhythmChart::loadRoot, path, "Chart" );
    }

    bool RhythmChart::loadFromXmlText( string_view xmlText, string_view sourceName )
    {
        return GameDataXml::loadText( *this, &RhythmChart::loadRoot, xmlText, sourceName, "Chart" );
    }

    float32 RhythmChart::convertBeatToSeconds( float32 beat ) const
    {
        const size_t pointCount = RhythmChartInternal::countPointsBeforeBeat( _listTimingPoint, beat );
        if ( pointCount == 0 )
        {
            const RhythmTimingPoint& first = _listTimingPoint.front();
            return first._time + ( beat - first._beat ) * first._secondsPerBeat;
        }
        const RhythmTimingPoint& point = _listTimingPoint[pointCount - 1];
        return point._time + point._stopSeconds + ( beat - point._beat ) * point._secondsPerBeat;
    }

    float32 RhythmChart::convertSecondsToBeat( float32 seconds ) const
    {
        const size_t pointCount = RhythmChartInternal::countPointsUpToTime( _listTimingPoint, seconds );
        if ( pointCount == 0 )
        {
            const RhythmTimingPoint& first = _listTimingPoint.front();
            return first._beat + ( seconds - first._time ) / first._secondsPerBeat;
        }
        const RhythmTimingPoint& point        = _listTimingPoint[pointCount - 1];
        const float32            localSeconds = seconds - point._time;
        if ( localSeconds <= point._stopSeconds )
            return point._beat;
        return point._beat + ( localSeconds - point._stopSeconds ) / point._secondsPerBeat;
    }

    float32 RhythmChart::findBpmAt( float32 beat ) const
    {
        const size_t             pointCount = RhythmChartInternal::countPointsBeforeBeat( _listTimingPoint, beat );
        const RhythmTimingPoint& point      = _listTimingPoint[pointCount == 0 ? 0 : pointCount - 1];
        return 60.0f / point._secondsPerBeat;
    }

    float32 RhythmChart::computeNoteY( float32 noteBeat, float32 currentTime, float32 hiSpeed, RhythmScrollMode mode ) const
    {
        switch ( mode )
        {
            case RhythmScrollMode::ConstantSpeed:
                return ( convertBeatToSeconds( noteBeat ) - currentTime ) * hiSpeed;
            case RhythmScrollMode::FollowBpm:
                return ( noteBeat - convertSecondsToBeat( currentTime ) ) * ( 60.0f / _baseBpm ) * hiSpeed;
        }
        return 0.0f;
    }

    void RhythmChart::fillMeasureLineBeats( float32 endBeat, vector<float32>& outListBeat ) const
    {
        outListBeat.clear();
        float32 beat            = 0.0f;
        float32 beatsPerMeasure = 4.0f;
        size_t  changeIndex     = 0;
        while ( beat <= endBeat )
        {
            while ( changeIndex < _listMeasureChange.size() && _listMeasureChange[changeIndex]._beat <= beat )
            {
                beatsPerMeasure = _listMeasureChange[changeIndex]._beatsPerMeasure;
                ++changeIndex;
            }
            outListBeat.push_back( beat );
            const float32 nextBeat     = beat + beatsPerMeasure;
            const bool    bChangeFirst = changeIndex < _listMeasureChange.size() && _listMeasureChange[changeIndex]._beat < nextBeat;
            beat                       = bChangeFirst ? _listMeasureChange[changeIndex]._beat : nextBeat;
        }
    }

    bool RhythmChart::loadRoot( const XmlNode& root, string_view sourceName )
    {
        const utf8* pTitle  = root.findAttribute( "title" );
        _title              = pTitle != nullptr ? pTitle : "";
        const utf8* pArtist = root.findAttribute( "artist" );
        _artist             = pArtist != nullptr ? pArtist : "";
        _level              = MathUtil::max( 0, root.getAttributeInt( "level", 0 ) );
        _laneCount          = MathUtil::clamp( root.getAttributeInt( "lanes", 7 ), 1, RhythmChartInternal::kMaxLaneCount );
        _offset             = root.getAttributeFloat( "offset", 0.0f );

        vector<RhythmBpmChange> listBpmChange;
        for ( XmlNode node = root.findChild( "Bpm" ); node; node = node.findNextSibling( "Bpm" ) )
        {
            RhythmBpmChange change;
            change._beat = node.getAttributeFloat( "beat", 0.0f );
            change._bpm  = node.getAttributeFloat( "bpm", RhythmChartInternal::kDefaultBpm );
            if ( change._beat < 0.0f || change._bpm < RhythmChartInternal::kMinBpm )
            {
                SW_LOG_WARNING( "%#: <Bpm beat=\"%#\" bpm=\"%#\"> is out of range - skipped", sourceName, change._beat, change._bpm );
                continue;
            }
            listBpmChange.push_back( change );
        }
        if ( listBpmChange.empty() )
        {
            RhythmBpmChange change;
            change._bpm = MathUtil::max( RhythmChartInternal::kMinBpm, root.getAttributeFloat( "bpm", RhythmChartInternal::kDefaultBpm ) );
            listBpmChange.push_back( change );
        }

        vector<RhythmStop> listStop;
        for ( XmlNode node = root.findChild( "Stop" ); node; node = node.findNextSibling( "Stop" ) )
        {
            RhythmStop stop;
            stop._beat    = node.getAttributeFloat( "beat", 0.0f );
            stop._seconds = node.getAttributeFloat( "seconds", 0.0f );
            if ( stop._beat < 0.0f || stop._seconds <= 0.0f )
            {
                SW_LOG_WARNING( "%#: <Stop beat=\"%#\" seconds=\"%#\"> is out of range - skipped", sourceName, stop._beat, stop._seconds );
                continue;
            }
            listStop.push_back( stop );
        }
        makeTimingPoints( listBpmChange, listStop );
        _baseBpm = MathUtil::max( RhythmChartInternal::kMinBpm, root.getAttributeFloat( "baseBpm", findBpmAt( 0.0f ) ) );

        _listMeasureChange.clear();
        for ( XmlNode node = root.findChild( "Measure" ); node; node = node.findNextSibling( "Measure" ) )
        {
            RhythmMeasureChange change;
            change._beat            = node.getAttributeFloat( "beat", 0.0f );
            change._beatsPerMeasure = node.getAttributeFloat( "beatsPerMeasure", 4.0f );
            if ( change._beat < 0.0f || change._beatsPerMeasure <= 0.0f )
            {
                SW_LOG_WARNING( "%#: <Measure beat=\"%#\"> is out of range - skipped", sourceName, change._beat );
                continue;
            }
            _listMeasureChange.push_back( change );
        }
        std::stable_sort( _listMeasureChange.begin(), _listMeasureChange.end(), &RhythmChartInternal::isMeasureChangeBefore );

        _listNote.clear();
        for ( XmlNode node = root.findChild( "Note" ); node; node = node.findNextSibling( "Note" ) )
        {
            RhythmNote note;
            note._lane    = node.getAttributeInt( "lane", -1 );
            note._beat    = node.getAttributeFloat( "beat", 0.0f );
            note._endBeat = node.getAttributeFloat( "endBeat", note._beat );
            if ( note._lane < 0 || note._lane >= _laneCount )
            {
                SW_LOG_WARNING( "%#: note at beat %# is on lane %# outside 0..%# - skipped", sourceName, note._beat, note._lane, _laneCount - 1 );
                continue;
            }
            if ( note._endBeat < note._beat )
            {
                SW_LOG_WARNING( "%#: note at beat %# ends before it starts - treated as a tap", sourceName, note._beat );
                note._endBeat = note._beat;
            }
            note._bLong = note._endBeat > note._beat ? SW_TRUE : SW_FALSE;
            _listNote.push_back( note );
        }
        std::stable_sort( _listNote.begin(), _listNote.end(), &RhythmChartInternal::isNoteBefore );

        // 같은 레인에서 앞 노트(롱노트면 그 끝)와 겹치는 노트는 칠 수 없다.
        vector<float32> listLaneFreeBeat;
        listLaneFreeBeat.resize( static_cast<size_t>( _laneCount ), -1.0e30f );
        vector<RhythmNote> listKeptNote;
        listKeptNote.reserve( _listNote.size() );
        _judgmentCount = 0;
        _endTime       = _offset;
        for ( RhythmNote& note : _listNote )
        {
            float32& freeBeat = listLaneFreeBeat[static_cast<size_t>( note._lane )];
            if ( note._beat <= freeBeat )
            {
                SW_LOG_WARNING( "%#: note at beat %# on lane %# overlaps the previous note - skipped", sourceName, note._beat, note._lane );
                continue;
            }
            freeBeat      = note._endBeat;
            note._time    = convertBeatToSeconds( note._beat );
            note._endTime = convertBeatToSeconds( note._endBeat );
            _endTime      = MathUtil::max( _endTime, note._endTime );
            _judgmentCount += note._bLong == SW_TRUE ? 2 : 1;
            listKeptNote.push_back( note );
        }
        _listNote.swap( listKeptNote );
        if ( _listNote.empty() )
            SW_LOG_WARNING( "%#: chart has no playable notes", sourceName );
        return _listNote.empty() == false;
    }

    void RhythmChart::makeTimingPoints( const vector<RhythmBpmChange>& listBpmChange, const vector<RhythmStop>& listStop )
    {
        vector<RhythmBpmChange> listSortedBpm = listBpmChange;
        std::stable_sort( listSortedBpm.begin(), listSortedBpm.end(), &RhythmChartInternal::isBpmChangeBefore );
        vector<RhythmStop> listSortedStop = listStop;
        std::stable_sort( listSortedStop.begin(), listSortedStop.end(), &RhythmChartInternal::isStopBefore );

        // 박 0 의 BPM 은 박 0 이하의 마지막 변화, 없으면 첫 변화입니다.
        float32 bpm = listSortedBpm.empty() ? RhythmChartInternal::kDefaultBpm : listSortedBpm.front()._bpm;
        _listTimingPoint.clear();
        RhythmTimingPoint first;
        first._beat = 0.0f;
        first._time = _offset;
        _listTimingPoint.push_back( first );

        size_t bpmIndex  = 0;
        size_t stopIndex = 0;
        while ( true )
        {
            RhythmTimingPoint& point = _listTimingPoint.back();
            while ( bpmIndex < listSortedBpm.size() && listSortedBpm[bpmIndex]._beat <= point._beat )
            {
                bpm = listSortedBpm[bpmIndex]._bpm;
                ++bpmIndex;
            }
            while ( stopIndex < listSortedStop.size() && listSortedStop[stopIndex]._beat <= point._beat )
            {
                point._stopSeconds += listSortedStop[stopIndex]._seconds;
                ++stopIndex;
            }
            point._secondsPerBeat = 60.0f / bpm;

            const bool bHasBpm  = bpmIndex < listSortedBpm.size();
            const bool bHasStop = stopIndex < listSortedStop.size();
            if ( bHasBpm == false && bHasStop == false )
                break;
            float32 nextBeat = bHasBpm ? listSortedBpm[bpmIndex]._beat : listSortedStop[stopIndex]._beat;
            if ( bHasStop )
                nextBeat = MathUtil::min( nextBeat, listSortedStop[stopIndex]._beat );

            RhythmTimingPoint next;
            next._beat = nextBeat;
            next._time = point._time + point._stopSeconds + ( nextBeat - point._beat ) * point._secondsPerBeat;
            _listTimingPoint.push_back( next );
        }
    }
} // namespace sw
