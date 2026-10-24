#include "pch.h"

#include "Engine/Automation/AutomationScratch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/UUID/UUID.h"

#include "Engine/Resource/AssetMeta.h"
#include "Engine/Resource/ResourcePaths.h"

namespace sw
{
    namespace
    {
        struct AutomationScratchInternal
        {
            /**
             * @brief 사본 폴더를 끝까지 만든 횟수입니다(`AutomationScratch::getPrepareCount`).
             * @details 읽는 쪽(에디터 감시 울타리)이 서비스 범위를 거치지 않고 정적 창구로 읽으므로 범위에 맡기지 않는다. 원자 변수 하나라 상수 초기화되고,
             *          지연 생성하는 접근자를 두지 않는다. 프로세스가 도는 동안 오르기만 한다.
             */
            static inline atomic<uint64> s_prepareCount{ 0 };

            /** @brief 리소스 id 에 들어가는 글자인지입니다(id 경계 판정). */
            static bool isIDCharacter( utf8 character )
            {
                const bool bLetterOrDigit = ( 'a' <= character && character <= 'z' ) || ( 'A' <= character && character <= 'Z' ) || ( '0' <= character && character <= '9' );
                return bLetterOrDigit || character == '_' || character == '.' || character == '/' || character == '-';
            }

            /**
             * @brief `.meta`(이미 사본 id 로 바꾼 글)를 `AssetMeta` 로 읽어 guid 를 새로 만든 글을 돌려줍니다. 원본과 사본이 같은 guid 를 가지면 안 된다.
             * @details 글자 찾기로 고치지 않는다 — 형식이 바뀌면(키=값 → XML) 조용히 그대로 복사됐다. 읽지 못하면 오류다.
             */
            [[nodiscard]] static bool renewMetaGuid( string_view text, const string& sourceFile, string& outText, string& outError )
            {
                AssetMeta meta;
                if ( meta.parseXML( text, sourceFile.c_str() ) == false )
                {
                    outError = "could not read the asset meta " + sourceFile;
                    return false;
                }
                meta._guid = UUID::generate().toString();
                outText    = meta.toXML();
                return true;
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
            string            text  = rebaseText( sourceText, folder._sourceID, folder._scratchID );
            const bool        bMeta = StringUtil::endsWith( FileUtil::normalizePath( targetFile ), ".meta" );
            if ( bMeta && AutomationScratchInternal::renewMetaGuid( string( text ), sourceFile, text, outError ) == false )
                return false;
            if ( FileUtil::writeTextFile( targetFile, text ) == false )
            {
                outError = "could not write " + targetFile;
                return false;
            }
        }
        (void)AutomationScratchInternal::s_prepareCount.fetch_add( 1 );
        return true;
    }

    uint64 AutomationScratch::getPrepareCount()
    {
        return AutomationScratchInternal::s_prepareCount.load();
    }
} // namespace sw
