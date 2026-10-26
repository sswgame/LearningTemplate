#include "pch.h"

#include "Engine/Graphics/Debug/VisualLogger.h"

#include "Core/Common/Defines.h"
#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    SW_LOG_CALLER( "VisualLogger" );

    namespace
    {
        constexpr const utf8* kArrShapeName[] = { "Text", "Line", "Sphere", "Box", "Arrow" };

        const utf8* toShapeName( VisualLogShape shape )
        {
            return kArrShapeName[static_cast<size_t>( shape )];
        }

        [[nodiscard]] bool tryParseShape( string_view name, VisualLogShape& outShape )
        {
            for ( size_t index = 0; index < SW_COUNT_OF( kArrShapeName ); ++index )
            {
                if ( StringUtil::equals( name, string_view( kArrShapeName[index] ), true ) )
                {
                    outShape = static_cast<VisualLogShape>( index );
                    return true;
                }
            }
            return false;
        }

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
            VisualLogEntry entry;
            if ( tryParseShape( *shapeTxt, entry._shape ) == false )
                return false;
            float32    arrA[3]{};
            float32    arrB[3]{};
            float32    arrColor[4]{};
            const bool bFloatsBroken = readFloats( node, "a", arrA, 3 ) == false || readFloats( node, "b", arrB, 3 ) == false || readFloats( node, "color", arrColor, 4 ) == false;
            if ( bFloatsBroken )
                return false;
            entry._frame    = *frame;
            entry._objectID = *objectID;
            entry._category = hashed_string( category->c_str() );
            entry._text     = *text;
            entry._radius   = *radius;
            entry._a        = float3{ arrA[0], arrA[1], arrA[2] };
            entry._b        = float3{ arrB[0], arrB[1], arrB[2] };
            entry._color    = float4{ arrColor[0], arrColor[1], arrColor[2], arrColor[3] };
            outEntry        = std::move( entry );
            return true;
        }

        bool matchesFilter( const VisualLogEntry& entry, uint64 objectID, const hashed_string& category )
        {
            const bool bObjectMismatch   = objectID != 0 && entry._objectID != objectID;
            const bool bCategoryMismatch = category.empty() == false && entry._category != category;
            return bObjectMismatch == false && bCategoryMismatch == false;
        }
    } // namespace

    VisualLogger::VisualLogger()
        : _mutex{}
        , _listEntry{}
        , _frame{ 0 }
        , _capacity{ kDefaultCapacity }
        , _bEnabled{ SW_FALSE }
    {
    }

    void VisualLogger::setEnabled( bool bEnabled )
    {
        _bEnabled = bEnabled ? SW_TRUE : SW_FALSE;
    }

    bool VisualLogger::setCapacity( uint32 capacity )
    {
        if ( capacity == 0 )
        {
            SW_LOG_ERROR( "VisualLogger capacity must be at least 1 entry" );
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

    void VisualLogger::logText( uint64 objectID, const hashed_string& category, const float3& position, string_view text, const float4& color )
    {
        if ( isEnabled() == false )
            return;
        VisualLogEntry entry;
        entry._objectID = objectID;
        entry._category = category;
        entry._shape    = VisualLogShape::Text;
        entry._a        = position;
        entry._color    = color;
        entry._text     = string( text );
        append( std::move( entry ) );
    }

    void VisualLogger::logLine( uint64 objectID, const hashed_string& category, const float3& from, const float3& to, const float4& color, string_view note )
    {
        if ( isEnabled() == false )
            return;
        VisualLogEntry entry;
        entry._objectID = objectID;
        entry._category = category;
        entry._shape    = VisualLogShape::Line;
        entry._a        = from;
        entry._b        = to;
        entry._color    = color;
        entry._text     = string( note );
        append( std::move( entry ) );
    }

    void VisualLogger::logSphere( uint64 objectID, const hashed_string& category, const float3& center, float32 radius, const float4& color, string_view note )
    {
        if ( isEnabled() == false )
            return;
        VisualLogEntry entry;
        entry._objectID = objectID;
        entry._category = category;
        entry._shape    = VisualLogShape::Sphere;
        entry._a        = center;
        entry._radius   = radius;
        entry._color    = color;
        entry._text     = string( note );
        append( std::move( entry ) );
    }

    void VisualLogger::logBox( uint64 objectID, const hashed_string& category, const float3& center, const float3& halfExtent, const float4& color, string_view note )
    {
        if ( isEnabled() == false )
            return;
        VisualLogEntry entry;
        entry._objectID = objectID;
        entry._category = category;
        entry._shape    = VisualLogShape::Box;
        entry._a        = center;
        entry._b        = halfExtent;
        entry._color    = color;
        entry._text     = string( note );
        append( std::move( entry ) );
    }

    void VisualLogger::logArrow( uint64 objectID, const hashed_string& category, const float3& from, const float3& to, const float4& color, string_view note )
    {
        if ( isEnabled() == false )
            return;
        VisualLogEntry entry;
        entry._objectID = objectID;
        entry._category = category;
        entry._shape    = VisualLogShape::Arrow;
        entry._a        = from;
        entry._b        = to;
        entry._color    = color;
        entry._text     = string( note );
        append( std::move( entry ) );
    }

    void VisualLogger::collect( uint64 firstFrame, uint64 lastFrame, uint64 objectID, const hashed_string& category, vector<VisualLogEntry>& outListEntry ) const
    {
        outListEntry.clear();
        ScopedLock<mutex> lock{ _mutex };
        for ( const VisualLogEntry& entry : _listEntry )
        {
            const bool bInRange = firstFrame <= entry._frame && entry._frame <= lastFrame;
            if ( bInRange && matchesFilter( entry, objectID, category ) )
                outListEntry.push_back( entry );
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

    uint32 VisualLogger::replayFrame( DebugDrawQueue& queue, uint64 frame, uint64 objectID, const hashed_string& category ) const
    {
        vector<VisualLogEntry> listEntry;
        collect( frame, frame, objectID, category, listEntry );
        for ( const VisualLogEntry& entry : listEntry )
        {
            switch ( entry._shape )
            {
                case VisualLogShape::Text:
                {
                    queue.drawText( entry._a, entry._text, entry._color, 0.0f, entry._category );
                    break;
                }
                case VisualLogShape::Line:
                {
                    queue.drawLine( entry._a, entry._b, entry._color, 0.0f, entry._category );
                    break;
                }
                case VisualLogShape::Sphere:
                {
                    queue.drawSphere( entry._a, entry._radius, entry._color, 0.0f, entry._category );
                    break;
                }
                case VisualLogShape::Box:
                {
                    queue.drawBox( entry._a, entry._b, entry._color, 0.0f, entry._category );
                    break;
                }
                case VisualLogShape::Arrow:
                {
                    queue.drawArrow( entry._a, entry._b, entry._color, 0.0f, entry._category );
                    break;
                }
            }
            // 도형에 붙은 설명은 시작점에 글로 그린다(글 기록은 위에서 이미 그렸다).
            if ( entry._shape != VisualLogShape::Text && entry._text.empty() == false )
                queue.drawText( entry._a, entry._text, entry._color, 0.0f, entry._category );
        }
        return static_cast<uint32>( listEntry.size() );
    }

    bool VisualLogger::saveToFile( string_view diskPath ) const
    {
        JSONDocument document;
        JSONValue    root = document.makeObject();
        root.set( "version" ).setUint( kFileVersion );
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
            node.set( "shape" ).setString( toShapeName( entry._shape ) );
            node.set( "text" ).setString( entry._text );
            node.set( "radius" ).setFloat( static_cast<float64>( entry._radius ) );
            const float32 arrA[3]     = { entry._a._x, entry._a._y, entry._a._z };
            const float32 arrB[3]     = { entry._b._x, entry._b._y, entry._b._z };
            const float32 arrColor[4] = { entry._color._x, entry._color._y, entry._color._z, entry._color._w };
            writeFloats( node, "a", arrA, 3 );
            writeFloats( node, "b", arrB, 3 );
            writeFloats( node, "color", arrColor, 4 );
        }
        return document.saveFile( diskPath, 1 );
    }

    bool VisualLogger::loadFromFile( string_view diskPath )
    {
        JSONDocument document;
        if ( document.loadFile( diskPath ) == false )
        {
            SW_LOG_ERROR( "Visual log '%#' could not be read: %#", diskPath, document.getLastError().c_str() );
            return false;
        }
        const JSONValue root         = document.getRoot();
        const auto      version      = root.readRequired<uint32>( "version" );
        const auto      frame        = root.readRequired<uint64>( "frame" );
        const JSONValue listNode     = root.get( "entries" );
        const bool      bNotThisFile = version.has_value() == false || *version != kFileVersion || frame.has_value() == false || listNode.isArray() == false;
        if ( bNotThisFile )
        {
            SW_LOG_ERROR( "Visual log '%#' is not a version %# visual log file", diskPath, kFileVersion );
            return false;
        }
        deque<VisualLogEntry> listLoaded;
        for ( size_t index = 0; index < listNode.size(); ++index )
        {
            VisualLogEntry entry;
            if ( readEntry( listNode.at( index ), entry ) == false )
            {
                SW_LOG_ERROR( "Visual log '%#': entry %# is broken", diskPath, static_cast<uint32>( index ) );
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

#if SW_DEV_COMMANDS_ENABLED

namespace sw
{
    namespace
    {
        /** @brief `%#` 서식으로 답 글을 만듭니다. */
        template <typename... Args>
        string makeReply( string_view format, Args&&... args )
        {
            StringBuilder<constant::kMaxBuffer512> text;
            text.appendFormat( format, std::forward<Args>( args )... );
            return string( text.view() );
        }

        /** @brief 개발 명령 `vlog.*` 가 쓰는 로거입니다. 엔진 서비스가 묶이지 않았으면 답을 적고 nullptr 입니다. */
        VisualLogger* findLogger( string& outReply )
        {
            VisualLogger* pLogger = engine::areEngineServicesBound() ? engine::findSubsystem<VisualLogger>() : nullptr;
            if ( pLogger == nullptr )
                outReply = "vlog: the visual logger is not available";
            return pLogger;
        }

        bool runOn( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.empty() == false )
                return false;
            VisualLogger* pLogger = findLogger( outReply );
            if ( pLogger != nullptr )
            {
                pLogger->setEnabled( true );
                outReply = "vlog: recording";
            }
            return true;
        }

        bool runOff( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.empty() == false )
                return false;
            VisualLogger* pLogger = findLogger( outReply );
            if ( pLogger != nullptr )
            {
                pLogger->setEnabled( false );
                outReply = "vlog: stopped (the recorded entries are kept)";
            }
            return true;
        }

        bool runClear( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.empty() == false )
                return false;
            VisualLogger* pLogger = findLogger( outReply );
            if ( pLogger != nullptr )
            {
                pLogger->clear();
                outReply = "vlog: cleared";
            }
            return true;
        }

        bool runStatus( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.empty() == false )
                return false;
            VisualLogger* pLogger = findLogger( outReply );
            if ( pLogger == nullptr )
                return true;
            uint64     firstFrame = 0;
            uint64     lastFrame  = 0;
            const bool bHasRange  = pLogger->findFrameRange( firstFrame, lastFrame );
            outReply              = makeReply( "vlog: %#, %# entries, frames %#..%#, now frame %#", pLogger->isEnabled() ? "recording" : "stopped",
                                               static_cast<uint64>( pLogger->getEntryCount() ), bHasRange ? firstFrame : uint64{ 0 }, bHasRange ? lastFrame : uint64{ 0 }, pLogger->getFrame() );
            return true;
        }

        bool runSave( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.size() != 1 )
                return false;
            VisualLogger* pLogger = findLogger( outReply );
            if ( pLogger == nullptr )
                return true;
            outReply = pLogger->saveToFile( string_view( listArgument[0].c_str(), listArgument[0].size() ) ) ? "vlog: saved" : "vlog: could not write the file";
            return true;
        }

        bool runReplay( const vector<string>& listArgument, string& outReply )
        {
            if ( listArgument.size() != 1 )
                return false;
            int32 frame = 0;
            if ( StringUtil::parseInt( string_view( listArgument[0].c_str(), listArgument[0].size() ), frame ) == false || frame < 0 )
                return false;
            VisualLogger*   pLogger = findLogger( outReply );
            DebugDrawQueue* pQueue  = engine::areEngineServicesBound() ? engine::findSubsystem<DebugDrawQueue>() : nullptr;
            if ( pLogger == nullptr )
                return true;
            if ( pQueue == nullptr )
            {
                outReply = "vlog: no debug draw queue";
                return true;
            }
            const uint32 count = pLogger->replayFrame( *pQueue, static_cast<uint64>( frame ), 0, hashed_string{} );
            outReply           = makeReply( "vlog: replayed %# entries of frame %#", count, static_cast<uint64>( frame ) );
            return true;
        }
    } // namespace

    SW_DEV_COMMAND( VisualLogOn, "vlog.on", "vlog.on", "Start recording visual log entries", &runOn );
    SW_DEV_COMMAND( VisualLogOff, "vlog.off", "vlog.off", "Stop recording visual log entries (kept)", &runOff );
    SW_DEV_COMMAND( VisualLogClear, "vlog.clear", "vlog.clear", "Drop every recorded visual log entry", &runClear );
    SW_DEV_COMMAND( VisualLogStatus, "vlog.status", "vlog.status", "Show whether the visual log records and how many entries it holds", &runStatus );
    SW_DEV_COMMAND( VisualLogSave, "vlog.save", "vlog.save <file.vlog.json>", "Write the recorded visual log to a file", &runSave );
    SW_DEV_COMMAND( VisualLogReplay, "vlog.replay", "vlog.replay <frame>", "Draw the shapes of a recorded frame through the debug draw queue for one frame", &runReplay );
} // namespace sw

#endif
