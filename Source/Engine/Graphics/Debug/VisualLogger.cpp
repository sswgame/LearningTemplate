#include "pch.h"

#include "Engine/Graphics/Debug/VisualLogger.h"

#include "Core/Foundation/Constants.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/DebugDraw/DebugDrawQueue.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    namespace
    {
        void writeFloats( const JSONValue& parent, string_view key, const float32* pValue, uint32 count )
        {
            JSONValue array = parent.set( key );
            array.setArray();
            for ( uint32 index = 0; index < count; ++index )
            {
                array.pushBack().setFloat( static_cast<float64>( pValue[index] ) );
            }
        }

        [[nodiscard]] bool readFloats( const JSONValue& parent, string_view key, float32* pOutValue, uint32 count )
        {
            const JSONValue array = parent.get( key );
            if ( array.isArray() == false || array.size() != count )
                return false;
            for ( uint32 index = 0; index < count; ++index )
            {
                const auto value = array.at( index ).read<float32>();
                if ( value.has_value() == false )
                    return false;
                pOutValue[index] = *value;
            }
            return true;
        }

        [[nodiscard]] bool readEntry( const JSONValue& node, VisualLogEntry& outEntry )
        {
            const auto frame         = node.readRequired<uint64>( "frame" );
            const auto objectID      = node.readRequired<uint64>( "object" );
            const auto shapeTxt      = node.readRequired<string>( "shape" );
            const auto category      = node.readRequired<string>( "category" );
            const auto text          = node.readRequired<string>( "text" );
            const auto radius        = node.readRequired<float32>( "radius" );
            const bool bMissingField = frame.has_value() == false || objectID.has_value() == false || shapeTxt.has_value() == false || category.has_value() == false ||
                                       text.has_value() == false || radius.has_value() == false;
            if ( bMissingField )
                return false;
            VisualLogEntry                 entry;
            const optional<VisualLogShape> shape = tryParseEnum<VisualLogShape>( *shapeTxt );
            if ( shape.has_value() == false )
                return false;
            entry._shape = *shape;
            float32    arrPosition[3]{};
            float32    arrExtent[3]{};
            float32    arrColor[4]{};
            const bool bFloatsBroken = readFloats( node, "position", arrPosition, 3 ) == false || readFloats( node, "extent", arrExtent, 3 ) == false || readFloats( node, "color", arrColor, 4 ) == false;
            if ( bFloatsBroken )
                return false;
            entry._frame    = *frame;
            entry._objectID = *objectID;
            entry._category = hashed_string( category->c_str() );
            entry._text     = *text;
            entry._radius   = *radius;
            entry._position = AbsolutePosition{
                float3{ arrPosition[0], arrPosition[1], arrPosition[2] }
            };
            entry._extent = float3{ arrExtent[0], arrExtent[1], arrExtent[2] };
            entry._color  = float4{ arrColor[0], arrColor[1], arrColor[2], arrColor[3] };
            outEntry      = std::move( entry );
            return true;
        }

        bool matchesFilter( const VisualLogEntry& entry, uint64 objectID, const hashed_string& category )
        {
            const bool bObjectMismatch   = objectID != 0 && entry._objectID != objectID;
            const bool bCategoryMismatch = category.empty() == false && entry._category != category;
            return bObjectMismatch == false && bCategoryMismatch == false;
        }
    } // namespace

    /** @brief 기록을 켭니다. 개발 명령 · 도구의 켬 스위치는 전역 변수다 — 자동화 단계가 시작 값으로 되돌린다. */
    SW_GLOBAL_VARIABLE( bool, gv_visualLogger, false, "비주얼 로그 기록을 켠다 (개발 명령 vlog 와 같다)" );

    VisualLogger::VisualLogger()
        : _mutex{}
        , _listEntry{}
        , _frame{ 0 }
        , _capacity{ kDefaultCapacity }
    {
    }

    void VisualLogger::setEnabled( bool bEnabled )
    {
        gv_visualLogger = bEnabled;
    }

    bool VisualLogger::isEnabled() const
    {
        return gv_visualLogger;
    }

    void VisualLogger::resetGameInstanceState( GameInstanceBoundary boundary )
    {
        if ( boundary == GameInstanceBoundary::Starting )
            clear(); // 기록은 게임이 끝난 뒤에 읽는다 — 다음 인스턴스가 시작할 때 비운다
    }

    const utf8* VisualLogger::findGameInstanceState() const
    {
        return getEntryCount() == 0 ? nullptr : "visual log entries";
    }

    bool VisualLogger::setCapacity( uint32 capacity )
    {
        if ( capacity == 0 )
        {
            SW_LOG_ERROR_ID( "VisualLogger.VisualLoggerCapacityMustLeast", "VisualLogger capacity must be at least 1 entry" );
            return false;
        }
        ScopedLock<mutex> lock{ _mutex };
        _capacity = capacity;
        trimLocked();
        return true;
    }

    void VisualLogger::endFrame()
    {
        ScopedLock<mutex> lock{ _mutex };
        ++_frame;
    }

    uint64 VisualLogger::getFrame() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _frame;
    }

    void VisualLogger::clear()
    {
        ScopedLock<mutex> lock{ _mutex };
        _listEntry.clear();
    }

    size_t VisualLogger::getEntryCount() const
    {
        ScopedLock<mutex> lock{ _mutex };
        return _listEntry.size();
    }

    void VisualLogger::trimLocked()
    {
        while ( _listEntry.size() > _capacity )
        {
            _listEntry.pop_front();
        }
    }

    void VisualLogger::append( VisualLogEntry entry )
    {
        if ( isEnabled() == false )
            return;
        if ( entry._category.empty() )
            entry._category = hashed_string( kDefaultCategoryName );
        ScopedLock<mutex> lock{ _mutex };
        entry._frame = _frame;
        _listEntry.push_back( std::move( entry ) );
        trimLocked();
    }

    void VisualLogger::logText( uint64 objectID, const hashed_string& category, const AbsolutePosition& position, string_view text, const float4& color )
    {
        if ( isEnabled() == false )
            return;
        VisualLogEntry entry;
        entry._objectID = objectID;
        entry._category = category;
        entry._shape    = VisualLogShape::Text;
        entry._position = position;
        entry._color    = color;
        entry._text     = string( text );
        append( std::move( entry ) );
    }

    void VisualLogger::logLine( uint64 objectID, const hashed_string& category, const AbsolutePosition& from, const AbsolutePosition& to, const float4& color, string_view note )
    {
        if ( isEnabled() == false )
            return;
        VisualLogEntry entry;
        entry._objectID = objectID;
        entry._category = category;
        entry._shape    = VisualLogShape::Line;
        entry._position = from;
        entry._extent   = to._value - from._value;
        entry._color    = color;
        entry._text     = string( note );
        append( std::move( entry ) );
    }

    void VisualLogger::logSphere( uint64 objectID, const hashed_string& category, const AbsolutePosition& center, float32 radius, const float4& color, string_view note )
    {
        if ( isEnabled() == false )
            return;
        VisualLogEntry entry;
        entry._objectID = objectID;
        entry._category = category;
        entry._shape    = VisualLogShape::Sphere;
        entry._position = center;
        entry._radius   = radius;
        entry._color    = color;
        entry._text     = string( note );
        append( std::move( entry ) );
    }

    void VisualLogger::logBox( uint64 objectID, const hashed_string& category, const AbsolutePosition& center, const float3& halfExtent, const float4& color, string_view note )
    {
        if ( isEnabled() == false )
            return;
        VisualLogEntry entry;
        entry._objectID = objectID;
        entry._category = category;
        entry._shape    = VisualLogShape::Box;
        entry._position = center;
        entry._extent   = halfExtent;
        entry._color    = color;
        entry._text     = string( note );
        append( std::move( entry ) );
    }

    void VisualLogger::logArrow( uint64 objectID, const hashed_string& category, const AbsolutePosition& from, const AbsolutePosition& to, const float4& color, string_view note )
    {
        if ( isEnabled() == false )
            return;
        VisualLogEntry entry;
        entry._objectID = objectID;
        entry._category = category;
        entry._shape    = VisualLogShape::Arrow;
        entry._position = from;
        entry._extent   = to._value - from._value;
        entry._color    = color;
        entry._text     = string( note );
        append( std::move( entry ) );
    }

    void VisualLogger::collect( uint64 firstFrame, uint64 lastFrame, uint64 objectID, const hashed_string& category, vector<VisualLogEntry>& outListEntry ) const
    {
        outListEntry.clear();
        ScopedLock<mutex> lock{ _mutex };
        // 기록은 프레임 순으로 쌓인다 — HUD 범주가 프레임마다 이번 프레임만 묻는다.
        const auto first = std::lower_bound( _listEntry.begin(), _listEntry.end(), firstFrame, []( const VisualLogEntry& entry, uint64 frame )
        { return entry._frame < frame; } );
        for ( auto iterator = first; iterator != _listEntry.end() && iterator->_frame <= lastFrame; ++iterator )
        {
            if ( matchesFilter( *iterator, objectID, category ) )
                outListEntry.push_back( *iterator );
        }
    }

    bool VisualLogger::findFrameRange( uint64& outFirstFrame, uint64& outLastFrame ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _listEntry.empty() )
            return false;
        outFirstFrame = _listEntry.front()._frame; // 기록은 프레임 순으로 쌓인다
        outLastFrame  = _listEntry.back()._frame;
        return true;
    }

    uint32 VisualLogger::replayFrame( DebugDrawQueue& queue, const WorldOrigin& origin, uint64 frame, uint64 objectID, const hashed_string& category ) const
    {
        vector<VisualLogEntry> listEntry;
        collect( frame, frame, objectID, category, listEntry );
        for ( const VisualLogEntry& entry : listEntry )
        {
            switch ( entry._shape )
            {
                case VisualLogShape::Text:
                {
                    queue.drawText( origin, entry._position, entry._text, entry._color, 0.0f, entry._category );
                    break;
                }
                case VisualLogShape::Line:
                {
                    queue.drawLine( origin, entry._position, entry._position + entry._extent, entry._color, 0.0f, entry._category );
                    break;
                }
                case VisualLogShape::Sphere:
                {
                    queue.drawSphere( origin, entry._position, entry._radius, entry._color, 0.0f, entry._category );
                    break;
                }
                case VisualLogShape::Box:
                {
                    queue.drawBox( origin, entry._position, entry._extent, entry._color, 0.0f, entry._category );
                    break;
                }
                case VisualLogShape::Arrow:
                {
                    queue.drawArrow( origin, entry._position, entry._position + entry._extent, entry._color, 0.0f, entry._category );
                    break;
                }
            }
            // 도형에 붙은 설명은 시작점에 글로 그린다(글 기록은 위에서 이미 그렸다).
            if ( entry._shape != VisualLogShape::Text && entry._text.empty() == false )
                queue.drawText( origin, entry._position, entry._text, entry._color, 0.0f, entry._category );
        }
        return static_cast<uint32>( listEntry.size() );
    }

    bool VisualLogger::saveToFile( const AbsolutePath& absPath ) const
    {
        JSONDocument document;
        JSONValue    root = document.makeObject();
        root.set( "version" ).setUint( kFileFormat._version );
        {
            ScopedLock<mutex> lock{ _mutex };
            root.set( "frame" ).setUint( _frame );
        }
        JSONValue listNode = root.set( "entries" );
        listNode.setArray();
        vector<VisualLogEntry> listEntry;
        {
            ScopedLock<mutex> lock{ _mutex };
            listEntry.assign( _listEntry.begin(), _listEntry.end() );
        }
        for ( const VisualLogEntry& entry : listEntry )
        {
            JSONValue node = listNode.pushBack();
            node.setObject();
            node.set( "frame" ).setUint( entry._frame );
            node.set( "object" ).setUint( entry._objectID );
            node.set( "category" ).setString( entry._category.c_str() );
            node.set( "shape" ).setString( getEnumName( entry._shape ) );
            node.set( "text" ).setString( entry._text );
            node.set( "radius" ).setFloat( static_cast<float64>( entry._radius ) );
            const float32 arrPosition[3] = { entry._position._value._x, entry._position._value._y, entry._position._value._z };
            const float32 arrExtent[3]   = { entry._extent._x, entry._extent._y, entry._extent._z };
            const float32 arrColor[4]    = { entry._color._x, entry._color._y, entry._color._z, entry._color._w };
            writeFloats( node, "position", arrPosition, 3 );
            writeFloats( node, "extent", arrExtent, 3 );
            writeFloats( node, "color", arrColor, 4 );
        }
        return document.saveToFile( absPath, 1 );
    }

    bool VisualLogger::loadFromFile( const AbsolutePath& diskPath )
    {
        JSONDocument document;
        if ( document.loadFromFile( diskPath ) == false )
        {
            SW_LOG_ERROR_ID( "VisualLogger.VisualLogCouldNot", "Visual log '%#' could not be read: %#", diskPath.str(), document.getLastError().c_str() );
            return false;
        }
        const JSONValue root         = document.getRoot();
        const auto      version      = root.readRequired<uint32>( "version" );
        const auto      frame        = root.readRequired<uint64>( "frame" );
        const JSONValue listNode     = root.get( "entries" );
        const bool      bNotThisFile = version.has_value() == false || *version != kFileFormat._version || frame.has_value() == false || listNode.isArray() == false;
        if ( bNotThisFile )
        {
            SW_LOG_ERROR_ID( "VisualLogger.VisualLogNotVersion", "Visual log '%#' is not a version %# visual log file", diskPath.str(), kFileFormat._version );
            return false;
        }
        deque<VisualLogEntry> listLoaded;
        for ( size_t index = 0; index < listNode.size(); ++index )
        {
            VisualLogEntry entry;
            if ( readEntry( listNode.at( index ), entry ) == false )
            {
                SW_LOG_ERROR_ID( "VisualLogger.VisualLogEntryBroken", "Visual log '%#': entry %# is broken", diskPath.str(), static_cast<uint32>( index ) );
                return false;
            }
            listLoaded.push_back( std::move( entry ) );
        }
        ScopedLock<mutex> lock{ _mutex };
        _listEntry = std::move( listLoaded );
        _frame     = *frame;
        trimLocked();
        return true;
    }
} // namespace sw

namespace sw
{
    SW_ENGINE_SUBSYSTEM_WITH_SLOT( VisualLogger, GameVisible, GameInstance, Shared, (), SW_ENGINE_DEFAULT_FACTORY( VisualLogger ) );
} // namespace sw
