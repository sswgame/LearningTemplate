#include "pch.h"

#include "Engine/Automation/AutomationScratch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/UUID/UUID.h"

#include "Engine/Resource/ResourcePaths.h"

namespace sw
{
    namespace
    {
        struct AutomationScratchInternal
        {
            static constexpr string_view kGuidKey = "guid=";

            /** @brief 리소스 id 에 들어가는 글자인지입니다(id 경계 판정). */
            static bool isIDCharacter( utf8 character )
            {
                const bool bLetterOrDigit = ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ) || ( '0' <= character && character <= '9' );
                return bLetterOrDigit || character == '_' || character == '.' || character == '/' || character == '-';
            }

            /** @brief `.meta` 의 `guid=` 줄을 새 guid 로 바꿉니다. 줄이 없으면 그대로입니다. */
            static string renewMetaGuid( string_view text )
            {
                string       result{ text };
                const size_t keyPos = result.find( kGuidKey.data() ); // 리터럴에서 만든 view 라 널로 끝난다
                if ( keyPos == string::npos )
                    return result;
                const size_t valueStart = keyPos + kGuidKey.size();
                size_t       valueEnd   = result.find_first_of( "\r\n", valueStart );
                if ( valueEnd == string::npos )
                    valueEnd = result.size();
                result.replace( valueStart, valueEnd - valueStart, UUID::generate().toString() );
                return result;
            }
        };
    } // namespace

    bool AutomationScratch::isValidFolder( const AutomationScratchFolder& folder, string& outReason )
    {
        const string scratchPrefix = string( kScratchDomain ) + "/";
        if ( folder._sourceID.empty() || folder._scratchID.empty() )
        {
            outReason = "<Scratch> needs from=\"<resource id>\" and as=\"scratch/<name>\"";
            return false;
        }
        const bool bLeavesTree = ResourcePaths::hasParentDirectoryComponent( folder._sourceID ) || ResourcePaths::hasParentDirectoryComponent( folder._scratchID );
        if ( bLeavesTree )
        {
            outReason = "<Scratch> ids may not use '..'";
            return false;
        }
        if ( StringUtil::startsWith( folder._scratchID, scratchPrefix ) == false )
        {
            outReason = "<Scratch as=\"" + folder._scratchID + "\"> must sit under " + scratchPrefix;
            return false;
        }
        if ( StringUtil::startsWith( folder._sourceID, scratchPrefix ) )
        {
            outReason = "<Scratch from=\"" + folder._sourceID + "\"> must be a tracked folder, not another scratch copy";
            return false;
        }
        return true;
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
            const bool bAtBoundary = hit == 0 || AutomationScratchInternal::isIDCharacter( text[hit - 1] ) == false;
            result.append( text.substr( cursor, hit - cursor ) );
            result.append( bAtBoundary ? toID : fromID );
            cursor = hit + fromID.size();
        }
        result.append( text.substr( cursor ) ); // 찾은 끝(`hit + fromID.size()`)은 글 길이를 넘지 않는다
        return result;
    }

    bool AutomationScratch::prepare( const AutomationScratchFolder& folder, string& outError )
    {
        if ( isValidFolder( folder, outError ) == false )
            return false;
        const string& resourceRoot = ResourcePaths::getRootFolderPath();
        const string  sourceDir    = FileUtil::joinPath( resourceRoot, folder._sourceID );
        const string  scratchDir   = FileUtil::joinPath( resourceRoot, folder._scratchID );
        if ( FileUtil::isDirectory( sourceDir ) == false )
        {
            outError = "<Scratch from=\"" + folder._sourceID + "\"> is not a folder under Resource/";
            return false;
        }
        // 지난 실행(중단된 것 포함)이 남긴 사본을 통째로 지운다 — 잔여 파일이 다음 실행의 이름을 바꾸지 않게 한다.
        if ( FileUtil::removeDirectory( scratchDir ) == false || FileUtil::ensureDirectoryExists( scratchDir ) == false )
        {
            outError = "could not reset the scratch folder " + scratchDir;
            return false;
        }
        vector<string> listSourceFile;
        if ( FileUtil::collectFiles( sourceDir, "", listSourceFile, true ) == false )
        {
            outError = "could not list " + sourceDir;
            return false;
        }
        for ( const string& sourceFile : listSourceFile )
        {
            string relative;
            if ( FileUtil::makeRelativePath( sourceDir, sourceFile, relative ) == false )
            {
                outError = "could not place " + sourceFile + " under " + sourceDir;
                return false;
            }
            const string targetFile = FileUtil::joinPath( scratchDir, relative );
            if ( FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( targetFile ) ) == false )
            {
                outError = "could not create the folder of " + targetFile;
                return false;
            }
            vector<uint8> bytes;
            if ( FileUtil::readFile( sourceFile, bytes ) == false )
            {
                outError = "could not read " + sourceFile;
                return false;
            }
            // NUL 이 든 파일은 바이너리다 — 그대로 복사한다. 글 파일은 원본 id 를 사본 id 로 바꾸고, `.meta` 는 guid 를 새로 만든다.
            const bool bBinary = std::find( bytes.begin(), bytes.end(), uint8{ 0 } ) != bytes.end();
            if ( bBinary )
            {
                if ( FileUtil::writeFile( targetFile, bytes.data(), bytes.size() ) == false )
                {
                    outError = "could not write " + targetFile;
                    return false;
                }
                continue;
            }
            const string_view sourceText( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
            string            text = rebaseText( sourceText, folder._sourceID, folder._scratchID );
            if ( StringUtil::endsWith( FileUtil::normalizePath( targetFile ), ".meta" ) )
                text = AutomationScratchInternal::renewMetaGuid( text );
            if ( FileUtil::writeTextFile( targetFile, text ) == false )
            {
                outError = "could not write " + targetFile;
                return false;
            }
        }
        return true;
    }
} // namespace sw
