#include "pch.h"

#include "Engine/Console/DebugHUDRegistry.h"

#if SW_DEV_COMMANDS_ENABLED

    #include "Core/Container/StringUtil.h"
    #include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "DebugHUD" );

    namespace
    {
        struct DebugHUDRegistryInternal
        {
            /** @brief 명령 `hud` 의 낱말이라 섹션 이름으로 쓰지 않는 것입니다. */
            static constexpr const utf8* kArrReservedName[] = { "on", "off", "list", "corner", "opacity", "window" };

            static bool isSeparator( utf8 character ) { return character == ' ' || character == ',' || character == '\t'; }

            /** @brief @p text 의 @p offset 부터 다음 낱말을 찾습니다. 없으면 false 입니다. */
            static bool findNextToken( string_view text, size_t& inoutOffset, string_view& outToken )
            {
                size_t begin = inoutOffset;
                while ( begin < text.size() && isSeparator( text[begin] ) )
                {
                    ++begin;
                }
                if ( begin >= text.size() )
                    return false;
                size_t end = begin;
                while ( end < text.size() && isSeparator( text[end] ) == false )
                {
                    ++end;
                }
                outToken    = text.substr( begin, end - begin );
                inoutOffset = end;
                return true;
            }

            /** @brief 낱말 @p token 이 @p name 의 것이면 true 이고, 켬 · 끔을 @p outShown 에 씁니다. */
            static bool matchesToken( string_view token, string_view name, bool& outShown )
            {
                const bool        bOff     = token.empty() == false && token[0] == '-';
                const string_view bareName = bOff ? token.substr( 1 ) : token;
                if ( StringUtil::equals( bareName, name, true ) == false )
                    return false;
                outShown = bOff == false;
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    DebugHUDSectionWriter::DebugHUDSectionWriter()
        : _listLine{}
        , _listGraph{}
        , _arrFrameSecond{}
        , _graphMax{ 0.0f }
        , _lineCount{ 0 }
        , _frameHistoryCount{ 0 }
    {
    }

    void DebugHUDSectionWriter::reset()
    {
        _lineCount = 0;
        _listGraph.clear();
        _graphMax = 0.0f;
    }

    void DebugHUDSectionWriter::addLine( string_view label, string_view value )
    {
        if ( _lineCount == _listLine.size() )
            _listLine.emplace_back();
        DebugHUDLine& line = _listLine[_lineCount];
        line._label.assign( label.data(), label.size() );
        line._value.assign( value.data(), value.size() );
        ++_lineCount;
    }

    void DebugHUDSectionWriter::addGraph( const float32* pValue, uint32 count, float32 maxValue )
    {
        _listGraph.assign( pValue, pValue + count );
        _graphMax = maxValue;
    }

    void DebugHUDSectionWriter::recordFrameSeconds( float32 seconds )
    {
        if ( _frameHistoryCount < kFrameHistoryCount )
        {
            _arrFrameSecond[_frameHistoryCount] = seconds;
            ++_frameHistoryCount;
            return;
        }
        for ( uint32 index = 1; index < kFrameHistoryCount; ++index )
        {
            _arrFrameSecond[index - 1] = _arrFrameSecond[index];
        }
        _arrFrameSecond[kFrameHistoryCount - 1] = seconds;
    }
} // namespace sw

namespace sw
{
    DebugHUDRegistry& DebugHUDRegistry::get()
    {
        static DebugHUDRegistry s_registry;
        return s_registry;
    }

    DebugHUDRegistry::DebugHUDRegistry()
        : _registration{ RegistrationOrder::ByOrderThenName, true, NameCase::IgnoreCase }
        , _revision{ 0 }
    {
    }

    bool DebugHUDRegistry::isReservedName( string_view name )
    {
        for ( const utf8* pReserved : DebugHUDRegistryInternal::kArrReservedName )
        {
            if ( StringUtil::equals( name, pReserved, true ) )
                return true;
        }
        return false;
    }

    bool DebugHUDRegistry::registerSection( const DebugHUDSectionRegistration* pRegistration )
    {
        if ( pRegistration == nullptr || StringUtil::isNullOrEmpty( pRegistration->_pName ) || pRegistration->_pFunc == nullptr )
            return false;
        const string_view name          = pRegistration->_pName;
        bool              bHasSeparator = name[0] == '-';
        for ( const utf8 character : name )
        {
            bHasSeparator = bHasSeparator || DebugHUDRegistryInternal::isSeparator( character );
        }
        if ( bHasSeparator || isReservedName( name ) )
        {
            SW_LOG_WARNING( "Debug HUD section '%#' has a reserved name or a separator - the registration is ignored", pRegistration->_pName );
            return false;
        }
        const RegistrationResult result = _registration.add( pRegistration, name, pRegistration->_order );
        if ( result == RegistrationResult::DuplicateName || result == RegistrationResult::AlreadyPresent )
        {
            SW_LOG_WARNING( "Debug HUD section '%#' is already registered - the second registration is ignored", pRegistration->_pName );
            return false;
        }
        if ( result != RegistrationResult::Added )
            return false;
        ++_revision;
        return true;
    }

    void DebugHUDRegistry::unregisterSection( const DebugHUDSectionRegistration* pRegistration )
    {
        if ( _registration.remove( pRegistration ) )
            ++_revision;
    }

    const DebugHUDSectionRegistration* DebugHUDRegistry::findSection( string_view name ) const
    {
        return _registration.findByName( name );
    }
} // namespace sw

namespace sw
{
    bool DebugHUDSectionState::isSectionShown( string_view stateText, const DebugHUDSectionRegistration& registration )
    {
        bool        bShown = ( registration._flags & DebugHUDSectionFlag::kDefaultOn ) != 0;
        size_t      offset = 0;
        string_view token{};
        while ( DebugHUDRegistryInternal::findNextToken( stateText, offset, token ) )
        {
            bool bTokenShown = false;
            if ( DebugHUDRegistryInternal::matchesToken( token, registration._pName, bTokenShown ) )
                bShown = bTokenShown;
        }
        return bShown;
    }

    string DebugHUDSectionState::makeStateText( string_view stateText, string_view name, bool bShown, bool bDefaultShown )
    {
        string      result;
        size_t      offset = 0;
        string_view token{};
        while ( DebugHUDRegistryInternal::findNextToken( stateText, offset, token ) )
        {
            bool bTokenShown = false;
            if ( DebugHUDRegistryInternal::matchesToken( token, name, bTokenShown ) )
                continue;
            if ( result.empty() == false )
                result += ' ';
            result.append( token.data(), token.size() );
        }
        if ( bShown == bDefaultShown )
            return result;
        if ( result.empty() == false )
            result += ' ';
        if ( bShown == false )
            result += '-';
        result.append( name.data(), name.size() );
        return result;
    }
} // namespace sw

namespace sw
{
    DebugHUDSectionRegistrar::DebugHUDSectionRegistrar( const DebugHUDSectionRegistration* pRegistration )
        : _pRegistration{ pRegistration }
        , _bRegistered{ DebugHUDRegistry::get().registerSection( pRegistration ) }
    {
    }

    DebugHUDSectionRegistrar::~DebugHUDSectionRegistrar()
    {
        if ( _bRegistered )
            DebugHUDRegistry::get().unregisterSection( _pRegistration );
    }
} // namespace sw

#endif
