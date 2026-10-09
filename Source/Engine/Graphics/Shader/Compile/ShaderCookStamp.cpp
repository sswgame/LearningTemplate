/**
 * @file ShaderCookStamp.cpp
 * @brief 쿠킹된 산출물이 최신인지 판정합니다. **파일 시간이 아니라 내용 해시**로 봅니다.
 * @details 이 저장소는 쿠킹된 바이너리까지 커밋하므로 `git pull` 이 소스와 산출물의 mtime 을 임의의 순서로 덮어씁니다.
 *          주의: `산출물 mtime >= 소스 mtime` 으로 판정하면 그때 "이미 최신" 이라 답해 낡은 바이너리가 커밋된 채 돌고,
 *          한 백엔드만 다른 그림을 내 백엔드 버그로 오인하게 됩니다. 그 판정이 이 파일 하나에 모여 있습니다(`ShaderCookStampTest.FreshnessIsJudgedByContentNotFileTime` 이 고정합니다).
 */
#include "pch.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    namespace
    {
        struct ShaderCookStampInternal
        {
            /** @brief 공유 헤더(.hlsli) 내용 해시 캐시입니다. 트리 전체를 매번 훑을 수는 없습니다. */
            inline static mutex _s_sharedHeaderMutex{};

            inline static uint64 _s_sharedHeaderHash{ 0 };

            inline static bool _s_bSharedHeaderHashCached{ false };

            /**
             * @brief 쿠킹 스탬프 헤더입니다. **버전을 올리면 스탬프가 모두 불일치가 되어 한 번 전부 다시 쿠킹합니다.**
             * @details 2 → 3 은 포맷이 아니라 **판정 기준**이 바뀐 것입니다(파일 시간 → 내용 해시).
             *          3 이전 스탬프는 "쿠킹된 것이 소스와 같다" 는 보장을 하지 못하므로 믿지 않습니다.
             */
            inline static const string kCookStampHeader{ "SWCOOK 3" };

            /** @brief 스탬프의 16자리 hex 를 uint64 로 바꿉니다. 형식이 아니면 0(= 불일치 처리)입니다. */
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

            /** @brief 내용 해시 캐시 한 칸입니다. 그 해시를 뽑았을 때의 파일 크기 · 쓰기 시각을 함께 둡니다. */
            struct ContentHashEntry
            {
                FileStamp _stamp{};
                uint64    _hash{ 0 };
            };
            /**
             * @brief 경로 → 내용 해시입니다. 파일의 크기 · 쓰기 시각이 그대로일 때만 씁니다.
             * @details 셰이더 요청 하나가 같은 소스를 세 번 봅니다(`ShaderCache::getOrCompile` 의 메모리 캐시 확인 · 로컬 캐시
             *          경로 · 쿠킹 신선도). 퍼뮤테이션 · 단계마다입니다. 파일 하나 여는 데 ~200 us 라 매번 읽으면 시작 시간 게임
             *          스레드의 큰 몫(12.6 %)이 되므로, 두 번째부터는 파일을 열지 않고 크기 · 시각만 봅니다(~75 us). 실행 중에
             *          고친 파일은 시각이 달라져 다시 읽으므로 편집은 그대로 알아챕니다.
             */
            inline static unordered_map<string, ContentHashEntry> _s_mapContentHash{};
            /// @brief `_s_mapContentHash` 의 잠금입니다. 셰이더 컴파일은 워커에서도 돕니다. `_s_sharedHeaderMutex` 안에서 잡힐 수 있습니다(그 반대는 없습니다).
            inline static mutex _s_contentHashMutex{};

            /**
             * @brief 소스 한 파일의 **내용 해시**입니다. CR 을 뺀 바이트의 FNV-1a 64 입니다.
             * @details 쿠킹 스탬프 · 쿠킹 판정 · 컴파일 캐시 키가 **같은 값**을 써야 합니다. 이 저장소는 쿠킹된 바이너리까지
             *          커밋하므로 `git pull` 이 소스와 산출물의 mtime 을 **임의의 순서**로 덮어씁니다. 어느 하나라도 파일 시간으로
             *          판정하면 소스가 바뀌었는데도 "산출물이 더 새것" 이라 그대로 넘어가, 한 백엔드만 다른 그림을 내는 것을
             *          **백엔드 버그로 오인**하게 됩니다.
             */
            static uint64 computeContentHash( string_view absPath )
            {
                FileStamp  stamp{};
                const bool bStamped = FileUtil::getFileStamp( absPath, stamp );
                if ( bStamped )
                {
                    std::scoped_lock<mutex> lock{ _s_contentHashMutex };
                    const auto              iter = _s_mapContentHash.find( absPath );
                    if ( iter != _s_mapContentHash.end() && iter->second._stamp == stamp )
                        return iter->second._hash;
                }

                const uint64 hash = readContentHash( absPath );
                // 읽는 사이에 파일이 바뀌었으면 옛 시각에 새 해시가 붙는다. 다음 조회는 시각이 달라 다시 읽으므로 틀린 값이 남지 않는다.
                if ( bStamped && hash != 0 )
                {
                    std::scoped_lock<mutex> lock{ _s_contentHashMutex };
                    _s_mapContentHash[string( absPath )] = ContentHashEntry{ stamp, hash };
                }
                return hash;
            }

            /** @brief 파일을 읽어 CR 을 뺀 바이트의 FNV-1a 64 를 구합니다(`computeContentHash` 의 캐시 밖 몸통). */
            static uint64 readContentHash( string_view absPath )
            {
                vector<uint8> bytes;
                if ( FileUtil::readFile( absPath, bytes ) == false )
                    return 0;

                // 줄 끝 정규화는 writeCookStamp · CookAssets.py 와 같아야 한다(스탬프 주석 참고).
                vector<uint8> normalizedByte;
                normalizedByte.reserve( bytes.size() );
                for ( const uint8 byte : bytes )
                {
                    if ( byte != static_cast<uint8>( '\r' ) )
                        normalizedByte.push_back( byte );
                }
                return StringUtil::computeHash64( reinterpret_cast<const utf8*>( normalizedByte.data() ), normalizedByte.size(), false );
            }

            /** @brief 스탬프 한 줄의 원천입니다 — 키와 그 소스의 절대 경로입니다. */
            struct StampedSource
            {
                string _key;
                string _absPath;
            };

            /**
             * @brief 스탬프가 볼 소스를 모읍니다. 자기 도메인의 `.hlsl`(@p bHeadersOnly 가 아니면) · `.hlsli` 와, 다른 include 루트 도메인
             *        (`ShaderCooker::kArrIncludeRootDomain`)의 `.hlsli` 입니다.
             * @details 셰이더는 자기 폴더 밖 include 루트(engine · common)의 헤더도 include 한다. 자기 도메인만 보면 engine 헤더를 고쳐도
             *          common 셰이더가 "최신" 으로 남는다. 다른 도메인 헤더의 키는 `<도메인>:<상대 경로>` 다(자기 키에는 ':' 가 없다).
             *          `CookAssets.py` 의 `collectStampedSourceHashesInternal` 이 같은 규칙이다.
             */
            static void collectStampedSources( const string& shadersDir, bool bHeadersOnly, vector<StampedSource>& outListSource )
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

                const string ownDirLower = StringUtil::toLower( shadersDir.c_str() );
                for ( const utf8* pDomain : ShaderCooker::kArrIncludeRootDomain )
                {
                    const string rootDir = FileUtil::normalizeSeparators( ResourceUtil::getDomainFolderPath( pDomain, "shaders" ) );
                    if ( rootDir.empty() || StringUtil::toLower( rootDir.c_str() ) == ownDirLower )
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
            }

            /** @brief `cook.stamp` 한 장을 읽은 결과입니다. */
            struct StampInfo
            {
                /// @brief 상대 경로(소문자) → 그 소스의 내용 해시입니다. 스탬프가 없거나 버전이 다르면 빈 맵입니다.
                unordered_map<string, uint64> _mapHash;
                /// @brief 스탬프에 적힌 모든 `.hlsli` 해시가 지금 소스와 같은지 여부입니다.
                bool _bHeadersCurrent{ false };
            };

            /// @brief `bin/<rhi>` 폴더별 스탬프 읽기 캐시입니다. 쿠킹 루프가 요청마다 부릅니다.
            inline static unordered_map<string, StampInfo> _s_mapStamp{};

            /**
             * @brief `bin/<rhi>/cook.stamp` 를 읽어 소스 해시 표를 반환합니다(폴더당 한 번 읽고 캐시합니다).
             * @details 스탬프 버전이 다르면 **빈 표**를 반환합니다. 그러면 모든 것이 낡은 것으로 판정돼
             *          한 번 전부 다시 쿠킹합니다. 버전을 올리는 것이 곧 강제 재쿠킹입니다.
             * @warning 참조를 반환하므로 **`_s_sharedHeaderMutex` 를 쥔 채로만** 부르십시오. 캐시에 삽입이
             *          일어나면 앞서 반환한 참조가 무효가 됩니다.
             */
            static const StampInfo& readCookStamp( const string& binDirectory, const string& shadersDir )
            {
                const auto cached = _s_mapStamp.find( binDirectory );
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

                // 헤더는 어느 셰이더가 무엇을 include 하는지 파싱하지 않고 include 루트의 것을 **모두** 본다. 넉넉하게 쿠킹하는 쪽이 안전하다.
                info._bHeadersCurrent = info._mapHash.empty() == false;
                vector<StampedSource> listHeader;
                collectStampedSources( shadersDir, true, listHeader );
                for ( const StampedSource& header : listHeader )
                {
                    const auto iter = info._mapHash.find( header._key );
                    if ( iter == info._mapHash.end() || iter->second != computeContentHash( header._absPath ) )
                    {
                        info._bHeadersCurrent = false;
                        break;
                    }
                }

                return _s_mapStamp.emplace( binDirectory, std::move( info ) ).first->second;
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
             * @details 쿠커(CookAssets.py)가 "지금 팩에 넣으려는 바이너리가 지금 이 소스에서 나온
             *          것인가" 를 파일 시간이 아니라 내용으로 확인하기 위한 것입니다. 파일 시간은
             *          `git clone` 이 모두 체크아웃 시각으로 덮어써서 비교 자체가 무의미해집니다.
             *          형식은 한 줄에 `<FNV-1a 64 16자리 hex> <shaders/ 기준 상대 경로>` 입니다. 해시는 CR 을
             *          뺀 바이트로 계산합니다(`computeContentHash`). 판정 쪽과 같은 함수입니다.
             * @param binDirectory 매니페스트를 쓴 폴더(`<domain>/shaders/bin/<rhi>`)
             */
            static void writeCookStamp( string_view binDirectory, const unordered_set<string>* pFailedSource )
            {
                // <domain>/shaders/bin/<rhi> → <domain>/shaders
                const string rhiDir     = FileUtil::normalizeSeparators( binDirectory );
                const string parentDir  = FileUtil::getDirectoryPart( rhiDir );
                const string shadersDir = FileUtil::getDirectoryPart( parentDir );
                if ( shadersDir.empty() )
                    return;

                vector<StampedSource> listSource;
                collectStampedSources( shadersDir, false, listSource );

                vector<string> listLine;
                listLine.reserve( listSource.size() );
                for ( const StampedSource& source : listSource )
                {
                    if ( pFailedSource != nullptr && pFailedSource->find( source._absPath ) != pFailedSource->end() )
                        continue; // 이번에 쿠킹하지 못했다 — 최신이 아니다

                    // 해싱과 키 만들기는 **판정 쪽과 같은 함수**를 쓴다(스탬프와 판정이 다른 규칙을 쓰면 낡은 산출물이 "최신" 이 된다).
                    const uint64 hash = computeContentHash( source._absPath );
                    if ( hash == 0 )
                        continue;

                    StringBuilder<constant::kMaxBuffer256> sb;
                    sb.appendFormat( "%#", Fmt( hash, Format( 16, Format::Padding::Zero ).hex() ) );
                    sb.append( ' ' ).append( source._key );
                    listLine.push_back( string( sb.c_str(), sb.size() ) );
                }

                std::sort( listLine.begin(), listLine.end() );

                string text = kCookStampHeader + "\n";
                for ( const string& line : listLine )
                {
                    text += line;
                    text += "\n";
                }

                const string stampPath = FileUtil::joinPath( rhiDir, "cook.stamp" );
                if ( FileUtil::writeTextFile( stampPath, text ) == false )
                    SW_LOG_WARNING( "쿠킹 스탬프 쓰기 실패: %#", stampPath.c_str() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "ShaderCooker" );

    void ShaderCooker::writeCookStamp( string_view binDirectory, const unordered_set<string>* pFailedSource )
    {
        ShaderCookStampInternal::writeCookStamp( binDirectory, pFailedSource );
    }

    void ShaderCooker::invalidateSharedHeaderCache()
    {
        std::scoped_lock<mutex> lock{ ShaderCookStampInternal::_s_sharedHeaderMutex };
        ShaderCookStampInternal::_s_bSharedHeaderHashCached = false;
        ShaderCookStampInternal::_s_mapStamp.clear();
        // 내용 해시는 파일 시각으로 스스로 낡음을 알지만, 수동 리로드는 "전부 다시 본다" 는 약속이라 같이 버린다.
        std::scoped_lock<mutex> contentLock{ ShaderCookStampInternal::_s_contentHashMutex };
        ShaderCookStampInternal::_s_mapContentHash.clear();
    }

    uint64 ShaderCooker::getSharedHeaderContentHash()
    {
        std::scoped_lock<mutex> lock{ ShaderCookStampInternal::_s_sharedHeaderMutex };
        if ( ShaderCookStampInternal::_s_bSharedHeaderHashCached )
            return ShaderCookStampInternal::_s_sharedHeaderHash;

        // 경로까지 섞는다. 헤더를 **지우기만** 해도 값이 달라져야 한다(내용만 XOR 하면 같은 내용 둘이
        // 서로를 지운다). 정렬은 collectFiles 순서에 기대지 않고 곱셈 누적으로 순서 무관하게 만든다.
        uint64        combined = 0;
        const string& rootDir  = ResourceUtil::getRootFolderPath();
        if ( rootDir.empty() == false )
        {
            vector<string> listHeader;
            FileUtil::collectFiles( rootDir, ".hlsli", listHeader, true );
            for ( const string& headerPath : listHeader )
            {
                const string norm = StringUtil::toLower( FileUtil::normalizeSeparators( headerPath ).c_str() );
                combined += StringUtil::computeHash64( norm, false, ShaderCookStampInternal::computeContentHash( headerPath ) );
            }
        }

        ShaderCookStampInternal::_s_sharedHeaderHash        = combined;
        ShaderCookStampInternal::_s_bSharedHeaderHashCached = true;
        return combined;
    }

    uint64 ShaderCooker::computeEffectiveSourceHash( string_view absShaderPath )
    {
        const uint64 sourceHash = ShaderCookStampInternal::computeContentHash( absShaderPath );
        if ( sourceHash == 0 )
            return 0;
        return StringUtil::computeHash64( to_string( getSharedHeaderContentHash() ), false, sourceHash );
    }

    bool ShaderCooker::isCookedOutputCurrent( string_view binDirectory, string_view absShaderPath )
    {
        // <domain>/shaders/bin/<rhi> → <domain>/shaders (writeCookStamp 와 같은 되짚기)
        const string rhiDir     = FileUtil::normalizeSeparators( binDirectory );
        const string shadersDir = FileUtil::getDirectoryPart( FileUtil::getDirectoryPart( rhiDir ) );
        if ( shadersDir.empty() )
            return false;

        const string normSource = FileUtil::normalizeSeparators( absShaderPath );
        const uint64 sourceHash = ShaderCookStampInternal::computeContentHash( normSource );
        if ( sourceHash == 0 )
            return false;

        std::scoped_lock<mutex> lock{ ShaderCookStampInternal::_s_sharedHeaderMutex };

        const ShaderCookStampInternal::StampInfo& stamp = ShaderCookStampInternal::readCookStamp( rhiDir, shadersDir );
        if ( stamp._bHeadersCurrent == false )
            return false;

        const string stampKey = ShaderCookStampInternal::makeStampKey( normSource, shadersDir );
        const auto   iter     = stamp._mapHash.find( stampKey );
        return iter != stamp._mapHash.end() && iter->second == sourceHash;
    }
} // namespace sw
