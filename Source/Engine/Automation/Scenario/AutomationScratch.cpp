#include "pch.h"

#include "Engine/Automation/Scenario/AutomationScratch.h"

#include "Core/String/StringUtil.h"

#include "Engine/Content/AssetPaths.h"

namespace sw
{
    namespace
    {
        struct AutomationScratchInternal
        {
            /** @brief 리소스 id 에 들어가는 글자인지입니다(id 경계 판정). */
            static bool isIDCharacter( utf8 character )
            {
                const bool bLetterOrDigit = ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ) || ( '0' <= character && character <= '9' );
                return bLetterOrDigit || character == '_' || character == '.' || character == '/' || character == '-';
            }
        };
    } // namespace

    expected<void> AutomationScratchFolder::validate() const
    {
        fixed_string<constant::buffer::kMax64> scratchPrefix;
        formatstring( scratchPrefix.data(), scratchPrefix.capacity(), "%#/", AutomationScratch::kScratchDomain );
        if ( _sourceID.empty() || _scratchID.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "<Scratch> needs from=\"<resource id>\" and as=\"scratch/<name>\"" } );
        const bool bLeavesTree = AssetPaths::hasParentDirectoryComponent( _sourceID ) || AssetPaths::hasParentDirectoryComponent( _scratchID );
        if ( bLeavesTree )
            return unexpected( Error{ ErrorKind::InvalidArgument, "<Scratch> ids may not use '..'" } );
        if ( StringUtil::startsWith( _scratchID, scratchPrefix ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "<Scratch as> must sit under scratch/", _scratchID } );
        if ( StringUtil::startsWith( _sourceID, scratchPrefix ) )
            return unexpected( Error{ ErrorKind::InvalidArgument, "<Scratch from> must be a tracked folder, not another scratch copy", _sourceID } );
        return {};
    }

    string AutomationScratch::rebaseText( string_view text, string_view fromID, string_view toID )
    {
        string result;
        result.reserve( text.size() );
        size_t cursor = 0;
        while ( cursor < text.size() )
        {
            const size_t hit = text.find( fromID, cursor );
            if ( hit == string_view::npos || fromID.empty() )
                break;
            const size_t end            = hit + fromID.size();
            const bool   bStartBoundary = hit == 0 || AutomationScratchInternal::isIDCharacter( text[hit - 1] ) == false;
            const bool   bEndBoundary   = end == text.size() || text[end] == '/' || AutomationScratchInternal::isIDCharacter( text[end] ) == false;
            const bool   bAtBoundary    = bStartBoundary && bEndBoundary;
            result.append( text.substr( cursor, hit - cursor ) );
            result.append( bAtBoundary ? toID : fromID );
            cursor = end;
        }
        result.append( text.substr( cursor ) ); // 찾은 끝(`hit + fromID.size()`)은 글 길이를 넘지 않는다
        return result;
    }
} // namespace sw
