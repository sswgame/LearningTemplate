/**
 * @file ShaderBakeStamp.cpp
 * @brief 구운 산출물이 최신인지 판정한다 — **파일 시간이 아니라 내용 해시**로.
 * @details 이 저장소는 구운 바이너리까지 커밋하므로 `git pull` 이 소스와 산출물의 mtime 을 임의의 순서로 덮어쓴다.
 *          예전 판정(`산출물 mtime >= 소스 mtime`)은 그때 "이미 최신" 이라 답했고, 그래서 `forwardlit` 바이너리가
 *          라이트 버퍼 이전 것으로 커밋된 채 돌았다 — Vulkan 만 다른 그림을 내는 것을 백엔드 버그로 오인해 오래 쫓았다.
 *          그 판정이 이 파일 하나에 모여 있다(`ShaderBakeStampTest.FreshnessIsJudgedByContentNotFileTime` 이 고정한다).
 */
#include "pch.h"

#include "Core/Concurrency/mutex.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Graphics/Shader/Compile/ShaderBaker.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    namespace
    {
        struct ShaderBakeStampInternal
        {
            /** @brief 공유 헤더(.hlsli) 내용 해시 캐시. 트리 전체를 매번 훑을 수는 없다. */
            inline static mutex _s_sharedHeaderMutex{};

            inline static uint64 _s_sharedHeaderHash{ 0 };

            inline static bool _s_bSharedHeaderHashCached{ false };

            /**
             * @brief 베이크 스탬프 헤더. **버전을 올리면 스탬프가 전부 불일치가 되어 한 번 다 다시 굽는다.**
             * @details 2 → 3 은 포맷이 아니라 **판정 기준**이 바뀐 것이다(파일 시간 → 내용 해시).
             *          3 이전 스탬프는 "구운 것이 소스와 같다" 는 보장을 하지 못하므로 믿지 않는다.
             */
            inline static const string kBakeStampHeader{ "SWBAKE 3" };

            /** @brief 스탬프의 16자리 hex 를 uint64 로. 형식이 아니면 0 (= 불일치 처리). */
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

            /** @brief 내용 해시 캐시 한 칸 — 그 해시를 뽑았을 때의 파일 크기 · 쓰기 시각. */
            struct ContentHashEntry
            {
                FileStamp _stamp{};
                uint64    _hash{ 0 };
            };
            /**
             * @brief 경로 -> 내용 해시. 파일의 크기 · 쓰기 시각이 그대로일 때만 쓴다.
             * @details 셰이더 요청 하나가 같은 소스를 세 번 읽었다(`ShaderCache::getOrCompile` 의 메모리 캐시 확인 · 로컬 캐시
             *          경로 · 굽기 신선도) — 퍼뮤테이션 · 단계마다. 이 PC 에서 파일 하나 여는 데 ~200 us 라 시작 시간 게임
             *          스레드의 12.6 % 가 이 읽기였다. 이제 두 번째부터는 파일을 열지 않고 크기 · 시각만 본다(~75 us). 실행 중에
             *          고친 파일은 시각이 달라져 다시 읽는다 — 편집을 알아채는 것은 예전과 같다.
             */
            inline static unordered_map<string, ContentHashEntry> _s_mapContentHash{};
            /// @brief `_s_mapContentHash` 의 잠금. 셰이더 컴파일은 워커에서도 돈다. `_s_sharedHeaderMutex` 안에서 잡힐 수 있다(그 반대는 없다).
            inline static mutex _s_contentHashMutex{};

            /**
             * @brief 소스 한 파일의 **내용 해시** — CR 을 뺀 바이트의 FNV-1a 64.
             * @details 베이크 스탬프·베이크 판정·컴파일 캐시 키가 **같은 값**을 써야 한다. 예전에는
             *          스탬프만 내용 해시였고 판정은 파일 시간이었는데, 이 저장소는 구운 바이너리까지
             *          커밋하므로 `git pull` 이 소스와 산출물의 mtime 을 **임의의 순서**로 덮어쓴다 —
             *          그러면 소스가 바뀌었는데도 "산출물이 더 새것" 이라 판정돼 그대로 넘어간다.
             *          실제로 그 일이 일어나 `forwardlit` 바이너리가 라이트 버퍼 이전 것으로 커밋됐고,
             *          Vulkan 만 다른 그림을 내는 것을 **백엔드 버그로 오인**해 오래 쫓았다.
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
                // 읽는 사이에 파일이 바뀌었으면 옛 시각에 새 해시가 붙는다 — 다음 조회는 시각이 달라 다시 읽으므로 틀린 값이 남지 않는다.
                if ( bStamped && hash != 0 )
                {
                    std::scoped_lock<mutex> lock{ _s_contentHashMutex };
                    _s_mapContentHash[string( absPath )] = ContentHashEntry{ stamp, hash };
                }
                return hash;
            }

            /** @brief 파일을 읽어 CR 을 뺀 바이트의 FNV-1a 64 를 구합니다 (`computeContentHash` 의 캐시 밖 몸통). */
            static uint64 readContentHash( string_view absPath )
            {
                vector<uint8> bytes;
                if ( FileUtil::readFile( absPath, bytes ) == false )
                    return 0;

                // 줄 끝 정규화는 writeBakeStamp · CookAssets.py 와 같아야 한다(스탬프 주석 참고).
                vector<uint8> normalizedByte;
                normalizedByte.reserve( bytes.size() );
                for ( const uint8 byte : bytes )
                {
                    if ( byte != static_cast<uint8>( '\r' ) )
                        normalizedByte.push_back( byte );
                }
                return StringUtil::computeHash64( reinterpret_cast<const utf8*>( normalizedByte.data() ), normalizedByte.size(), false );
            }

            /** @brief `bake.stamp` 한 장을 읽은 결과. */
            struct StampInfo
            {
                /// @brief 상대경로(소문자) → 그 소스의 내용 해시. 스탬프가 없거나 버전이 다르면 빈 맵이다.
                unordered_map<string, uint64> _mapHash;
                /// @brief 스탬프에 적힌 모든 `.hlsli` 해시가 지금 소스와 같은가.
                bool _bHeadersCurrent{ false };
            };

            /// @brief `bin/<rhi>` 폴더별 스탬프 읽기 캐시 — 베이크 루프가 레시피마다 부른다.
            inline static unordered_map<string, StampInfo> _s_mapStamp{};

            /**
             * @brief `bin/<rhi>/bake.stamp` 를 읽어 소스 해시 표를 돌려줍니다 (폴더당 한 번 읽고 캐시).
             * @details 스탬프 버전이 다르면 **빈 표**를 돌려준다 — 그러면 모든 것이 낡은 것으로 판정돼
             *          한 번 전부 다시 굽는다. 버전을 올리는 것이 곧 강제 재굽기다.
             * @warning 참조를 돌려주므로 **`_s_sharedHeaderMutex` 를 쥔 채로만** 부를 것. 캐시에 삽입이
             *          일어나면 앞서 돌려준 참조가 무효가 된다.
             */
            static const StampInfo& readBakeStamp( const string& binDirectory, const string& shadersDir )
            {
                const auto cached = _s_mapStamp.find( binDirectory );
                if ( cached != _s_mapStamp.end() )
                    return cached->second;

                StampInfo    info{};
                const string stampPath = FileUtil::joinPath( binDirectory, "bake.stamp" );
                string       text;
                if ( FileUtil::readTextFile( stampPath, text ) && text.compare( 0, kBakeStampHeader.size(), kBakeStampHeader ) == 0 )
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

                        // 베이커는 '\n' 으로 쓰지만 이 파일은 저장소가 추적한다 — autocrlf 가 켜진 윈도우에서
                        // 체크아웃하면 CRLF 로 내려오고, 그 '\r' 이 경로 키 끝에 붙으면 모든 조회가 빗나가
                        // **갓 체크아웃한 트리가 통째로 "낡음"** 으로 판정된다(전부 다시 굽는다).
                        string key = line.substr( space + 1 );
                        if ( key.empty() == false && key.back() == '\r' )
                            key.pop_back();
                        info._mapHash.emplace( std::move( key ), parseHex64( line.substr( 0, space ) ) );
                    }
                }

                // 공유 헤더는 어느 셰이더가 무엇을 include 하는지 파싱하지 않고 **전부** 본다 —
                // 넉넉하게 굽는 쪽이 안전하다(과거 타임스탬프 판정과 같은 정책).
                info._bHeadersCurrent = info._mapHash.empty() == false;
                vector<string> listHeader;
                FileUtil::collectFiles( shadersDir, ".hlsli", listHeader, true );
                for ( const string& headerPath : listHeader )
                {
                    const string rel  = makeStampKey( headerPath, shadersDir );
                    const auto   iter = info._mapHash.find( rel );
                    if ( iter == info._mapHash.end() || iter->second != computeContentHash( headerPath ) )
                    {
                        info._bHeadersCurrent = false;
                        break;
                    }
                }

                return _s_mapStamp.emplace( binDirectory, std::move( info ) ).first->second;
            }

            /** @brief 소스 절대경로를 스탬프 키(shaders/ 기준 소문자 상대경로)로 바꿉니다. */
            static string makeStampKey( string_view absSourcePath, const string& shadersDir )
            {
                const string norm = FileUtil::normalizeSeparators( absSourcePath );
                if ( norm.size() <= shadersDir.size() + 1 )
                    return string{};
                return StringUtil::toLower( norm.substr( shadersDir.size() + 1 ).c_str() );
            }

            /**
             * @brief `bin/<rhi>/bake.stamp` 에 셰이더 소스의 **내용 해시**를 적습니다.
             * @details 쿠커(CookAssets.py)가 "지금 팩에 넣으려는 바이너리가 지금 이 소스에서 나온
             *          것인가" 를 파일 시간이 아니라 내용으로 확인하기 위한 것이다. 파일 시간은
             *          `git clone` 이 전부 체크아웃 시각으로 덮어써서 비교 자체가 무의미해진다.
             *          형식은 한 줄에 `<FNV-1a 64 16자리 hex> <shaders/ 기준 상대 경로>` 다. 해시는 CR 을
             *          뺀 바이트로 계산한다(`computeContentHash`) — 판정 쪽과 같은 함수다.
             * @param binDirectory 매니페스트를 쓴 폴더 (`<domain>/shaders/bin/<rhi>`)
             */
            static void writeBakeStamp( string_view binDirectory )
            {
                // <domain>/shaders/bin/<rhi> → <domain>/shaders
                const string rhiDir     = FileUtil::normalizeSeparators( binDirectory );
                const string parentDir  = FileUtil::getDirectoryPart( rhiDir );
                const string shadersDir = FileUtil::getDirectoryPart( parentDir );
                if ( shadersDir.empty() )
                    return;

                vector<string> listSource;
                FileUtil::collectFiles( shadersDir, ".hlsl", listSource, true );
                FileUtil::collectFiles( shadersDir, ".hlsli", listSource, true );

                vector<string> listLine;
                listLine.reserve( listSource.size() );
                for ( const string& sourcePath : listSource )
                {
                    const string normSource = FileUtil::normalizeSeparators( sourcePath );
                    if ( normSource.find( "/bin/" ) != string::npos )
                        continue;
                    if ( normSource.size() <= shadersDir.size() + 1 )
                        continue;

                    // 해싱과 키 만들기는 **판정 쪽과 같은 함수**를 쓴다 — 스탬프와 판정이 다른 규칙을
                    // 쓰던 것이 이 파일이 고치는 버그였다. 줄 끝 정규화 사연은 computeContentHash 주석에 있다.
                    const uint64 hash    = computeContentHash( normSource );
                    const string relPath = makeStampKey( normSource, shadersDir );
                    if ( hash == 0 || relPath.empty() )
                        continue;

                    StringBuilder<constant::kMaxBuffer256> sb;
                    sb.appendFormat( "%#", Fmt( hash, Format( 16, Format::Padding::Zero ).hex() ) );
                    sb.append( ' ' ).append( relPath );
                    listLine.push_back( string( sb.c_str(), sb.size() ) );
                }

                std::sort( listLine.begin(), listLine.end() );

                string text = kBakeStampHeader + "\n";
                for ( const string& line : listLine )
                {
                    text += line;
                    text += "\n";
                }

                const string stampPath = FileUtil::joinPath( rhiDir, "bake.stamp" );
                if ( FileUtil::writeTextFile( stampPath, text ) == false )
                    SW_LOG_WARNING( "베이크 스탬프 쓰기 실패: %#", stampPath.c_str() );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "ShaderBaker" );

    void ShaderBaker::writeBakeStamp( string_view binDirectory )
    {
        ShaderBakeStampInternal::writeBakeStamp( binDirectory );
    }

    void ShaderBaker::invalidateSharedHeaderCache()
    {
        std::scoped_lock<mutex> lock{ ShaderBakeStampInternal::_s_sharedHeaderMutex };
        ShaderBakeStampInternal::_s_bSharedHeaderHashCached = false;
        ShaderBakeStampInternal::_s_mapStamp.clear();
        // 내용 해시는 파일 시각으로 스스로 낡음을 알지만, 수동 리로드는 "전부 다시 본다" 는 약속이라 같이 버린다.
        std::scoped_lock<mutex> contentLock{ ShaderBakeStampInternal::_s_contentHashMutex };
        ShaderBakeStampInternal::_s_mapContentHash.clear();
    }

    uint64 ShaderBaker::getSharedHeaderContentHash()
    {
        std::scoped_lock<mutex> lock{ ShaderBakeStampInternal::_s_sharedHeaderMutex };
        if ( ShaderBakeStampInternal::_s_bSharedHeaderHashCached )
            return ShaderBakeStampInternal::_s_sharedHeaderHash;

        // 경로까지 섞는다 — 헤더를 **지우기만** 해도 값이 달라져야 한다(내용만 XOR 하면 같은 내용 둘이
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
                combined += StringUtil::computeHash64( norm, false, ShaderBakeStampInternal::computeContentHash( headerPath ) );
            }
        }

        ShaderBakeStampInternal::_s_sharedHeaderHash        = combined;
        ShaderBakeStampInternal::_s_bSharedHeaderHashCached = true;
        return combined;
    }

    uint64 ShaderBaker::computeEffectiveSourceHash( string_view absShaderPath )
    {
        const uint64 sourceHash = ShaderBakeStampInternal::computeContentHash( absShaderPath );
        if ( sourceHash == 0 )
            return 0;
        return StringUtil::computeHash64( to_string( getSharedHeaderContentHash() ), false, sourceHash );
    }

    bool ShaderBaker::isBakedOutputCurrent( string_view binDirectory, string_view absShaderPath )
    {
        // <domain>/shaders/bin/<rhi> → <domain>/shaders (writeBakeStamp 와 같은 되짚기)
        const string rhiDir     = FileUtil::normalizeSeparators( binDirectory );
        const string shadersDir = FileUtil::getDirectoryPart( FileUtil::getDirectoryPart( rhiDir ) );
        if ( shadersDir.empty() )
            return false;

        const string normSource = FileUtil::normalizeSeparators( absShaderPath );
        const uint64 sourceHash = ShaderBakeStampInternal::computeContentHash( normSource );
        if ( sourceHash == 0 )
            return false;

        std::scoped_lock<mutex> lock{ ShaderBakeStampInternal::_s_sharedHeaderMutex };

        const ShaderBakeStampInternal::StampInfo& stamp = ShaderBakeStampInternal::readBakeStamp( rhiDir, shadersDir );
        if ( stamp._bHeadersCurrent == false )
            return false;

        const string stampKey = ShaderBakeStampInternal::makeStampKey( normSource, shadersDir );
        const auto   iter     = stamp._mapHash.find( stampKey );
        return iter != stamp._mapHash.end() && iter->second == sourceHash;
    }
} // namespace sw
