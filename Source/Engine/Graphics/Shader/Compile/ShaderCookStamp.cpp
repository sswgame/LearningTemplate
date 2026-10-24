/**
 * @file ShaderCookStamp.cpp
 * @brief 쿠킹된 산출물이 최신인지 판정합니다. **파일 시간이 아니라 내용 해시**로, **셰이더마다 실제로 연 헤더만** 보고 판단합니다.
 * @details 이 저장소는 쿠킹된 바이너리까지 커밋하므로 `git pull` 이 소스와 산출물의 mtime 을 임의의 순서로 덮어쓴다.
 *          주의: `산출물 mtime >= 소스 mtime` 으로 판정하면 그때 "이미 최신" 이라 답해 낡은 바이너리가 커밋된 채 돌고,
 *          한 백엔드만 다른 이미지를 내면 백엔드 버그로 오인하게 된다. 그 판정이 이 파일 하나에 모여 있다(`ShaderCookStampTest.FreshnessIsJudgedByContentNotFileTime` 이 고정한다).
 *
 *          **헤더는 소스마다 컴파일러가 실제로 연 것만 적는다.** 예전에는 모든 `.hlsli` 의 해시 하나를 섞어 헤더 하나를 고치면 모든 셰이더가 다시 쿠킹됐다.
 *          어느 헤더를 열었는지는 바이트코드(리플렉션은 바인딩 정보다)가 아니라 컴파일러가 소스를 읽는 순간 include 처리기가 안다 — 그 목록(`ShaderCompileResult::_listIncludedFile`)을
 *          쿠커가 받아 `writeCookStamp` 에 넘기고, 스탬프에 소스마다 "연 헤더와 각 헤더의 내용 해시" 가 적힌다. 같은 이유로 컴파일 캐시도 항목 옆에 의존 기록(`.deps`)을 둔다.
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
            /** @brief 스탬프 읽기 캐시를 지키는 잠금입니다. */
            inline static mutex _s_stampMutex{};

            /**
             * @brief 쿠킹 스탬프 헤더입니다. **버전을 올리면 스탬프가 모두 불일치가 되어 한 번 전부 다시 쿠킹합니다.**
             * @details 5판: 소스(`.hlsl`)마다 컴파일이 연 헤더와 그 내용 해시를 적는다(4판은 모든 `.hlsli` 를 줄마다 적었다). 옛 판은 읽지 않고 낡음으로 본다.
             *          판정은 소스 내용 해시, 소스별 헤더 해시, 도구 지문 줄(키 `kToolchainKey`, `ShaderCooker::getToolchainFingerprint`)이다.
             */
            static constexpr string_view kCookStampHeader{ "SWCOOK 5" };

            /** @brief 스탬프에서 도구 지문 줄의 키입니다. 소스 키는 '@' 로 시작하지 않습니다. */
            static constexpr string_view kToolchainKey{ "@toolchain" };

            /** @brief 의존 줄에서 소스 키와 헤더 키 사이의 구분자입니다(`<해시> <소스 키>?<헤더 키>`). 경로에 쓸 수 없는 글자라 키와 섞이지 않는다. */
            static constexpr utf8 kDependencySeparator{ '?' };

            /** @brief 컴파일 캐시 옆 의존 기록의 첫 줄입니다(`ShaderCooker::writeIncludeRecord`). */
            static constexpr string_view kIncludeRecordHeader{ "SWDEPS 1" };

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

            /** @brief uint64 를 16자리 소문자 hex 로 바꿉니다(`parseHex64` 의 짝). */
            static string formatHex64( uint64 value )
            {
                utf8 arrDigit[16]{};
                for ( size_t index = 16; index > 0; --index )
                {
                    arrDigit[index - 1] = "0123456789abcdef"[value & 0xFu];
                    value >>= 4;
                }
                return string( arrDigit, 16 );
            }

            /** @brief 스탬프 한 줄의 원천입니다. 키와 그 소스의 절대 경로를 가집니다. */
            struct StampedSource
            {
                string _key;
                string _absPath;
            };

            /**
             * @brief 스탬프가 볼 소스를 모읍니다 — 자기 도메인(@p shadersDir)의 `.hlsl` 입니다(`/bin/` 아래는 뺀다).
             * @details 헤더(`.hlsli`)는 줄로 적지 않는다. 소스마다 컴파일이 연 헤더가 그 소스의 의존 줄로 적힌다. 판정(`ShaderCooker::verifyCookedShaders`)도 이 함수를 쓴다 — 규칙이 한 곳이다.
             */
            static void collectStampedSources( const string& shadersDir, vector<StampedSource>& outListSource )
            {
                outListSource.clear();
                vector<string> listOwn;
                FileUtil::collectFiles( shadersDir, ".hlsl", listOwn, true );
                for ( const string& sourcePath : listOwn )
                {
                    const string normSource = FileUtil::normalizeSeparators( sourcePath );
                    if ( normSource.find( "/bin/" ) != string::npos )
                        continue;
                    string key = makeStampKey( normSource, shadersDir );
                    if ( key.empty() == false )
                        outListSource.push_back( StampedSource{ std::move( key ), normSource } );
                }
            }

            /** @brief 소스 하나의 의존 줄입니다 — 연 헤더의 키(대소문자 그대로)와 그 내용 해시. */
            struct StampDependency
            {
                string _headerKey;
                uint64 _hash{ 0 };
            };

            /** @brief `cook.stamp` 한 개를 읽은 결과입니다. */
            struct StampInfo
            {
                /// @brief 첫 줄이 지금 판(`kCookStampHeader`)인가. 아니면 아래는 모두 비어 있다.
                bool _bVersionKnown{ false };
                /// @brief 도구 지문 줄이 있었는가.
                bool _bHasToolchain{ false };
                /// @brief 도구 지문 줄의 값입니다.
                uint64 _toolchain{ 0 };
                /// @brief 소스 키(소문자 상대 경로)에서 그 소스의 내용 해시를 찾는 맵입니다.
                unordered_map<string, uint64> _mapSourceHash;
                /// @brief 소스 키에서 그 소스가 연 헤더와 내용 해시를 찾는 맵입니다. 헤더를 열지 않은 소스는 항목이 없다.
                unordered_map<string, vector<StampDependency>> _mapDependency;
            };

            /** @brief 스탬프 글을 해석합니다. 첫 줄이 지금 판이 아니면 `_bVersionKnown` 이 false 인 빈 결과입니다. 판정은 호출자가 한다(도구 지문 비교 등). */
            static StampInfo parseCookStamp( string_view text )
            {
                StampInfo info{};
                if ( StringUtil::startsWith( text, kCookStampHeader ) == false )
                    return info;
                info._bVersionKnown = true;

                size_t pos = text.find( '\n' );
                while ( pos != string_view::npos )
                {
                    const size_t lineBegin = pos + 1;
                    const size_t lineEnd   = text.find( '\n', lineBegin );
                    string_view  line      = text.substr( lineBegin, ( lineEnd == string_view::npos ) ? string_view::npos : lineEnd - lineBegin );
                    pos                    = lineEnd;

                    // 쿠커는 '\n' 으로 쓰지만 이 파일은 저장소가 추적한다. autocrlf 가 켜진 윈도우에서
                    // 체크아웃하면 CRLF 로 내려오고, 그 '\r' 이 경로 키 끝에 붙으면 모든 조회가 빗나가
                    // **갓 체크아웃한 트리가 통째로 "낡음"** 으로 판정된다(모두 다시 쿠킹한다).
                    line = StringUtil::trimEnd( line );
                    if ( line.size() <= 17 || line[16] != ' ' )
                        continue;

                    const uint64      hash = parseHex64( line.substr( 0, 16 ) );
                    const string_view rest = line.substr( 17 );
                    if ( rest == kToolchainKey )
                    {
                        info._toolchain     = hash;
                        info._bHasToolchain = true;
                        continue;
                    }
                    const size_t separator = rest.find( kDependencySeparator );
                    if ( separator == string_view::npos )
                    {
                        info._mapSourceHash.emplace( string( rest ), hash );
                        continue;
                    }
                    info._mapDependency[string( rest.substr( 0, separator ) )].push_back( StampDependency{ string( rest.substr( separator + 1 ) ), hash } );
                }
                return info;
            }

            /// @brief `bin/<rhi>` 폴더별 스탬프 읽기 캐시입니다. 쿠킹 루프가 요청마다 부릅니다. 스탬프 글만 담는다 — 헤더 파일의 지금 상태는 담지 않고 부를 때마다 본다.
            inline static unordered_map<string, StampInfo> _s_mapStamp{};

            /**
             * @brief `bin/<rhi>/cook.stamp` 를 읽어 해석합니다(폴더당 한 번 읽고 캐시합니다).
             * @details 스탬프 판이나 도구 지문이 다르면 **빈 결과**를 반환한다. 그러면 모든 것이 낡은 것으로 판정되어
             *          한 번 전부 다시 쿠킹한다. 버전을 올리는 것이 곧 강제 재쿠킹이다.
             * @warning 참조를 반환하므로 **`_s_stampMutex` 를 잡은 채로만** 부르세요. 캐시에 삽입이
             *          일어나면 앞서 반환한 참조가 무효가 됩니다.
             */
            static const StampInfo& readCookStamp( const string& binDirectory )
            {
                const auto cached = _s_mapStamp.find( binDirectory );
                if ( cached != _s_mapStamp.end() )
                    return cached->second;

                StampInfo    info{};
                const string stampPath = FileUtil::joinPath( binDirectory, "cook.stamp" );
                string       text;
                if ( FileUtil::readTextFile( stampPath, text ) )
                    info = parseCookStamp( text );

                // 도구 지문이 다르면(컴파일러 · 옵션이 바뀌었으면) 소스가 같아도 모두 낡았다. 빈 결과와 같게 다룬다.
                const bool bUsable = info._bVersionKnown && info._bHasToolchain && info._toolchain == computeToolchainFingerprint();
                if ( bUsable == false )
                    info = StampInfo{};

                return _s_mapStamp.emplace( binDirectory, std::move( info ) ).first->second;
            }

            /** @brief 도구 지문을 구합니다(`ShaderCooker::getToolchainFingerprint` 의 본체). 상수만으로 정해져 OS · 파일과 무관하다. */
            static uint64 computeToolchainFingerprint()
            {
                // SPIR-V 타깃 환경(ShaderCompiler 의 -fspv-target-env)은 Vulkan 요구 판이다.
                constexpr uint32 kVulkanTargetVersion = constant::kVulkanRequiredMajor * 1000 + constant::kVulkanRequiredMinor;
                return ShaderCooker::makeToolchainFingerprint( ShaderCooker::kCompileOptionsRevision, kVulkanTargetVersion, ShaderCooker::kCompilerVersion );
            }

            /** @brief 헤더 키를 만들 때 헤더가 어느 루트 아래인지 가르는 항목입니다(자기 도메인, 다른 include 루트 도메인). */
            struct HeaderRoot
            {
                /// @brief 실제 경로(`makeCanonicalPath`)를 소문자로 바꾼 루트 폴더입니다(`<도메인>/shaders`). 비교용이다.
                string _lowerDir;
                /// @brief 키 앞에 붙는 도메인 이름입니다. 자기 도메인이면 비어 있다(자기 키에는 ':' 가 없다).
                string _domain;
            };

            /**
             * @brief 헤더 키를 만들 루트 목록을 모읍니다 — 자기 도메인(@p shadersDir)이 먼저, 다음에 @p resourceRoot 아래 다른 include 루트 도메인(@p listIncludeRoot)입니다.
             * @return 자기 도메인의 폴더를 실제 경로로 풀지 못하면 false 입니다.
             * @details 다른 도메인은 @p resourceRoot 아래에서 찾는다. 전역 루트(`ResourcePaths`)를 여기서 읽으면 임시 트리에서 도는 테스트도
             *          실제 `Resource/` 를 보게 된다. 루트가 같은 폴더를 가리키는 도메인(자기 도메인이 engine 인 경우의 engine)은 빼고 자기 키로 적는다.
             */
            [[nodiscard]] static bool collectHeaderRoots( string_view resourceRoot, const vector<string>& listIncludeRoot, const string& shadersDir, vector<HeaderRoot>& outListRoot )
            {
                outListRoot.clear();
                string ownCanonical;
                if ( FileUtil::makeCanonicalPath( shadersDir, ownCanonical ) == false )
                    return false;
                const string ownLower = StringUtil::toLower( FileUtil::normalizeSeparators( ownCanonical ).c_str() );
                outListRoot.push_back( HeaderRoot{ ownLower, string{} } );
                if ( resourceRoot.empty() )
                    return true; // 루트가 없으면 다른 도메인도 없다(상대 경로로 붙이면 작업 폴더 아래를 본다).

                for ( const string& domain : listIncludeRoot )
                {
                    const string rootDir = FileUtil::normalizeSeparators( FileUtil::joinPath( FileUtil::joinPath( resourceRoot, domain ), "shaders" ) );
                    string       canonical;
                    if ( FileUtil::isDirectory( rootDir ) == false || FileUtil::makeCanonicalPath( rootDir, canonical ) == false )
                        continue;
                    const string lowerDir = StringUtil::toLower( FileUtil::normalizeSeparators( canonical ).c_str() );
                    if ( lowerDir != ownLower )
                        outListRoot.push_back( HeaderRoot{ lowerDir, domain } );
                }
                return true;
            }

            /**
             * @brief 컴파일이 연 헤더의 절대 경로를 스탬프의 헤더 키로 바꿉니다 — 자기 도메인이면 `shaders/` 기준 상대 경로, 다른 include 루트면 `<도메인>:<상대 경로>` 입니다.
             * @return 어느 루트 아래도 아니거나 경로를 풀지 못하면 빈 글입니다.
             * @details 상대 경로의 대소문자는 그대로 둔다 — 리눅스는 이 키로 파일을 다시 열어야 한다(소스 키는 비교용이라 소문자다).
             *          shortcut: 헤더는 컴파일러가 먼저 찾은 하나만 적힌다. 소스가 그대로인데 앞 순위 루트에 같은 이름의 헤더가 새로 생겨 가려지는 경우는 이 스탬프로 못 잡는다
             *          (같은 이름은 `findShadowedInDir` 가 컴파일과 `verifyCookedShaders` 에서 오류로 막는다).
             */
            static string makeHeaderKey( const vector<HeaderRoot>& listRoot, string_view absHeader )
            {
                string resolved;
                if ( FileUtil::makeCanonicalPath( absHeader, resolved ) == false )
                    return string{};
                const string canonical = FileUtil::normalizeSeparators( resolved ); // 구분자를 맞춘다 — 키는 OS 가 달라도 같은 글이어야 한다
                const string lower     = StringUtil::toLower( canonical.c_str() );
                for ( const HeaderRoot& root : listRoot )
                {
                    const bool bInside = FileUtil::startsWithPathComponent( lower, root._lowerDir ) && root._lowerDir.size() < lower.size();
                    if ( bInside == false )
                        continue;
                    const string relative = canonical.substr( root._lowerDir.size() + 1 );
                    if ( root._domain.empty() )
                        return relative;
                    string keyed = root._domain;
                    keyed += ":";
                    keyed += relative;
                    return keyed;
                }
                return string{};
            }

            /**
             * @brief 스탬프의 헤더 키를 지금 파일 경로로 바꿉니다(`makeHeaderKey` 의 역).
             * @return 다른 도메인이 include 루트가 아니거나 루트가 없으면 빈 글입니다 — 호출자는 낡음으로 본다.
             */
            static string resolveHeaderKey( string_view resourceRoot, const vector<string>& listIncludeRoot, const string& shadersDir, const string& headerKey )
            {
                const size_t colon = headerKey.find( ':' );
                if ( colon == string::npos )
                    return FileUtil::normalizeSeparators( FileUtil::joinPath( shadersDir, headerKey ) );
                const string domain = headerKey.substr( 0, colon );
                if ( resourceRoot.empty() || VectorUtil::contains( listIncludeRoot, domain ) == false )
                    return string{};
                return FileUtil::normalizeSeparators( FileUtil::joinPath( FileUtil::joinPath( FileUtil::joinPath( resourceRoot, domain ), "shaders" ), headerKey.substr( colon + 1 ) ) );
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
             * @brief `bin/<rhi>/cook.stamp` 에 셰이더 소스의 **내용 해시**와 소스마다 컴파일이 연 **헤더의 내용 해시**를 적습니다.
             * @details 판정(`verifyCookedShaders`, `App --verify-shaders`)이 "지금 팩에 넣으려는 바이너리가 지금 이 소스(와 이 소스가 연 헤더)에서 나온
             *          것인가" 를 파일 시간이 아니라 내용으로 확인하기 위한 것이다. 파일 시간은
             *          `git clone` 이 모두 체크아웃 시각으로 덮어써서 비교 자체가 무의미해진다.
             *          형식은 한 줄에 `<FNV-1a 64 16자리 hex> <shaders/ 기준 상대 경로>` 이고, 소스가 연 헤더마다 `<해시> <소스 경로>?<헤더 키>` 줄이 더 있다(`makeHeaderKey`).
             *          해시는 CR 을 뺀 바이트로 계산한다(`ContentHash::computeFile`). 판정 쪽과 같은 함수다.
             * @param resourceRoot 다른 include 루트 도메인을 찾을 리소스 루트
             * @param binDirectory 매니페스트를 쓴 폴더(`<domain>/shaders/bin/<rhi>`)
             * @param mapIncludedFile 소스(정규화한 절대 경로)별 연 헤더(절대 경로). 여기 없는 소스는 헤더에 기대지 않는 것으로 적는다.
             */
            [[nodiscard]] static bool writeCookStamp( string_view resourceRoot, const vector<string>& listPriority, string_view binDirectory, const unordered_set<string>* pFailedSource,
                                                      const unordered_map<string, vector<string>>& mapIncludedFile )
            {
                // <domain>/shaders/bin/<rhi> → <domain>/shaders
                const string rhiDir     = FileUtil::normalizeSeparators( binDirectory );
                const string parentDir  = FileUtil::getDirectoryPart( rhiDir );
                const string shadersDir = FileUtil::getDirectoryPart( parentDir );
                if ( shadersDir.empty() )
                    return false;

                vector<string> listIncludeRoot;
                if ( ShaderCooker::collectIncludeRootDomains( listPriority, listIncludeRoot ) == false )
                    return false; // 스탬프를 쓰지 않는다 — 없는 스탬프는 "낡음" 이라 다음 쿠킹과 판정이 오류를 다시 낸다. 호출자가 실패로 센다.
                vector<string> listShadowed;
                if ( findShadowedInDir( resourceRoot, listPriority, shadersDir, listShadowed ) == false )
                    return false; // 가려진 헤더가 있으면 어느 쪽이 쓰였는지 모른다 — 스탬프를 쓰지 않는다(문제 글은 컴파일과 판정이 남긴다).
                vector<HeaderRoot> listHeaderRoot;
                if ( collectHeaderRoots( resourceRoot, listIncludeRoot, shadersDir, listHeaderRoot ) == false )
                {
                    SW_LOG_ERROR( "Failed to write the cook stamp: the shaders folder cannot be resolved: %#", shadersDir.c_str() );
                    return false;
                }

                vector<StampedSource> listSource;
                collectStampedSources( shadersDir, listSource );

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
                    if ( source._key.find( kDependencySeparator ) != string::npos )
                    {
                        SW_LOG_ERROR( "Failed to write the cook stamp: the shader path contains the dependency separator character: %#", source._absPath.c_str() );
                        return false;
                    }
                    listLine.push_back( formatHex64( hash ) + " " + source._key );

                    const auto includedIter = mapIncludedFile.find( source._absPath );
                    if ( includedIter == mapIncludedFile.end() )
                        continue;
                    for ( const string& header : includedIter->second )
                    {
                        // 컴파일이 방금 읽은 파일이다. 키로 못 바꾸거나 읽지 못하면 그 헤더의 변경을 영영 못 잡으므로 조용히 빼지 않고 멈춘다.
                        const string headerKey  = makeHeaderKey( listHeaderRoot, header );
                        const uint64 headerHash = ContentHash::computeFile( header );
                        if ( headerKey.empty() || headerHash == 0 )
                        {
                            SW_LOG_ERROR( "Failed to write the cook stamp: the header '%#' opened by '%#' is not under a shader include root or cannot be read", header.c_str(), source._absPath.c_str() );
                            return false;
                        }
                        string dependencyLine = formatHex64( headerHash ) + " " + source._key;
                        dependencyLine += kDependencySeparator;
                        dependencyLine += headerKey;
                        listLine.push_back( std::move( dependencyLine ) );
                    }
                }

                listLine.push_back( formatHex64( computeToolchainFingerprint() ) + " " + string( kToolchainKey ) );

                std::sort( listLine.begin(), listLine.end() );
                listLine.erase( std::unique( listLine.begin(), listLine.end() ), listLine.end() ); // 같은 헤더를 다른 이름으로 열었어도 한 줄이다

                string text{ kCookStampHeader };
                text += '\n';
                for ( const string& line : listLine )
                {
                    text += line;
                    text += "\n";
                }

                const string stampPath = FileUtil::joinPath( rhiDir, "cook.stamp" );
                if ( FileUtil::writeTextFile( stampPath, text ) == false )
                {
                    SW_LOG_ERROR( "Failed to write the cook stamp: %#", stampPath.c_str() );
                    return false;
                }
                {
                    // 방금 쓴 스탬프를 다음 조회가 읽게 한다(읽기 캐시에 옛 글이 남아 있으면 갓 쿠킹한 산출물이 낡음으로 보인다).
                    std::scoped_lock<mutex> lock{ _s_stampMutex };
                    _s_mapStamp.clear();
                }
                return true;
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
                vector<string> listIncludeRoot;
                if ( ShaderCooker::collectIncludeRootDomains( listPriority, listIncludeRoot ) == false )
                {
                    appendProblem( outListProblem, "%#/shaders: the resource priority list is missing a shader include root domain (engine, common)", domain );
                    return;
                }
                (void)findShadowedInDir( resourceRoot, listPriority, shadersDir, outListProblem );
                vector<StampedSource> listSource;
                collectStampedSources( shadersDir, listSource );
                if ( listSource.empty() )
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
                const StampInfo stamped = parseCookStamp( text );
                if ( stamped._bVersionKnown == false )
                {
                    appendProblem( outListProblem, "%#: '%#' cook.stamp has an unknown header (an older format is stale - cook again)", label, shaderFolder );
                    return;
                }
                if ( stamped._bHasToolchain == false || stamped._toolchain != computeToolchainFingerprint() )
                    appendProblem( outListProblem, "%#: '%#' was cooked with another toolchain (compiler or options changed)", label, shaderFolder );

                unordered_set<string> uniqueCurrentKey;
                for ( const StampedSource& source : listSource )
                {
                    uniqueCurrentKey.insert( source._key );
                    const uint64 hash        = ContentHash::computeFile( source._absPath );
                    const auto   stampedIter = stamped._mapSourceHash.find( source._key );
                    if ( stampedIter == stamped._mapSourceHash.end() )
                    {
                        appendProblem( outListProblem, "%#/%#: never cooked for '%#'", label, source._key, shaderFolder );
                        continue;
                    }
                    if ( stampedIter->second != hash )
                    {
                        appendProblem( outListProblem, "%#/%#: source changed but '%#' was not cooked again", label, source._key, shaderFolder );
                        continue;
                    }
                    // 소스가 열었던 헤더만 본다. 이 소스와 상관없는 헤더가 바뀐 것은 문제가 아니다.
                    const auto dependencyIter = stamped._mapDependency.find( source._key );
                    if ( dependencyIter == stamped._mapDependency.end() )
                        continue;
                    for ( const StampDependency& dependency : dependencyIter->second )
                    {
                        const string headerPath = resolveHeaderKey( resourceRoot, listIncludeRoot, shadersDir, dependency._headerKey );
                        if ( headerPath.empty() || ContentHash::computeFile( headerPath ) != dependency._hash )
                            appendProblem( outListProblem, "%#/%#: header '%#' changed or is gone but '%#' was not cooked again", label, source._key, dependency._headerKey, shaderFolder );
                    }
                }
                for ( const auto& [key, hash] : stamped._mapSourceHash )
                {
                    if ( uniqueCurrentKey.find( key ) == uniqueCurrentKey.end() )
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

    bool ShaderCooker::writeCookStamp( string_view resourceRoot, const vector<string>& listPriority, string_view binDirectory, const unordered_set<string>* pFailedSource,
                                       const unordered_map<string, vector<string>>& mapIncludedFile )
    {
        return ShaderCookStampInternal::writeCookStamp( resourceRoot, listPriority, binDirectory, pFailedSource, mapIncludedFile );
    }

    bool ShaderCooker::removeUnlistedBinaries( string_view binDirectory, const unordered_set<string>& uniqueKeepFileName, uint32& outRemovedCount )
    {
        outRemovedCount = 0;
        vector<string> listFile;
        (void)FileUtil::collectFiles( binDirectory, "", listFile, false ); // 폴더가 없으면 지울 것도 없다
        bool bAllRemoved = true;
        for ( const string& filePath : listFile )
        {
            if ( ShaderCookStampInternal::isShaderBinaryFile( filePath ) == false || uniqueKeepFileName.find( FileUtil::getFileNamePart( filePath ) ) != uniqueKeepFileName.end() )
                continue;
            if ( FileUtil::removeFile( filePath ) )
            {
                ++outRemovedCount;
                SW_LOG_INFO( "Removed a shader binary no cook request makes: %#", filePath );
            }
            else
            {
                SW_LOG_ERROR( "Could not remove the stale shader binary: %#", filePath );
                bAllRemoved = false;
            }
        }
        return bAllRemoved;
    }

    void ShaderCooker::invalidateSharedHeaderCache()
    {
        std::scoped_lock<mutex> lock{ ShaderCookStampInternal::_s_stampMutex };
        ShaderCookStampInternal::_s_mapStamp.clear();
        // 내용 해시는 파일 시각으로 스스로 낡음을 알지만, 수동 리로드는 "전부 다시 본다" 는 약속이라 같이 버린다.
        ContentHash::clearCache();
    }

    uint64 ShaderCooker::computeSourceFingerprint( string_view absShaderPath, const vector<string>& listIncludedFile )
    {
        const uint64 sourceHash = ContentHash::computeFile( absShaderPath );
        if ( sourceHash == 0 )
            return 0;
        // 헤더마다 경로까지 섞는다. 같은 내용의 두 헤더가 서로를 지우지 않게, 헤더 하나를 **다른 파일로 바꾸기만** 해도 값이 달라지게 한다.
        // 더하기로 합쳐 목록 순서와 무관하다.
        uint64 combined = 0;
        for ( const string& header : listIncludedFile )
        {
            const uint64 headerHash = ContentHash::computeFile( header );
            if ( headerHash == 0 )
                return 0; // 읽지 못하는 헤더가 있으면 같은 지문이 나올 수 없다 — 부르는 쪽이 낡음으로 본다
            const string norm = StringUtil::toLower( FileUtil::normalizeSeparators( header ).c_str() );
            combined += HashUtil::computeStringHash64( norm, false, headerHash );
        }
        // 도구 지문도 섞는다. 디스크 캐시 키가 이 값을 쓰므로, 빠지면 DXC 를 바꿔도 옛 바이트를 돌려준다.
        const uint64 headerPart = HashUtil::computeStringHash64( to_string( combined ), false, sourceHash );
        return HashUtil::computeStringHash64( to_string( getToolchainFingerprint() ), false, headerPart );
    }

    bool ShaderCooker::writeIncludeRecord( string_view recordPath, const vector<string>& listIncludedFile )
    {
        string text{ ShaderCookStampInternal::kIncludeRecordHeader };
        text += '\n';
        for ( const string& header : listIncludedFile )
        {
            const uint64 headerHash = ContentHash::computeFile( header );
            if ( headerHash == 0 )
            {
                SW_LOG_WARNING( "Could not hash the header '%#' for the include record '%#'", header.c_str(), string( recordPath ).c_str() );
                return false;
            }
            text += ShaderCookStampInternal::formatHex64( headerHash );
            text += ' ';
            text += header;
            text += '\n';
        }
        return FileUtil::writeTextFile( recordPath, text );
    }

    bool ShaderCooker::readIncludeRecord( string_view recordPath, vector<string>& outListIncludedFile )
    {
        outListIncludedFile.clear();
        string text;
        if ( FileUtil::readTextFile( recordPath, text ) == false || StringUtil::startsWith( text, ShaderCookStampInternal::kIncludeRecordHeader ) == false )
            return false;

        vector<string> listHeader;
        size_t         pos = text.find( '\n' );
        while ( pos != string::npos )
        {
            const size_t lineBegin = pos + 1;
            const size_t lineEnd   = text.find( '\n', lineBegin );
            string_view  line      = string_view( text ).substr( lineBegin, ( lineEnd == string::npos ) ? string_view::npos : lineEnd - lineBegin );
            pos                    = lineEnd;
            line                   = StringUtil::trimEnd( line );
            if ( line.empty() )
                continue;
            if ( line.size() <= 17 || line[16] != ' ' )
                return false; // 형식이 틀렸다 — 믿지 않는다
            const string headerPath{ line.substr( 17 ) };
            const uint64 recorded = ShaderCookStampInternal::parseHex64( line.substr( 0, 16 ) );
            if ( recorded == 0 || ContentHash::computeFile( headerPath ) != recorded )
                return false; // 바뀌었거나 없어졌거나 읽지 못한다
            listHeader.push_back( headerPath );
        }
        outListIncludedFile = std::move( listHeader );
        return true;
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

    bool ShaderCooker::isCookedOutputCurrent( string_view binDirectory, string_view absShaderPath, vector<string>* pOutListIncludedFile )
    {
        return isCookedOutputCurrent( ResourcePaths::getRootFolderPath(), ResourcePaths::getSearchPriority(), binDirectory, absShaderPath, pOutListIncludedFile );
    }

    bool ShaderCooker::isCookedOutputCurrent( string_view resourceRoot, const vector<string>& listPriority, string_view binDirectory, string_view absShaderPath, vector<string>* pOutListIncludedFile )
    {
        using Internal = ShaderCookStampInternal;
        if ( pOutListIncludedFile != nullptr )
            pOutListIncludedFile->clear();

        // <domain>/shaders/bin/<rhi> → <domain>/shaders (writeCookStamp 와 같은 되짚기)
        const string rhiDir     = FileUtil::normalizeSeparators( binDirectory );
        const string shadersDir = FileUtil::getDirectoryPart( FileUtil::getDirectoryPart( rhiDir ) );
        if ( shadersDir.empty() )
            return false;

        const string normSource = FileUtil::normalizeSeparators( absShaderPath );
        const uint64 sourceHash = ContentHash::computeFile( normSource );
        if ( sourceHash == 0 )
            return false;

        vector<string> listIncludeRoot;
        if ( collectIncludeRootDomains( listPriority, listIncludeRoot ) == false )
            return false; // 설정이 틀리면 최신이라 답할 수 없다(오류는 위 함수가 남겼다)

        // 스탬프에서 이 소스의 줄만 복사해 잠금 밖으로 나간다. 헤더 파일을 읽는 일(내용 해시)은 잠금 밖에서 한다.
        const string                      stampKey = Internal::makeStampKey( normSource, shadersDir );
        uint64                            stampedSourceHash{ 0 };
        vector<Internal::StampDependency> listDependency;
        {
            std::scoped_lock<mutex> lock{ Internal::_s_stampMutex };

            const Internal::StampInfo& stamp = Internal::readCookStamp( rhiDir );
            const auto                 iter  = stamp._mapSourceHash.find( stampKey );
            if ( iter == stamp._mapSourceHash.end() )
                return false;
            stampedSourceHash         = iter->second;
            const auto dependencyIter = stamp._mapDependency.find( stampKey );
            if ( dependencyIter != stamp._mapDependency.end() )
                listDependency = dependencyIter->second;
        }
        if ( stampedSourceHash != sourceHash )
            return false;

        // 이 소스가 연 헤더만 본다. 없어졌거나 읽지 못하거나 내용이 다르면 낡음이다.
        vector<string> listHeaderPath;
        listHeaderPath.reserve( listDependency.size() );
        for ( const Internal::StampDependency& dependency : listDependency )
        {
            string headerPath = Internal::resolveHeaderKey( resourceRoot, listIncludeRoot, shadersDir, dependency._headerKey );
            if ( headerPath.empty() || ContentHash::computeFile( headerPath ) != dependency._hash )
                return false;
            listHeaderPath.push_back( std::move( headerPath ) );
        }
        if ( pOutListIncludedFile != nullptr )
            *pOutListIncludedFile = std::move( listHeaderPath );
        return true;
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
