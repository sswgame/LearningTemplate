#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/Rhythm/RhythmChart.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Foundation/GameData/GameDataXML.h"

#include <algorithm>
#include <optional>

namespace sw
{
    SW_LOG_CALLER( "RhythmChart" );

    namespace
    {
        struct RhythmChartInternal
        {

            static bool isNoteBefore( const RhythmNote& lhs, const RhythmNote& rhs )
            {
                if ( lhs._beat != rhs._beat )
                    return lhs._beat < rhs._beat;
                return lhs._lane < rhs._lane;
            }

            static bool isBpmChangeBefore( const RhythmBpmChange& lhs, const RhythmBpmChange& rhs ) { return lhs._beat < rhs._beat; }
            static bool isStopBefore( const RhythmStop& lhs, const RhythmStop& rhs ) { return lhs._beat < rhs._beat; }
            static bool isMeasureChangeBefore( const RhythmMeasureChange& lhs, const RhythmMeasureChange& rhs ) { return lhs._beat < rhs._beat; }

            /** @brief `_beat` 가 @p beat 보다 작은 점의 수를 반환합니다(이분 탐색). */
            static size_t countPointsBeforeBeat( const vector<RhythmTimingPoint>& listPoint, float32 beat )
            {
                const auto found = std::lower_bound( listPoint.begin(), listPoint.end(), beat, []( const RhythmTimingPoint& point, float32 value )
                { return point._beat < value; } );
                return static_cast<size_t>( found - listPoint.begin() );
            }

            /** @brief `_time` 이 @p seconds 이하인 점의 수를 반환합니다(이분 탐색). */
            static size_t countPointsUpToTime( const vector<RhythmTimingPoint>& listPoint, float32 seconds )
            {
                const auto found = std::upper_bound( listPoint.begin(), listPoint.end(), seconds, []( float32 value, const RhythmTimingPoint& point )
                { return value < point._time; } );
                return static_cast<size_t>( found - listPoint.begin() );
            }

            /**
             * @brief `<Note>` 하나를 읽어 @p outListNote 끝에 붙입니다. `endBeat` 를 적지 않으면 탭 노트입니다.
             * @return 필드가 틀렸거나, 레인이 @p laneCount 밖이거나, 끝이 시작보다 앞이면 붙이지 않고 false 입니다(이유는 로그로 남긴다).
             */
            [[nodiscard]] static bool readNote( const XMLNode& node, string_view sourceName, int32 laneCount, vector<RhythmNote>& outListNote )
            {
                RhythmNoteDef          noteDef;
                std::optional<float32> endBeat;
                const bool             bFields = GameDataXML::readFields( node, noteDef, sourceName, { "endBeat" } );
                const bool             bEnd    = node.readOptionalAttribute( "endBeat", endBeat );
                const bool             bLane   = noteDef._lane < laneCount;
                const bool             bOrder  = endBeat.has_value() == false || *endBeat >= noteDef._beat;
                if ( bLane == false )
                    SW_LOG_ERROR( "%#: note at beat %# is on lane %# outside 0..%#", sourceName, noteDef._beat, noteDef._lane, laneCount - 1 );
                if ( bOrder == false )
                    SW_LOG_ERROR( "%#: note at beat %# ends at %# before it starts", sourceName, noteDef._beat, *endBeat );
                const bool bNote = bFields && bEnd && bLane && bOrder;
                if ( bNote == false )
                    return false;
                RhythmNote note;
                note._lane    = noteDef._lane;
                note._beat    = noteDef._beat;
                note._endBeat = endBeat.has_value() ? *endBeat : noteDef._beat;
                note._bLong   = note._endBeat > note._beat ? SW_TRUE : SW_FALSE;
                outListNote.push_back( note );
                return true;
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
        , _baseBpm{ RhythmChartRootDef{}._bpm }
        , _endTime{ 0.0f }
        , _level{ 0 }
        , _laneCount{ 7 }
        , _judgmentCount{ 0 }
    {
        makeTimingPoints( {}, {} );
    }

    bool RhythmChart::loadFromAsset( const AssetID& assetID )
    {
        return GameDataXML::loadFile( *this, &RhythmChart::loadRoot, assetID, "Chart" );
    }

    bool RhythmChart::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        return GameDataXML::loadText( *this, &RhythmChart::loadRoot, xmlText, sourceName, "Chart" );
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

    bool RhythmChart::loadRoot( const XMLNode& root, string_view sourceName )
    {
        // 칸의 기본값은 Def 의 초기값, 범위는 `PROPERTY( Min · Max )` 다. 틀린 원소가 하나라도 있으면 차트를 읽지 못한 것으로 본다(반쪽 차트를 치게 하지 않는다).
        RhythmChartRootDef rootDef;
        bool               bValid = GameDataXML::readFields( root, rootDef, sourceName, { "baseBpm" } );
        _title                    = rootDef._title;
        _artist                   = rootDef._artist;
        _level                    = rootDef._level;
        _laneCount                = rootDef._laneCount;
        _offset                   = rootDef._offset;

        vector<RhythmBpmChange> listBpmChange;
        vector<RhythmStop>      listStop;
        _listMeasureChange.clear();
        _listNote.clear();
        for ( XMLNode node = root.findChild(); node; node = node.findNextSibling() )
        {
            const string_view name = node.getName() != nullptr ? string_view( node.getName() ) : string_view{};
            if ( name == "Bpm" )
                bValid = GameDataXML::readInto( node, sourceName, listBpmChange ) && bValid;
            else if ( name == "Stop" )
                bValid = GameDataXML::readInto( node, sourceName, listStop ) && bValid;
            else if ( name == "Measure" )
                bValid = GameDataXML::readInto( node, sourceName, _listMeasureChange ) && bValid;
            else if ( name == "Note" )
                bValid = RhythmChartInternal::readNote( node, sourceName, _laneCount, _listNote ) && bValid;
            else
            {
                SW_LOG_ERROR( "%#: unknown element <%#> - skipped", sourceName, name );
                bValid = false;
            }
        }
        // `<Bpm>` 이 없으면 루트의 bpm 하나가 처음부터 끝까지의 빠르기다.
        if ( listBpmChange.empty() )
        {
            RhythmBpmChange change;
            change._bpm = rootDef._bpm;
            listBpmChange.push_back( change );
        }
        makeTimingPoints( listBpmChange, listStop );
        // 스크롤 기준 BPM 은 적지 않으면 0 박의 BPM 이다(곡의 첫 빠르기를 기준으로 흐른다).
        std::optional<float32> baseBpm;
        const bool             bBaseBpm = root.readOptionalAttribute( "baseBpm", baseBpm ) && ( baseBpm.has_value() == false || *baseBpm >= 1.0f );
        if ( bBaseBpm == false )
        {
            SW_LOG_ERROR( "%#: <Chart baseBpm=\"%#\"> needs a number of 1 or more", sourceName, root.getAttributeText( "baseBpm" ) );
            bValid = false;
        }
        _baseBpm = baseBpm.has_value() ? *baseBpm : findBpmAt( 0.0f );
        std::stable_sort( _listMeasureChange.begin(), _listMeasureChange.end(), &RhythmChartInternal::isMeasureChangeBefore );

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
                SW_LOG_ERROR( "%#: note at beat %# on lane %# overlaps the previous note", sourceName, note._beat, note._lane );
                bValid = false;
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
            SW_LOG_ERROR( "%#: chart has no playable notes", sourceName );
        return bValid && _listNote.empty() == false;
    }

    void RhythmChart::makeTimingPoints( const vector<RhythmBpmChange>& listBpmChange, const vector<RhythmStop>& listStop )
    {
        vector<RhythmBpmChange> listSortedBpm = listBpmChange;
        std::stable_sort( listSortedBpm.begin(), listSortedBpm.end(), &RhythmChartInternal::isBpmChangeBefore );
        vector<RhythmStop> listSortedStop = listStop;
        std::stable_sort( listSortedStop.begin(), listSortedStop.end(), &RhythmChartInternal::isStopBefore );

        // 박 0 의 BPM 은 박 0 이하의 마지막 변화이고, 없으면 첫 변화이다.
        float32 bpm = listSortedBpm.empty() ? RhythmChartRootDef{}._bpm : listSortedBpm.front()._bpm;
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
