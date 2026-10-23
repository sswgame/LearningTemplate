/**
 * @file ShaderCookStamp.cpp
 * @brief 쿠킹된 산출물이 최신인지 판정합니다. **파일 시간이 아니라 내용 해시**로 판단합니다.
 * @details 이 저장소는 쿠킹된 바이너리까지 커밋하므로 `git pull` 이 소스와 산출물의 mtime 을 임의의 순서로 덮어쓴다.
 *          주의: `산출물 mtime >= 소스 mtime` 으로 판정하면 그때 "이미 최신" 이라 답해 낡은 바이너리가 커밋된 채 돌고,
 *          한 백엔드만 다른 이미지를 내면 백엔드 버그로 오인하게 된다. 그 판정이 이 파일 하나에 모여 있다(`ShaderCookStampTest.FreshnessIsJudgedByContentNotFileTime` 이 고정한다).
 */
#include "pch.h"

#include "Core/Common/HashUtil.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Resource/Cook/ContentHash.h"
#include "Engine/Resource/ResourcePaths.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct ShaderCookStampInternal
        {
            /** @brief 공유 헤더(.hlsli) 내용 해시 캐시입니다. 트리 전체를 매번 훑을 수는 없습니다. */
            inline static mutex _s_sharedHeaderMutex{};

            /// @brief 리소스 루트에서 그 아래 공유 헤더 해시를 찾는 캐시입니다. 루트마다 따로 둔다 — 테스트가 임시 루트를 넘겨도 실제 루트의 값을 덮지 않는다.
            inline static unordered_map<string, uint64> _s_mapSharedHeaderHash{};

            /**
             * @brief 쿠킹 스탬프 헤더입니다. **버전을 올리면 스탬프가 모두 불일치가 되어 한 번 전부 다시 쿠킹합니다.**
             * @details 판정은 소스 내용 해시와 도구 지문 줄(키 `kToolchainKey`, `ShaderCooker::getToolchainFingerprint`)이다.
             *          헤더가 다른 스탬프는 판정 기준이 달라 믿지 않는다.
             */
            static constexpr string_view kCookStampHeader{ "SWCOOK 4" };

            /** @brief 스탬프에서 도구 지문 줄의 키입니다. 소스 키는 '@' 로 시작하지 않습니다. */
            static constexpr string_view kToolchainKey{ "@toolchain" };

            /** @brief 스탬프의 16자리 hex 를 uint64 로 바꿉니다. 형식이 맞지 않으면 0이며 불일치로 처리합니다. */
            static uint64 parseHex64( string_view hex )
            {
                uint64 value{ 0 };
                for ( const utf8 c : hex )
                {
                    uint64 digit{ 0 };
                    if ( c >= '0' && c <= '9' )
                        digit = static_cast<uint64>( c - '0' );
                    else if ( c >= 'a' && c <= 'f' )
                        digit = static_cast<uint64>( c - 'a' ) + 10u;
                    else
                        return 0;
                    value = ( value << 4 ) | digit;
                }
                return value;
            }

            /** @brief 스탬프 한 줄의 원천입니다. 키와 그 소스의 절대 경로를 가집니다. */
            struct StampedSource
            {
                string _key;
                string _absPath;
            };

            /**
             * @brief 스탬프가 볼 소스를 모읍니다. 자기 도메인의 `.hlsl`(@p bHeadersOnly 가 아니면)과 `.hlsli`, 그리고 다른 include 루트 도메인
             *        (`ShaderCooker::collectIncludeRootDomains`)의 `.hlsli` 입니다.
             * @return 우선순위 설정이 틀렸으면(도메인 종류가 빠짐) false 입니다. 호출자는 스탬프를 쓰거나 최신으로 판정하지 않는다.
             * @details 셰이더는 자기 폴더 밖 include 루트(engine, common)의 헤더도 include 한다. 자기 도메인만 보면 engine 헤더를 고쳐도
             *          common 셰이더가 "최신" 으로 남는다. 다른 도메인 헤더의 키는 `<도메인>:<상대 경로>` 다(자기 키에는 ':' 가 없다).
             *          판정(`ShaderCooker::verifyCookedShaders`)도 이 함수를 쓴다 — 규칙이 한 곳이다.
             *          다른 도메인은 @p resourceRoot 아래에서 찾는다. 전역 루트(`ResourcePaths`)를 여기서 읽으면 임시 트리에서 도는 테스트도
             *          실제 `Resource/` 의 헤더를 보게 되어, 다른 프로세스가 그 트리를 건드리는 순간 판정이 흔들린다.
             */
            [[nodiscard]] static bool collectStampedSources( string_view resourceRoot, const vector<string>& listPriority, const string& shadersDir, bool bHeadersOnly, vector<StampedSource>& outListSource )
            {
                outListSource.clear();
                vector<string> listOwn;
                if ( bHeadersOnly == false )
                    FileUtil::collectFiles( shadersDir, ".hlsl", listOwn, true );
                FileUtil::collectFiles( shadersDir, ".hlsli", listOwn, true );
                for ( const string& sourcePath : listOwn )
                {
                    const string normSource = FileUtil::normalizeSeparators( sourcePath );
                    if ( normSource.find( "/bin/" ) != string::npos )
                        continue;
                    string key = makeStampKey( normSource, shadersDir );
                    if ( key.empty() == false )
                        outListSource.push_back( StampedSource{ std::move( key ), normSource } );
                }

                vector<string> listIncludeRoot;
                if ( ShaderCooker::collectIncludeRootDomains( listPriority, listIncludeRoot ) == false )
                    return false; // 설정이 틀렸다. 일부만 보고 스탬프를 쓰지 않는다(오류는 위 함수가 남겼다).
                if ( resourceRoot.empty() )
                    return true; // 루트가 없으면 다른 도메인도 없다(상대 경로로 붙이면 작업 폴더 아래를 본다).

                const string ownDirLower = StringUtil::toLower( shadersDir.c_str() );
                for ( const string& domain : listIncludeRoot )
                {
                    const utf8*  pDomain = domain.c_str();
                    const string rootDir = FileUtil::normalizeSeparators( FileUtil::joinPath( FileUtil::joinPath( resourceRoot, pDomain ), "shaders" ) );
                    if ( FileUtil::isDirectory( rootDir ) == false || StringUtil::toLower( rootDir.c_str() ) == ownDirLower )
                        continue;
                    vector<string> listHeader;
                    FileUtil::collectFiles( rootDir, ".hlsli", listHeader, true );
                    for ( const string& headerPath : listHeader )
                    {
                        const string normHeader = FileUtil::normalizeSeparators( headerPath );
                        if ( normHeader.find( "/bin/" ) != string::npos )
                            continue;
                        const string relKey = makeStampKey( normHeader, rootDir );
                        if ( relKey.empty() == false )
                            outListSource.push_back( StampedSource{ string( pDomain ) + ":" + relKey, normHeader } );
                    }
                }
                return true;
            }

            /** @brief `cook.stamp` 한 개를 읽은 결과입니다. */
            struct StampInfo
            {
                /// @brief 상대 경로(소문자)에서 그 소스의 내용 해시를 찾는 맵입니다. 스탬프가 없거나 버전이 다르면 빈 맵입니다.
                unordered_map<string, uint64> _mapHash;
                /// @brief 스탬프에 적힌 모든 `.hlsli` 해시가 지금 소스와 같은지 여부입니다.
                bool _bHeadersCurrent{ false };
            };

            /// @brief `<리소스 루트>|bin/<rhi>` 별 스탬프 읽기 캐시입니다. 쿠킹 루프가 요청마다 부릅니다.
            inline static unordered_map<string, StampInfo> _s_mapStamp{};

            /**
             * @brief `bin/<rhi>/cook.stamp` 를 읽어 소스 해시 테이블을 반환합니다(폴더당 한 번 읽고 캐시합니다).
             * @details 스탬프 버전이 다르면 **빈 테이블**을 반환한다. 그러면 모든 것이 낡은 것으로 판정되어
             *          한 번 전부 다시 쿠킹한다. 버전을 올리는 것이 곧 강제 재쿠킹이다.
             * @warning 참조를 반환하므로 **`_s_sharedHeaderMutex` 를 잡은 채로만** 부르세요. 캐시에 삽입이
             *          일어나면 앞서 반환한 참조가 무효가 됩니다.
             */
            static const StampInfo& readCookStamp( string_view resourceRoot, const vector<string>& listPriority, const string& binDirectory, const string& shadersDir )
            {
                // 헤더 판정이 루트 아래 다른 도메인을 보므로 같은 폴더라도 루트가 다르면 다른 결과다.
                string cacheKey{ resourceRoot };
                cacheKey += '|';
                cacheKey += binDirectory;
                const auto cached = _s_mapStamp.find( cacheKey );
                if ( cached != _s_mapStamp.end() )
                    return cached->second;

                StampInfo    info{};
                const string stampPath = FileUtil::joinPath( binDirectory, "cook.stamp" );
                string       text;
                if ( FileUtil::readTextFile( stampPath, text ) && StringUtil::startsWith( text, kCookStampHeader ) )
                {
                    size_t pos = text.find( '\n' );
                    while ( pos != string::npos )
                    {
                        const size_t lineBegin = pos + 1;
                        const size_t lineEnd   = text.find( '\n', lineBegin );
                        const string line      = text.substr( lineBegin, ( lineEnd == string::npos ) ? string::npos : lineEnd - lineBegin );
                        pos                    = lineEnd;

                        const size_t space = line.find( ' ' );
                        if ( space != 16 )
                            continue;

                        // 쿠커는 '\n' 으로 쓰지만 이 파일은 저장소가 추적한다. autocrlf 가 켜진 윈도우에서
                        // 체크아웃하면 CRLF 로 내려오고, 그 '\r' 이 경로 키 끝에 붙으면 모든 조회가 빗나가
                        // **갓 체크아웃한 트리가 통째로 "낡음"** 으로 판정된다(모두 다시 쿠킹한다).
                        string key = line.substr( space + 1 );
                        if ( key.empty() == false && key.back() == '\r' )
                            key.pop_back();
                        info._mapHash.emplace( std::move( key ), parseHex64( line.substr( 0, space ) ) );
                    }
                }

                // 도구 지문이 다르면(컴파일러 · 옵션이 바뀌었으면) 소스가 같아도 모두 낡았다. 빈 테이블과 같게 다룬다.
                const auto toolchainIter = info._mapHash.find( string( kToolchainKey ) );
                if ( toolchainIter == info._mapHash.end() || toolchainIter->second != computeToolchainFingerprint() )
                    info._mapHash.clear();

                // 헤더는 어느 셰이더가 무엇을 include 하는지 파싱하지 않고 include 루트의 것을 **모두** 본다. 넉넉하게 쿠킹하는 쪽이 안전하다.
                info._bHeadersCurrent = info._mapHash.empty() == false;
                vector<StampedSource> listHeader;
                if ( collectStampedSources( resourceRoot, listPriority, shadersDir, true, listHeader ) == false )
                    info._bHeadersCurrent = false; // 도메인 설정이 틀리면 최신이라 답할 수 없다
                for ( const StampedSource& header : listHeader )
                {
                    const auto iter = info._mapHash.find( header._key );
                    if ( iter == info._mapHash.end() || iter->second != ContentHash::computeFile( header._absPath ) )
                    {
                        info._bHeadersCurrent = false;
                        break;
                    }
                }

                return _s_mapStamp.emplace( std::move( cacheKey ), std::move( info ) ).first->second;
            }

            /** @brief 도구 지문을 구합니다(`ShaderCooker::getToolchainFingerprint` 의 본체). 상수만으로 정해져 OS · 파일과 무관하다. */
            static uint64 computeToolchainFingerprint()
            {
                // SPIR-V 타깃 환경(ShaderCompiler 의 -fspv-target-env)은 Vulkan 요구 판이다.
                constexpr uint32 kVulkanTargetVersion = constant::kVulkanRequiredMajor * 1000 + constant::kVulkanRequiredMinor;
                return ShaderCooker::makeToolchainFingerprint( ShaderCooker::kCompileOptionsRevision, kVulkanTargetVersion, ShaderCooker::kCompilerVersion );
            }

            /**
             * @brief @p shadersDir(`<도메인>/shaders`)의 `.hlsli` 가 include 루트 도메인의 `.hlsli` 와 같은 상대 경로이면 그 쌍을 @p outListProblem 에 적습니다.
             * @return 문제가 없으면 true 입니다. 우선순위 설정이 틀려도 false 입니다.
             */
            [[nodiscard]] static bool findShadowedInDir( string_view resourceRoot, const vector<string>& listPriority, const string& shadersDir, vector<string>& outListProblem )
            {
                vector<string> listIncludeRoot;
                if ( ShaderCooker::collectIncludeRootDomains( listPriority, listIncludeRoot ) == false )
                    return false;
                if ( resourceRoot.empty() )
                    return true;

                vector<string> listOwn;
                FileUtil::collectFiles( shadersDir, ".hlsli", listOwn, true );
                unordered_set<string> setOwnKey;
                for ( const string& ownPath : listOwn )
                {
                    const string normOwn = FileUtil::normalizeSeparators( ownPath );
                    if ( normOwn.find( "/bin/" ) == string::npos )
                        setOwnKey.insert( makeStampKey( normOwn, shadersDir ) );
                }

                const string ownDirLower = StringUtil::toLower( shadersDir.c_str() );
                bool         bClean      = true;
                for ( const string& domain : listIncludeRoot )
                {
                    const string rootDir = FileUtil::normalizeSeparators( FileUtil::joinPath( FileUtil::joinPath( resourceRoot, domain ), "shaders" ) );
                    if ( FileUtil::isDirectory( rootDir ) == false || StringUtil::toLower( rootDir.c_str() ) == ownDirLower )
                        continue;
                    vector<string> listRootHeader;
                    FileUtil::collectFiles( rootDir, ".hlsli", listRootHeader, true );
                    for ( const string& headerPath : listRootHeader )
                    {
                        const string normHeader = FileUtil::normalizeSeparators( headerPath );
                        const string key        = normHeader.find( "/bin/" ) == string::npos ? makeStampKey( normHeader, rootDir ) : string{};
                        if ( key.empty() == false && setOwnKey.find( key ) != setOwnKey.end() )
                        {
                            appendProblem( outListProblem, "%#: header '%#' has the same name as '%#/shaders/%#' and would hide it (rename it)", shadersDir, key, domain, key );
                            bClean = false;
                        }
                    }
                }
                return bClean;
            }

            /** @brief 소스 절대 경로를 스탬프 키(shaders/ 기준 소문자 상대 경로)로 바꿉니다. */
            static string makeStampKey( string_view absSourcePath, const string& shadersDir )
            {
                const string norm = FileUtil::normalizeSeparators( absSourcePath );
                if ( norm.size() <= shadersDir.size() + 1 )
                    return string{};
                return StringUtil::toLower( norm.substr( shadersDir.size() + 1 ).c_str() );
            }

            /**
             * @brief `bin/<rhi>/cook.stamp` 에 셰이더 소스의 **내용 해시**를 적습니다.
             * @details 판정(`verifyCookedShaders`, `App --verify-shaders`)이 "지금 팩에 넣으려는 바이너리가 지금 이 소스에서 나온
             *          것인가" 를 파일 시간이 아니라 내용으로 확인하기 위한 것이다. 파일 시간은
             *          `git clone` 이 모두 체크아웃 시각으로 덮어써서 비교 자체가 무의미해진다.
             *          형식은 한 줄에 `<FNV-1a 64 16자리 hex> <shaders/ 기준 상대 경로>` 다. 해시는 CR 을
             *          뺀 바이트로 계산한다(`ContentHash::computeFile`). 판정 쪽과 같은 함수다.
             * @param resourceRoot 다른 include 루트 도메인을 찾을 리소스 루트
             * @param binDirectory 매니페스트를 쓴 폴더(`<domain>/shaders/bin/<rhi>`)
             */
            static void writeCookStamp( string_view resourceRoot, const vector<string>& listPriority, string_view binDirectory, const unordered_set<string>* pFailedSource )
            {
                // <domain>/shaders/bin/<rhi> → <domain>/shaders
                const string rhiDir     = FileUtil::normalizeSeparators( binDirectory );
                const string parentDir  = FileUtil::getDirectoryPart( rhiDir );
                const string shadersDir = FileUtil::getDirectoryPart( parentDir );
                if ( shadersDir.empty() )
                    return;

                vector<StampedSource> listSource;
                if ( collectStampedSources( resourceRoot, listPriority, shadersDir, false, listSource ) == false )
                    return; // 스탬프를 쓰지 않는다 — 없는 스탬프는 "낡음" 이라 다음 쿠킹과 판정이 오류를 다시 낸다.
                vector<string> listShadowed;
                if ( findShadowedInDir( resourceRoot, listPriority, shadersDir, listShadowed ) == false )
                    return; // 가려진 헤더가 있으면 어느 쪽이 쓰였는지 모른다 — 스탬프를 쓰지 않는다(문제 글은 컴파일과 판정이 남긴다).

                vector<string> listLine;
                listLine.reserve( listSource.size() );
                for ( const StampedSource& source : listSource )
                {
                    if ( pFailedSource != nullptr && pFailedSource->find( source._absPath ) != pFailedSource->end() )
                        continue; // 이번에 쿠킹하지 못했으므로 최신이 아니다.

                    // 해싱과 키 만들기는 **판정 쪽과 같은 함수**를 쓴다(스탬프와 판정이 다른 규칙을 쓰면 낡은 산출물이 "최신" 이 된다).
                    const uint64 hash = ContentHash::computeFile( source._absPath );
                    if ( hash == 0 )
                        continue;

                    StringBuilder<constant::kMaxBuffer256> sb;
                    sb.appendFormat( "%#", Fmt( hash, Format( 16, Format::Padding::Zero ).hex() ) );
                    sb.append( ' ' ).append( source._key );
                    listLine.push_back( string( sb.c_str(), sb.size() ) );
                }

                {
                    StringBuilder<constant::kMaxBuffer256> sb;
                    sb.appendFormat( "%#", Fmt( computeToolchainFingerprint(), Format( 16, Format::Padding::Zero ).hex() ) );
                    sb.append( ' ' ).append( string( kToolchainKey ) );
                    listLine.push_back( string( sb.c_str(), sb.size() ) );
                }

                std::sort( listLine.begin(), listLine.end() );

                string text{ kCookStampHeader };
                text += '\n';
                for ( const string& line : listLine )
                {
                    text += line;
                    text += "\n";
                }

                const string stampPath = FileUtil::joinPath( rhiDir, "cook.stamp" );
                if ( FileUtil::writeTextFile( stampPath, text ) == false )
                    SW_LOG_WARNING( "Failed to write the cook stamp: %#", stampPath.c_str() );
            }

            /** @brief 문제 한 줄을 서식으로 만들어 더합니다. */
            template <typename... Args>
            static void appendProblem( vector<string>& outListProblem, const utf8* pFormat, const Args&... args )
            {
                StringBuilder<constant::kMaxBuffer512> sb;
                sb.appendFormat( pFormat, args... );
                outListProblem.emplace_back( sb.c_str(), sb.size() );
            }

            /** @brief 도메인 발견의 기준인 우선순위 설정이 쓸 수 있으면 true 입니다. 비었거나 도메인 종류(engine · common)가 빠졌으면 오류 글을 남기고 false 입니다. */
            [[nodiscard]] static bool validatePriority( const vector<string>& listPriority )
            {
                if ( listPriority.empty() )
                {
                    SW_LOG_ERROR( "Shader domains cannot be found: _listResourcePriority (EngineConfig.json) is empty" );
                    return false;
                }
                for ( const utf8* pKind : { path::kEnginePack, path::kCommonPack } )
                {
                    if ( VectorUtil::contains( listPriority, string( pKind ) ) == false )
                    {
                        SW_LOG_ERROR( "Shader domains cannot be found: _listResourcePriority (EngineConfig.json) has no '%#' (the shader include root kind)", pKind );
                        return false;
                    }
                }
                return true;
            }

            /** @brief 쿠킹한 셰이더 바이너리의 확장자이면 true 입니다. */
            static bool isShaderBinaryFile( string_view path )
            {
                return StringUtil::endsWith( path, ".dxil", true ) || StringUtil::endsWith( path, ".dxbc", true ) || StringUtil::endsWith( path, ".spv", true );
            }

            /** @brief 도메인 하나의 `bin/<폴더>` 를 판정합니다(`ShaderCooker::verifyCookedShaders` 의 본체). */
            static void verifyDomain( string_view resourceRoot, const vector<string>& listPriority, const string& domain, string_view shaderFolder, vector<string>& outListProblem )
            {
                const string shadersDir = FileUtil::normalizeSeparators( FileUtil::joinPath( FileUtil::joinPath( resourceRoot, domain ), "shaders" ) );
                if ( FileUtil::isDirectory( shadersDir ) == false )
                    return;
                vector<StampedSource> listSource;
                if ( collectStampedSources( resourceRoot, listPriority, shadersDir, false, listSource ) == false )
                {
                    appendProblem( outListProblem, "%#/shaders: the resource priority list is missing a shader include root domain (engine, common)", domain );
                    return;
                }
                (void)findShadowedInDir( resourceRoot, listPriority, shadersDir, outListProblem );
                const bool bHasOwnSource = std::any_of( listSource.begin(), listSource.end(), []( const StampedSource& source )
                { return source._key.find( ':' ) == string::npos; } );
                if ( bHasOwnSource == false )
                    return;

                const string label  = domain + "/shaders";
                const string binDir = FileUtil::joinPath( FileUtil::joinPath( shadersDir, "bin" ), shaderFolder );
                if ( FileUtil::isDirectory( binDir ) == false )
                {
                    appendProblem( outListProblem, "%#: no '%#' binary folder (never cooked)", label, shaderFolder );
                    return;
                }
                string text;
                if ( FileUtil::readTextFile( FileUtil::joinPath( binDir, "cook.stamp" ), text ) == false )
                {
                    appendProblem( outListProblem, "%#: '%#' has no cook.stamp (the cooker writes it)", label, shaderFolder );
                    return;
                }
                if ( StringUtil::startsWith( text, kCookStampHeader ) == false )
                {
                    appendProblem( outListProblem, "%#: '%#' cook.stamp has an unknown header", label, shaderFolder );
                    return;
                }

                unordered_map<string, uint64> mapStamped;
                StringUtil::forEachLine( text, [&mapStamped]( string_view line )
                {
                    line = StringUtil::trimEnd( line );
                    if ( line.size() > 17 && line[16] == ' ' )
                        mapStamped.emplace( string( line.substr( 17 ) ), parseHex64( line.substr( 0, 16 ) ) );
                } );
                const auto toolchainIter = mapStamped.find( string( kToolchainKey ) );
                if ( toolchainIter == mapStamped.end() || toolchainIter->second != computeToolchainFingerprint() )
                    appendProblem( outListProblem, "%#: '%#' was cooked with another toolchain (compiler or options changed)", label, shaderFolder );

                unordered_map<string, uint64> mapCurrent;
                for ( const StampedSource& source : listSource )
                {
                    const uint64 hash = ContentHash::computeFile( source._absPath );
                    mapCurrent.emplace( source._key, hash );
                    const auto stampedIter = mapStamped.find( source._key );
                    if ( stampedIter == mapStamped.end() )
                        appendProblem( outListProblem, "%#/%#: never cooked for '%#'", label, source._key, shaderFolder );
                    else if ( stampedIter->second != hash )
                        appendProblem( outListProblem, "%#/%#: source changed but '%#' was not cooked again", label, source._key, shaderFolder );
                }
                for ( const auto& [key, hash] : mapStamped )
                {
                    const bool bToolchainLine = key.empty() == false && key.front() == '@';
                    if ( bToolchainLine == false && mapCurrent.find( key ) == mapCurrent.end() )
                        appendProblem( outListProblem, "%#/%#: removed shader is still in the '%#' stamp", label, key, shaderFolder );
                }

                vector<string> listBinary;
                vector<string> listFile;
                (void)FileUtil::collectFiles( binDir, "", listFile, false ); // 방금 본 폴더다
                for ( const string& filePath : listFile )
                {
                    if ( isShaderBinaryFile( filePath ) )
                        listBinary.push_back( FileUtil::getFileNamePart( filePath ) );
                }
                if ( listBinary.empty() )
                    appendProblem( outListProblem, "%#: '%#' has no shader binaries", label, shaderFolder );

                // 바이너리는 있는데 매니페스트에 그 키가 없는 조합을 잡는다. 스탬프는 소스가 바뀌었는지만 보므로 쿠커의 레시피가 늘어난 경우(새 퍼뮤테이션 축)는
                // 여기가 아니면 못 잡는다 — 리플렉션 없이 바인딩한 DX12 가 DEVICE_HUNG 으로 죽었다. 매니페스트 키는 바이너리 파일 이름 그대로다.
                vector<uint8> manifestBytes;
                const string  manifestPath = FileUtil::joinPath( binDir, "reflection.manifest" );
                if ( FileUtil::isRegularFile( manifestPath ) == false || FileUtil::readFile( manifestPath, manifestBytes ) == false )
                {
                    appendProblem( outListProblem, "%#: '%#' has no reflection manifest", label, shaderFolder );
                    return;
                }
                const string_view manifest{ reinterpret_cast<const utf8*>( manifestBytes.data() ), manifestBytes.size() };
                vector<string>    listUncovered;
                for ( const string& binaryName : listBinary )
                {
                    if ( manifest.find( binaryName ) == string_view::npos )
                        listUncovered.push_back( binaryName );
                }
                if ( listUncovered.empty() == false )
                {
                    std::sort( listUncovered.begin(), listUncovered.end() );
                    appendProblem( outListProblem, "%#: '%#' manifest misses %# binaries (first: %#) - cook again or delete the stale files", label, shaderFolder,
                                   listUncovered.size(), listUncovered.front() );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "ShaderCooker" );

    bool ShaderCooker::collectShaderDomains( string_view resourceRoot, const vector<string>& listPriority, vector<string>& outListDomain )
    {
        outListDomain.clear();
        if ( ShaderCookStampInternal::validatePriority( listPriority ) == false )
            return false;
        for ( const string& token : listPriority )
        {
            if ( token != path::kGamePack )
            {
                if ( VectorUtil::contains( outListDomain, token ) == false )
                    outListDomain.push_back( token );
                continue;
            }
            // 게임 도메인은 이름을 코드가 알지 않고 game/ 아래 폴더를 훑어 찾는다. 폴더가 없으면 게임 도메인이 없을 뿐이다.
            vector<string> listGameFolder;
            (void)FileUtil::collectFolders( FileUtil::joinPath( resourceRoot, path::kGamePack ), listGameFolder, false );
            std::sort( listGameFolder.begin(), listGameFolder.end() );
            for ( const string& gameFolder : listGameFolder )
            {
                string domain = string( path::kGamePack ) + "/" + FileUtil::getFileNamePart( FileUtil::trimTrailingSlashes( gameFolder ) );
                if ( VectorUtil::contains( outListDomain, domain ) == false )
                    outListDomain.push_back( std::move( domain ) );
            }
        }
        return true;
    }

    bool ShaderCooker::collectIncludeRootDomains( const vector<string>& listPriority, vector<string>& outListDomain )
    {
        outListDomain.clear();
        if ( ShaderCookStampInternal::validatePriority( listPriority ) == false )
            return false;
        outListDomain.emplace_back( path::kEnginePack );
        outListDomain.emplace_back( path::kCommonPack );
        return true;
    }

    bool ShaderCooker::findShadowedHeaders( string_view resourceRoot, const vector<string>& listPriority, string_view absShaderPath, vector<string>& outListProblem )
    {
        vector<string> listDomain;
        if ( collectShaderDomains( resourceRoot, listPriority, listDomain ) == false )
            return false;
        const string shaderLower = StringUtil::toLower( FileUtil::normalizeSeparators( absShaderPath ).c_str() );
        for ( const string& domain : listDomain )
        {
            const string shadersDir = FileUtil::normalizeSeparators( FileUtil::joinPath( FileUtil::joinPath( resourceRoot, domain ), "shaders" ) );
            if ( FileUtil::startsWithPathComponent( shaderLower, StringUtil::toLower( shadersDir.c_str() ) ) )
                return ShaderCookStampInternal::findShadowedInDir( resourceRoot, listPriority, shadersDir, outListProblem );
        }
        return true; // 어느 도메인에도 속하지 않는 소스(임시 폴더)는 가릴 헤더가 없다
    }

    void ShaderCooker::writeCookStamp( string_view binDirectory, const unordered_set<string>* pFailedSource )
    {
        ShaderCookStampInternal::writeCookStamp( ResourcePaths::getRootFolderPath(), ResourcePaths::getSearchPriority(), binDirectory, pFailedSource );
    }

    void ShaderCooker::writeCookStamp( string_view resourceRoot, string_view binDirectory, const unordered_set<string>* pFailedSource )
    {
        ShaderCookStampInternal::writeCookStamp( resourceRoot, ResourcePaths::getSearchPriority(), binDirectory, pFailedSource );
    }

    void ShaderCooker::invalidateSharedHeaderCache()
    {
        std::scoped_lock<mutex> lock{ ShaderCookStampInternal::_s_sharedHeaderMutex };
        ShaderCookStampInternal::_s_mapSharedHeaderHash.clear();
        ShaderCookStampInternal::_s_mapStamp.clear();
        // 내용 해시는 파일 시각으로 스스로 낡음을 알지만, 수동 리로드는 "전부 다시 본다" 는 약속이라 같이 버린다.
        ContentHash::clearCache();
    }

    uint64 ShaderCooker::getSharedHeaderContentHash()
    {
        return getSharedHeaderContentHash( ResourcePaths::getRootFolderPath() );
    }

    uint64 ShaderCooker::getSharedHeaderContentHash( string_view resourceRoot )
    {
        std::scoped_lock<mutex> lock{ ShaderCookStampInternal::_s_sharedHeaderMutex };
        const string            rootDir{ resourceRoot };
        const auto              cached = ShaderCookStampInternal::_s_mapSharedHeaderHash.find( rootDir );
        if ( cached != ShaderCookStampInternal::_s_mapSharedHeaderHash.end() )
            return cached->second;

        // 경로까지 섞는다. 헤더를 **지우기만** 해도 값이 달라져야 한다(내용만 XOR 하면 같은 내용 둘이
        // 서로를 지운다). 정렬은 collectFiles 순서에 기대지 않고 곱셈 누적으로 순서와 무관하게 만든다.
        uint64 combined = 0;
        if ( rootDir.empty() == false )
        {
            vector<string> listHeader;
            FileUtil::collectFiles( rootDir, ".hlsli", listHeader, true );
            for ( const string& headerPath : listHeader )
            {
                const string norm = StringUtil::toLower( FileUtil::normalizeSeparators( headerPath ).c_str() );
                combined += HashUtil::computeStringHash64( norm, false, ContentHash::computeFile( headerPath ) );
            }
        }

        ShaderCookStampInternal::_s_mapSharedHeaderHash.emplace( rootDir, combined );
        return combined;
    }

    uint64 ShaderCooker::computeEffectiveSourceHash( string_view absShaderPath )
    {
        return computeEffectiveSourceHash( ResourcePaths::getRootFolderPath(), absShaderPath );
    }

    uint64 ShaderCooker::computeEffectiveSourceHash( string_view resourceRoot, string_view absShaderPath )
    {
        const uint64 sourceHash = ContentHash::computeFile( absShaderPath );
        if ( sourceHash == 0 )
            return 0;
        // 도구 지문도 섞는다. 디스크 캐시 키가 이 값을 쓰므로, 빠지면 DXC 를 바꿔도 옛 바이트를 돌려준다.
        const uint64 headerHash = HashUtil::computeStringHash64( to_string( getSharedHeaderContentHash( resourceRoot ) ), false, sourceHash );
        return HashUtil::computeStringHash64( to_string( getToolchainFingerprint() ), false, headerHash );
    }

    uint64 ShaderCooker::makeToolchainFingerprint( uint32 compileOptionsRevision, uint32 vulkanTargetVersion, string_view compilerVersion )
    {
        uint64 hash = HashUtil::computeStringHash64( to_string( compileOptionsRevision ), false );
        hash        = HashUtil::computeStringHash64( to_string( vulkanTargetVersion ), false, hash );
        return HashUtil::computeStringHash64( string( compilerVersion ), false, hash );
    }

    uint64 ShaderCooker::getToolchainFingerprint()
    {
        return ShaderCookStampInternal::computeToolchainFingerprint();
    }

    bool ShaderCooker::isCookedOutputCurrent( string_view binDirectory, string_view absShaderPath )
    {
        return isCookedOutputCurrent( ResourcePaths::getRootFolderPath(), binDirectory, absShaderPath );
    }

    bool ShaderCooker::isCookedOutputCurrent( string_view resourceRoot, string_view binDirectory, string_view absShaderPath )
    {
        // <domain>/shaders/bin/<rhi> → <domain>/shaders (writeCookStamp 와 같은 되짚기)
        const string rhiDir     = FileUtil::normalizeSeparators( binDirectory );
        const string shadersDir = FileUtil::getDirectoryPart( FileUtil::getDirectoryPart( rhiDir ) );
        if ( shadersDir.empty() )
            return false;

        const string normSource = FileUtil::normalizeSeparators( absShaderPath );
        const uint64 sourceHash = ContentHash::computeFile( normSource );
        if ( sourceHash == 0 )
            return false;

        std::scoped_lock<mutex> lock{ ShaderCookStampInternal::_s_sharedHeaderMutex };

        const ShaderCookStampInternal::StampInfo& stamp = ShaderCookStampInternal::readCookStamp( resourceRoot, ResourcePaths::getSearchPriority(), rhiDir, shadersDir );
        if ( stamp._bHeadersCurrent == false )
            return false;

        const string stampKey = ShaderCookStampInternal::makeStampKey( normSource, shadersDir );
        const auto   iter     = stamp._mapHash.find( stampKey );
        return iter != stamp._mapHash.end() && iter->second == sourceHash;
    }

    void ShaderCooker::verifyCookedShaders( string_view resourceRoot, string_view shaderFolder, vector<string>& outListProblem )
    {
        verifyCookedShaders( resourceRoot, ResourcePaths::getSearchPriority(), shaderFolder, outListProblem );
    }

    void ShaderCooker::verifyCookedShaders( string_view resourceRoot, const vector<string>& listPriority, string_view shaderFolder, vector<string>& outListProblem )
    {
        if ( resourceRoot.empty() || shaderFolder.empty() )
        {
            ShaderCookStampInternal::appendProblem( outListProblem, "shader verification needs a resource root and a shader folder (got '%#', '%#')", resourceRoot, shaderFolder );
            return;
        }
        vector<string> listDomain;
        if ( collectShaderDomains( resourceRoot, listPriority, listDomain ) == false )
        {
            ShaderCookStampInternal::appendProblem( outListProblem, "shader domains cannot be found: _listResourcePriority must list the engine and common domains" );
            return;
        }
        for ( const string& domain : listDomain )
        {
            ShaderCookStampInternal::verifyDomain( resourceRoot, listPriority, domain, shaderFolder, outListProblem );
        }
    }
} // namespace sw
