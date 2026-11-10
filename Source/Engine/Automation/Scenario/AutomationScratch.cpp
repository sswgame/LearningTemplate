#include "pch.h"

#include "Engine/Automation/Scenario/AutomationScratch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"
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

            /** @brief @p text 가 비지 않은 10 진 숫자만이면 그 값을 @p outValue 에 넣고 true 입니다. */
            [[nodiscard]] static bool parseProcessID( string_view text, int32& outValue )
            {
                const bool bDigits = text.empty() == false && VectorUtil::allOf( text, []( utf8 character )
                {
                    return '0' <= character && character <= '9';
                } );
                return bDigits && StringUtil::parseInt( text, outValue );
            }
        };
    } // namespace

    expected<void> AutomationScratch::isValidFolder( const AutomationScratchFolder& folder )
    {
        const string scratchPrefix = string( kScratchDomain ) + "/";
        if ( folder._sourceID.empty() || folder._scratchID.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "<Scratch> needs from=\"<resource id>\" and as=\"scratch/<name>\"" } );
        const bool bLeavesTree = AssetPaths::hasParentDirectoryComponent( folder._sourceID ) || AssetPaths::hasParentDirectoryComponent( folder._scratchID );
        if ( bLeavesTree )
            return unexpected( Error{ ErrorKind::InvalidArgument, "<Scratch> ids may not use '..'" } );
        if ( StringUtil::startsWith( folder._scratchID, scratchPrefix ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "<Scratch as> must sit under scratch/", folder._scratchID } );
        if ( StringUtil::startsWith( folder._sourceID, scratchPrefix ) )
            return unexpected( Error{ ErrorKind::InvalidArgument, "<Scratch from> must be a tracked folder, not another scratch copy", folder._sourceID } );
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

    string AutomationScratch::makeInstanceID( string_view name )
    {
        return string( name ) + string( kInstanceMark ) + to_string( Process::getCurrentProcessID() );
    }

    bool AutomationScratch::isOtherProcessInstance( string_view assetID )
    {
        const string_view domain = kScratchDomain;
        if ( StringUtil::startsWith( assetID, domain ) == false || assetID.size() <= domain.size() || assetID[domain.size()] != '/' )
            return false;
        const string_view     rest    = assetID.substr( domain.size() + 1 );
        const string_view     folder  = rest.substr( 0, rest.find( '/' ) );
        const optional<int32> ownerID = findInstanceProcessID( folder );
        return ownerID.has_value() && *ownerID != static_cast<int32>( Process::getCurrentProcessID() );
    }

    void AutomationScratch::removeOtherProcessInstances( string_view contentRoot, vector<string>& inoutListPath )
    {
        (void)VectorUtil::removeIf( inoutListPath, [contentRoot]( const string& path )
        {
            string relativePath;
            return FileUtil::makeRelativePath( contentRoot, path, relativePath ) && isOtherProcessInstance( FileUtil::normalizeSeparators( relativePath ) );
        } );
    }

    optional<int32> AutomationScratch::findInstanceProcessID( string_view folderName )
    {
        const size_t markAt = folderName.rfind( kInstanceMark );
        int32        processID{ 0 };
        if ( markAt == string_view::npos || AutomationScratchInternal::parseProcessID( folderName.substr( markAt + kInstanceMark.size() ), processID ) == false )
            return nullopt;
        return processID;
    }
} // namespace sw
