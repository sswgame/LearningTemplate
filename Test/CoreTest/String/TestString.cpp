#include "pch.h"

#include "Core/Common/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"
#include "Core/String/hashed_string.h"

#include "TestFramework/TestFramework.h"

/**
 * @brief [StringTest] 넓은 문자가 하위 한 바이트로 잘리지 않는다
 * @details 부호 확장을 막으려고 `uint8` 로 **고정**하면 안 된다. 이 해시 템플릿은 `utf16`
 *          으로도 불리는데(`std::hash<fixed_wstring>`), 그러면 넓은 문자가 하위 한 바이트만 남는다 —
 *          한글처럼 상위 바이트가 의미를 갖는 문자열이 통째로 한 버킷에 뭉친다. 부호 없는 타입은
 *          맞되 **폭은 CharT 를 따라야** 한다.
 * @note 부호 확장 쪽 주장은 `NonAsciiBytesAreUnsigned` 가 든다.
 */
SW_TEST_CASE( StringTest, WideCharHashIsNotTruncatedToOneByte )
{
    // 상위 바이트만 다르고 하위 바이트는 둘 다 0 이다.
    const utf16 arrWideA[] = { static_cast<utf16>( 0xAC00 ), static_cast<utf16>( 0 ) };
    const utf16 arrWideB[] = { static_cast<utf16>( 0xAD00 ), static_cast<utf16>( 0 ) };

    SW_EXPECT_TRUE( sw::StringUtil::computeHash64( arrWideA, 1 ) != sw::StringUtil::computeHash64( arrWideB, 1 ) );
    SW_EXPECT_TRUE( sw::StringUtil::computeHash32( arrWideA, 1 ) != sw::StringUtil::computeHash32( arrWideB, 1 ) );

    // ASCII utf8 값은 바뀌면 안 된다 — 셰이더 쿠킹 스탬프 같은 것이 이 값으로 디스크에 남는다.
    SW_EXPECT_EQUAL( sw::HashUtil::kFnvOffset64, sw::StringUtil::computeHash64( "", 0, false ) );
    SW_EXPECT_EQUAL( ( sw::HashUtil::kFnvOffset64 ^ uint64{ 'a' } ) * sw::HashUtil::kFnvPrime64,
                     sw::StringUtil::computeHash64( "a", 1, false ) );
}

/**
 * @brief [StringTest] 비워진 intern 테이블은 다시 세워진다 — 빈 채로 남지 않는다
 * @details 테이블 인스턴스는 `HashedStringPool::initialize` 안의 **함수 지역 static** 이라 두 번째
 *          initialize 에서는 생성자가 돌지 않는다. `shutdown` 이 `clear()` 로 0번 청크와 사전 정의
 *          이름까지 돌려준 뒤라, 그대로 두면 `hashed_string( NameType_float3 )` 의 `c_str()` 이
 *          nullptr 이고 새로 intern 되는 첫 문자열이 0번(`NameType_None`)을 받는다.
 *          전역 풀을 건드리면 다른 테스트가 들고 있는 인덱스가 어긋나므로 저장소만 따로 세워 본다.
 */
SW_TEST_CASE( StringTest, ClearedInternTableIsRebuiltNotLeftEmpty )
{
    const uint32 predefinedCount = static_cast<uint32>( sw::PredefinedNameType::Count );

    sw::hashed_string::AllocationInfo info;
    SW_EXPECT_EQUAL( predefinedCount, info._entryCount.load() );
    SW_ASSERT_TRUE( info._arrChunk[0].load() != nullptr );

    info.clear();
    SW_EXPECT_EQUAL( uint32{ 0 }, info._entryCount.load() );
    SW_EXPECT_TRUE( info._arrChunk[0].load() == nullptr );

    info.initializeStorage();
    SW_EXPECT_EQUAL( predefinedCount, info._entryCount.load() );
    SW_EXPECT_TRUE( info._arrChunk[0].load() != nullptr );

    info.clear();
}

/**
 * @brief [StringTest] 비운 intern 테이블은 저장소(샤드 맵의 버킷 · 밀집 배열, 아레나 목록)까지 돌려준다
 * @details `HashedStringPool::shutdown` 은 이 `clear()` 하나다. 맵 · 목록의 `clear()` 는 원소만 지우고 저장소를 남기므로, 비운 뒤에도 블록이
 *          살아 있으면 엔진 종료 누수 보고(기준선 대비 태그 증가)에 남는다. 테이블을 세웠다 비운 전후로 살아 있는 블록 수가 같아야 한다.
 */
SW_TEST_CASE( StringTest, ClearedInternTableReturnsItsStorage )
{
    if constexpr ( sw::kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "allocation headers are compiled out in this configuration" );
    const sw::MemoryProfiler* pProfiler = sw::MemoryProfiler::getActive();
    if ( pProfiler == nullptr || pProfiler->isTrackingEnabled() == false )
        SW_TEST_SKIP( "no tracking memory profiler in this host" );

    const uint64 liveCountBefore = pProfiler->getLiveAllocationCount();
    uint64       liveCountHeld{ 0 };
    uint64       liveCountCleared{ 0 };
    {
        sw::hashed_string::AllocationInfo info;
        liveCountHeld = pProfiler->getLiveAllocationCount();
        info.clear();
        liveCountCleared = pProfiler->getLiveAllocationCount();
    }
    // 세운 테이블이 실제로 블록을 잡았어야 아래 비교가 뜻이 있다.
    SW_ASSERT_TRUE( liveCountHeld > liveCountBefore );
    // 살아 있는 블록 수는 프로세스 전체 값이라 다른 스레드(비동기 로거 등)의 할당 · 해제가 한두 개 섞인다 — 고치기 전 차이는 수십 블록이다.
    SW_EXPECT_TRUE_MSG( liveCountCleared <= liveCountBefore + 2u, "the cleared intern table still holds storage blocks" );
}

/**
 * @brief [StringTest] 이름 풀의 블록은 부른 쪽의 태그가 아니라 `EngineMisc` 로 센다
 * @details 새 이름을 넣으면 청크 · 아레나 블록 · 샤드 맵이 자란다. 둘러싼 스코프 태그로 세면 같은 몫이 "누가 먼저 넣었나" 에 따라 태그를
 *          옮겨 다닌다(종료 보고에서 Scene · Mesh 로 보였다). 새 이름 2048 개면 청크 경계(1024)를 반드시 한 번 넘는다.
 */
SW_TEST_CASE( StringTest, InternPoolMemoryIsTaggedEngineMisc )
{
    if constexpr ( sw::kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );
    const sw::MemoryProfiler* pProfiler = sw::MemoryProfiler::getActive();
    if ( pProfiler == nullptr || pProfiler->isTrackingEnabled() == false )
        SW_TEST_SKIP( "no tracking memory profiler in this host" );

    constexpr uint32 kNameCount = 2048;
    // `--test_repeat` 의 다음 판도 새 이름을 넣어야 시험이 헛돌지 않는다.
    static uint32 s_round{ 0 };
    ++s_round;
    sw::vector<sw::string> listName;
    listName.reserve( kNameCount );
    for ( uint32 index = 0; index < kNameCount; ++index )
    {
        listName.push_back( sw::string( "InternPoolTagProbe_" ) + sw::to_string( s_round ) + "_" + sw::to_string( index ) );
    }

    const uint32 internedBefore = sw::hashed_string::getInternedCount();
    const uint64 scriptBefore   = pProfiler->getStats( sw::MemoryTag::Script )._currentAllocatedBytes.load();
    uint32       nonEmptyCount{ 0 };
    {
        SW_MEMORY_SCOPE( Script );
        for ( const sw::string& name : listName )
        {
            const sw::hashed_string interned{ sw::string_view{ name } };
            nonEmptyCount += ( interned.size() > 0 ) ? 1u : 0u;
        }
    }
    const uint64 scriptAfter = pProfiler->getStats( sw::MemoryTag::Script )._currentAllocatedBytes.load();

    SW_ASSERT_EQUAL( kNameCount, nonEmptyCount );
    SW_ASSERT_TRUE( sw::hashed_string::getInternedCount() >= internedBefore + kNameCount );
    SW_EXPECT_EQUAL( scriptBefore, scriptAfter );
}

// ------------------------------------------------------------------------------
// 1) Core_String — Util·해시·스플리터·빌더
// ------------------------------------------------------------------------------
/**
 * @brief [StringTest] StringUtil 기본
 */
SW_TEST_CASE( StringTest, StringUtilBasic )
{
    SW_EXPECT_TRUE( sw::StringUtil::isNullOrEmpty( static_cast<const utf8*>( nullptr ) ) );
    SW_EXPECT_TRUE( sw::StringUtil::isNullOrEmpty( "" ) );
    SW_EXPECT_FALSE( sw::StringUtil::isNullOrEmpty( "Hello" ) );

    sw::string text = "  Hello World!  ";
    sw::string trimmed{ sw::StringUtil::trim( text ) };
    SW_EXPECT_EQUAL( sw::string( "Hello World!" ), trimmed );

    sw::string upper = sw::StringUtil::toUpper( "hello" );
    SW_EXPECT_EQUAL( sw::string( "HELLO" ), upper );

    sw::string lower = sw::StringUtil::toLower( "WORLD" );
    SW_EXPECT_EQUAL( sw::string( "world" ), lower );

    SW_EXPECT_TRUE( sw::StringUtil::equals( "RenderPass", "renderpass", true ) );
    SW_EXPECT_FALSE( sw::StringUtil::equals( "RenderPass", "renderpass", false ) );
    SW_EXPECT_TRUE( sw::StringUtil::equals( "HP", "hp", true ) );
    SW_EXPECT_FALSE( sw::StringUtil::equals( "Hero", "hero!", true ) );
    SW_EXPECT_TRUE( sw::StringUtil::equals( static_cast<const utf8*>( "Scene" ), "scene", true ) );
    SW_EXPECT_FALSE( sw::StringUtil::equals( static_cast<const utf8*>( nullptr ), "x", true ) );
    SW_EXPECT_EQUAL( 0, sw::StringUtil::compare( "abc", "abc" ) );
    SW_EXPECT_TRUE( sw::StringUtil::compare( "ABC", "abc", true ) == 0 );
    SW_EXPECT_TRUE( sw::StringUtil::compare( "abc", "def" ) < 0 );
    SW_EXPECT_TRUE( sw::StringUtil::compare( "def", "abc" ) > 0 );

    const sw::string_splitter parts{ "apple,banana,orange", { "," } };
    SW_EXPECT_EQUAL( 3u, parts.getCount() );
    if ( parts.getCount() == 3 )
    {
        SW_EXPECT_EQUAL( sw::string( "apple" ), sw::string( parts.getSplitList()[0] ) );
        SW_EXPECT_EQUAL( sw::string( "banana" ), sw::string( parts.getSplitList()[1] ) );
        SW_EXPECT_EQUAL( sw::string( "orange" ), sw::string( parts.getSplitList()[2] ) );
    }
}

/**
 * @brief [StringTest] 공통 접두·접미를 뺀 변경 구간
 */
SW_TEST_CASE( StringTest, StringChangeSpanStoresOnlyChangedMiddle )
{
    const sw::string           before = R"({"albedo":"white","roughness":0.5,"name":"Mat"})";
    const sw::string           after  = R"({"albedo":"white","roughness":0.8,"name":"Mat"})";
    const sw::StringChangeSpan span   = sw::StringUtil::makeChangeSpan( before, after );
    SW_EXPECT_TRUE( span._removed == "5" );
    SW_EXPECT_TRUE( span._added == "8" );
    SW_EXPECT_TRUE( span._prefixLength + span._suffixLength + span._removed.size() == before.size() );
    SW_EXPECT_TRUE( sw::StringUtil::reconstructBefore( span, after ) == before );
    SW_EXPECT_TRUE( sw::StringUtil::reconstructAfter( span, before ) == after );
}

/**
 * @brief [StringTest] 첫 편집 스팬이 이후 after에서도 역변환된다
 */
SW_TEST_CASE( StringTest, StringChangeSpanFirstEditReversesLaterAfter )
{
    const sw::string           before0   = R"({"field":"x"})";
    const sw::string           after1    = R"({"field":"xy"})";
    const sw::string           afterN    = R"({"field":"xyz"})";
    const sw::StringChangeSpan firstSpan = sw::StringUtil::makeChangeSpan( before0, after1 );
    SW_EXPECT_TRUE( sw::StringUtil::reconstructBefore( firstSpan, afterN ) == before0 );
}

/**
 * @brief [StringTest] hashed_string
 */
SW_TEST_CASE( StringTest, HashedString )
{
    sw::hashed_string defaultStr;
    sw::hashed_string str1( "TestKey" );
    sw::hashed_string str2( "testkey" );
    sw::hashed_string str3( "TESTKEY" );
    sw::hashed_string str4( "OtherKey" );

    SW_EXPECT_TRUE( defaultStr.empty() );
    SW_EXPECT_FALSE( str1.empty() );
    SW_EXPECT_TRUE( str1 == str2 );
    SW_EXPECT_TRUE( str1 == str3 );
    SW_EXPECT_EQUAL( str1.getIndex(), str2.getIndex() );
    SW_EXPECT_EQUAL( str1.getIndex(), str3.getIndex() );
    SW_EXPECT_TRUE( str1 != str4 );
}

/**
 * @brief [StringTest] hashed_string 은 FName 규칙이다 — 같음은 대소문자 무시, 표시는 적은 철자 그대로, 순서는 사전순 · 빠른 순을 고른다
 * @details 표시 철자를 처음 intern 된 것 하나로 나눠 쓰면 "Hero" 를 먼저 만든 뒤 `hashed_string( "hero" ).c_str()` 도 "Hero" 가 되어
 *          에디터의 `hero` → `Hero` 이름 바꾸기가 아무 일도 하지 않는다. `operator<` 가 intern 순서를 따르면 실행마다 순서가 달라진다.
 */
SW_TEST_CASE( StringTest, HashedStringFollowsFNameRules )
{
    const sw::hashed_string upper( "FNameRuleHero" );
    const sw::hashed_string lower( "fnamerulehero" );

    // 같음 · 해시는 대소문자를 무시한다(FName 의 ComparisonIndex)
    SW_EXPECT_TRUE( upper == lower );
    SW_EXPECT_EQUAL( upper.getIndex(), lower.getIndex() );
    SW_EXPECT_EQUAL( upper.getHash(), lower.getHash() );
    SW_EXPECT_EQUAL( sw::hashed_string::computeHash( "FNAMERULEHERO" ), lower.getHash() ); // 저장되는 해시는 철자와 무관하다

    // 표시는 적은 철자 그대로다(FName 의 DisplayIndex)
    SW_EXPECT_STREQ( "FNameRuleHero", upper.c_str() );
    SW_EXPECT_STREQ( "fnamerulehero", lower.c_str() );
    SW_EXPECT_TRUE( upper.getDisplayIndex() != lower.getDisplayIndex() );
    SW_EXPECT_TRUE( upper.isEqual( lower ) );
    SW_EXPECT_FALSE( upper.isEqual( lower, sw::NameCase::CaseSensitive ) );
    SW_EXPECT_TRUE( upper.isEqual( sw::hashed_string( "FNameRuleHero" ), sw::NameCase::CaseSensitive ) );

    // 찾기만 하는 조회는 철자를 새로 넣지 않는다 — 있는 철자면 그것, 없으면 그 이름의 첫 철자
    SW_EXPECT_STREQ( "fnamerulehero", sw::hashed_string::findInterned( "fnamerulehero" ).c_str() );
    const uint32            countBefore = sw::hashed_string::getInternedCount();
    const sw::hashed_string found       = sw::hashed_string::findInterned( "FNAMERULEHERO" );
    SW_EXPECT_TRUE( found == upper );
    SW_EXPECT_STREQ( "FNameRuleHero", found.c_str() );
    SW_EXPECT_EQUAL( countBefore, sw::hashed_string::getInternedCount() );

    // 해시 맵 키 — 철자가 달라도 같은 칸
    sw::unordered_map<sw::hashed_string, int32> mapNameToValue;
    mapNameToValue[upper] = 1;
    mapNameToValue[lower] = 2;
    SW_EXPECT_EQUAL( size_t( 1 ), mapNameToValue.size() );

    // 순서는 고른다 — 사전순(대소문자 무시)은 실행과 무관하고, 같은 이름은 어느 쪽도 앞이 아니다
    sw::vector<sw::hashed_string> listName{ sw::hashed_string( "FNameRuleCharlie" ), sw::hashed_string( "fnameRuleAlpha" ),
                                            sw::hashed_string( "FNAMERULEBRAVO" ) };
    std::sort( listName.begin(), listName.end(), sw::HashedStringLexicalLess{} );
    SW_EXPECT_STREQ( "fnameRuleAlpha", listName[0].c_str() );
    SW_EXPECT_STREQ( "FNAMERULEBRAVO", listName[1].c_str() );
    SW_EXPECT_STREQ( "FNameRuleCharlie", listName[2].c_str() );
    SW_EXPECT_FALSE( upper.lexicalLess( lower ) );
    SW_EXPECT_FALSE( lower.lexicalLess( upper ) );
    SW_EXPECT_FALSE( upper.fastLess( lower ) );
    SW_EXPECT_FALSE( lower.fastLess( upper ) );
}

/**
 * @brief [StringTest] string_splitter
 */
SW_TEST_CASE( StringTest, StringSplitter )
{
    // 1) 기존 initializer_list 호환성
    {
        sw::string_splitter                 splitter( "one|two|three", { "|" } );
        const sw::vector<std::string_view>& tokens = splitter.getSplitList();

        SW_EXPECT_EQUAL( 3u, splitter.getCount() );
        SW_EXPECT_FALSE( splitter.empty() );
        if ( tokens.size() == 3 )
        {
            SW_EXPECT_EQUAL( std::string_view( "one" ), tokens[0] );
            SW_EXPECT_EQUAL( std::string_view( "two" ), tokens[1] );
            SW_EXPECT_EQUAL( std::string_view( "three" ), tokens[2] );
        }
    }

    // 2) 단일 문자(char) SIMD 패스트 패스 생성자 및 인덱싱
    {
        sw::string_splitter splitter( "apple/banana/cherry", '/' );
        SW_EXPECT_EQUAL( 3u, splitter.getCount() );
        SW_EXPECT_EQUAL( std::string_view( "apple" ), splitter[0] );
        SW_EXPECT_EQUAL( std::string_view( "banana" ), splitter[1] );
        SW_EXPECT_EQUAL( std::string_view( "cherry" ), splitter[2] );
    }

    // 3) 단일 문자열 뷰(string_view) 생성자
    {
        sw::string_splitter splitter( "root::sub::leaf", "::" );
        SW_EXPECT_EQUAL( 3u, splitter.getCount() );
        SW_EXPECT_EQUAL( std::string_view( "root" ), splitter[0] );
        SW_EXPECT_EQUAL( std::string_view( "sub" ), splitter[1] );
        SW_EXPECT_EQUAL( std::string_view( "leaf" ), splitter[2] );
    }

    // 4) 다중 단일 문자 구분자 find_first_of 패스트 패스
    {
        sw::string_splitter splitter( "folder/sub\\file.txt", { "/", "\\" } );
        SW_EXPECT_EQUAL( 3u, splitter.getCount() );
        SW_EXPECT_EQUAL( std::string_view( "folder" ), splitter[0] );
        SW_EXPECT_EQUAL( std::string_view( "sub" ), splitter[1] );
        SW_EXPECT_EQUAL( std::string_view( "file.txt" ), splitter[2] );
    }

    // 5) Range-based for 루프 순회 지원
    {
        sw::string_splitter          splitter( "x,y,z", ',' );
        sw::vector<std::string_view> listResult;
        for ( const std::string_view token : splitter )
            listResult.push_back( token );

        SW_EXPECT_EQUAL( 3u, static_cast<uint32>( listResult.size() ) );
        if ( listResult.size() == 3 )
        {
            SW_EXPECT_EQUAL( std::string_view( "x" ), listResult[0] );
            SW_EXPECT_EQUAL( std::string_view( "y" ), listResult[1] );
            SW_EXPECT_EQUAL( std::string_view( "z" ), listResult[2] );
        }
    }

    // 6) 이터레이터 자체의 인덱스(it.getIndex()) 및 오프셋(it.getOffset()) 제어
    {
        sw::string_splitter    splitter( "10;20;30", ';' );
        size_t                 expectedIndex     = 0;
        const std::string_view expectedTokens[]  = { "10", "20", "30" };
        const size_t           expectedOffsets[] = { 0, 3, 6 };

        for ( auto it = splitter.begin(); it != splitter.end(); ++it )
        {
            SW_EXPECT_EQUAL( expectedIndex, it.getIndex() );
            SW_EXPECT_EQUAL( expectedOffsets[expectedIndex], it.getOffset() );
            SW_EXPECT_EQUAL( expectedTokens[expectedIndex], *it );
            ++expectedIndex;
        }
        SW_EXPECT_EQUAL( 3u, expectedIndex );
    }

    // 7) 독립 string_split_iterator 순회 (힙 할당 0회)
    {
        sw::string_split_iterator it( "red,green,blue", ',' );
        sw::string_split_iterator itEnd;

        SW_EXPECT_EQUAL( 0u, it.getIndex() );
        SW_EXPECT_EQUAL( std::string_view( "red" ), *it );
        ++it;
        SW_EXPECT_EQUAL( 1u, it.getIndex() );
        SW_EXPECT_EQUAL( std::string_view( "green" ), *it );
        ++it;
        SW_EXPECT_EQUAL( 2u, it.getIndex() );
        SW_EXPECT_EQUAL( std::string_view( "blue" ), *it );
        ++it;
        SW_EXPECT_TRUE( it == itEnd );
    }

    // 8) 빈 문자열 처리
    {
        sw::string_splitter emptySplitter( "", '/' );
        SW_EXPECT_EQUAL( 0u, emptySplitter.getCount() );
        SW_EXPECT_TRUE( emptySplitter.empty() );
    }
}

/**
 * @brief [StringTest] fixed_string 동작
 */
SW_TEST_CASE( StringTest, FixedStringOperations )
{
    sw::fixed_string<64> fs( "Hello" );
    SW_EXPECT_EQUAL( 5u, fs.size() );
    SW_EXPECT_FALSE( fs.empty() );
    SW_EXPECT_EQUAL( sw::string( "Hello" ), sw::string( fs.c_str() ) );

    fs.append( " World" );
    SW_EXPECT_EQUAL( sw::string( "Hello World" ), sw::string( fs.c_str() ) );

    fs.push_back( '!' );
    SW_EXPECT_EQUAL( sw::string( "Hello World!" ), sw::string( fs.c_str() ) );

    sw::fixed_string<64> sub = fs.substr( 0, 5 );
    SW_EXPECT_EQUAL( sw::string( "Hello" ), sw::string( sub.c_str() ) );

    uint32 foundPos = fs.find( "World" );
    SW_EXPECT_EQUAL( 6u, foundPos );

    fs.erase( 5, 6 );
    SW_EXPECT_EQUAL( sw::string( "Hello!" ), sw::string( fs.c_str() ) );
}

/**
 * @brief [StringTest] fixed_string 전체 커버리지
 */
SW_TEST_CASE( StringTest, FixedStringFullCoverage )
{
    sw::fixed_string<32> fs( "Engine" );
    SW_EXPECT_EQUAL( 'E', fs.front() );
    SW_EXPECT_EQUAL( 'e', fs.back() );
    SW_EXPECT_EQUAL( 'g', fs[2] );
    SW_EXPECT_EQUAL( 'n', fs.at( 1 ) );

    fs.insert( 0, "Core" );
    SW_EXPECT_EQUAL( sw::string( "CoreEngine" ), sw::string( fs.c_str() ) );

    fs.pop_back();
    SW_EXPECT_EQUAL( sw::string( "CoreEngin" ), sw::string( fs.c_str() ) );

    fs += "e";
    SW_EXPECT_EQUAL( sw::string( "CoreEngine" ), sw::string( fs.c_str() ) );

    sw::fixed_string<32> fs2( "CoreEngine" );
    sw::fixed_string<32> fs3( "OtherEngine" );

    SW_EXPECT_EQUAL( 0, fs.compare( fs2 ) );
    SW_EXPECT_TRUE( fs.compare( fs3 ) != 0 );

    sw::fixed_wstring<32> wfs( L"WideString" );
    SW_EXPECT_EQUAL( 10u, wfs.size() );
    SW_EXPECT_FALSE( wfs.empty() );
}

/**
 * @brief [StringTest] 용량을 넘는 입력은 잘리고 버퍼 밖은 건드리지 않는다
 * @details 단정으로 알리기만 하고 **원래 길이 그대로 복사**하면 `_arrData` 뒤(여기서는 `_canary`)를
 *          덮어쓴다. 단정은 실행을 멈추지 않고 Shipping 에서는 사라지므로 그대로 스택 오버플로가 된다.
 *          이 테스트는 잘리는지(size)와 이웃을 안 건드리는지(canary)를 함께 본다.
 */
SW_TEST_CASE( StringTest, FixedStringTruncatesInsteadOfOverflowing )
{
    SW_TEST_DEFENSIVE_SCOPE( "Testing fixed_string capacity overflow truncation" );

    /** @brief 문자열 바로 뒤에 감시값을 두어 버퍼 밖 쓰기를 잡는다. */
    struct Guarded
    {
        sw::fixed_string<8> _text;
        uint64              _canary;
    };

    static constexpr uint64 kCanary = 0xA5A5A5A5A5A5A5A5ull;
    const sw::string        longText( 64, 'x' );

    // 1) C 문자열 대입
    {
        Guarded guarded{};
        guarded._canary = kCanary;
        guarded._text   = longText.c_str();

        SW_EXPECT_EQUAL( 8u, guarded._text.size() );
        SW_EXPECT_EQUAL( sw::string( "xxxxxxxx" ), sw::string( guarded._text.c_str() ) );
        SW_EXPECT_EQUAL( kCanary, guarded._canary );
    }

    // 2) std::basic_string 대입
    {
        Guarded guarded{};
        guarded._canary = kCanary;
        guarded._text   = longText;

        SW_EXPECT_EQUAL( 8u, guarded._text.size() );
        SW_EXPECT_EQUAL( kCanary, guarded._canary );
    }

    // 3) 뒤에 붙이기 — 남은 자리만큼만 들어간다
    {
        Guarded guarded{};
        guarded._canary = kCanary;
        guarded._text   = "abc";
        guarded._text.append( "0123456789" );

        SW_EXPECT_EQUAL( 8u, guarded._text.size() );
        SW_EXPECT_EQUAL( sw::string( "abc01234" ), sw::string( guarded._text.c_str() ) );
        SW_EXPECT_EQUAL( kCanary, guarded._canary );
    }

    // 4) 꽉 찬 뒤의 push_back 은 버려진다 (_arrData[N + 1] 을 쓰면 안 된다)
    {
        Guarded guarded{};
        guarded._canary = kCanary;
        guarded._text   = "01234567";
        guarded._text.push_back( '!' );

        SW_EXPECT_EQUAL( 8u, guarded._text.size() );
        SW_EXPECT_EQUAL( sw::string( "01234567" ), sw::string( guarded._text.c_str() ) );
        SW_EXPECT_EQUAL( kCanary, guarded._canary );
    }

    // 5) 가운데 삽입 — 꼬리는 지키고 삽입분만 자른다
    {
        Guarded guarded{};
        guarded._canary = kCanary;
        guarded._text   = "abcd";
        guarded._text.insert( 2, "0123456789" );

        SW_EXPECT_EQUAL( 8u, guarded._text.size() );
        SW_EXPECT_EQUAL( sw::string( "ab0123cd" ), sw::string( guarded._text.c_str() ) );
        SW_EXPECT_EQUAL( kCanary, guarded._canary );
    }

    // 6) 자기 대입 — 두 경로 모두 자기 버퍼를 자기에게 복사하지 않는다
    {
        Guarded guarded{};
        guarded._canary = kCanary;
        guarded._text   = "abcd";

        guarded._text = guarded._text; // 같은 타입 대입: this != &rhs 가드
        SW_EXPECT_EQUAL( sw::string( "abcd" ), sw::string( guarded._text.c_str() ) );

        guarded._text = guarded._text.c_str(); // 포인터 별칭 대입: pStr == _arrData 가드
        SW_EXPECT_EQUAL( sw::string( "abcd" ), sw::string( guarded._text.c_str() ) );
        SW_EXPECT_EQUAL( 4u, guarded._text.size() );
        SW_EXPECT_EQUAL( kCanary, guarded._canary );
    }

    // 7) 문자 채우기 생성자 · 더 큰 용량에서 좁혀 담기
    {
        const sw::fixed_string<8>  filled( 64u, 'y' );
        const sw::fixed_string<64> big( longText.c_str() );
        const sw::fixed_string<8>  narrowed( big );

        SW_EXPECT_EQUAL( 8u, filled.size() );
        SW_EXPECT_EQUAL( sw::string( "yyyyyyyy" ), sw::string( filled.c_str() ) );
        SW_EXPECT_EQUAL( 64u, big.size() );
        SW_EXPECT_EQUAL( 8u, narrowed.size() );
        SW_EXPECT_EQUAL( sw::string( "xxxxxxxx" ), sw::string( narrowed.c_str() ) );
    }
}

/**
 * @brief [StringTest] 포맷 문자열 유틸
 */
SW_TEST_CASE( StringTest, FormatStringUtility )
{
    utf8 buffer[sw::constant::kMaxBuffer256] = {};

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "Value: %#", 42 );
    SW_EXPECT_EQUAL( sw::string( "Value: 42" ), sw::string( buffer ) );

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "Name: %#, Count: %#, Size: %#", "Subsystem", -10, 256u );
    SW_EXPECT_EQUAL( sw::string( "Name: Subsystem, Count: -10, Size: 256" ), sw::string( buffer ) );

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "Hex: 0x%#, HEX: 0x%#", sw::Fmt( 255, sw::Format().hex() ), sw::Fmt( 255, sw::Format().hexUpper() ) );
    SW_EXPECT_EQUAL( sw::string( "Hex: 0xff, HEX: 0xFF" ), sw::string( buffer ) );

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "Progress: %#%% complete", 100 );
    SW_EXPECT_EQUAL( sw::string( "Progress: 100% complete" ), sw::string( buffer ) );
}

// ------------------------------------------------------------------------------
// 2) StringBuilder — append/format·용량 성장
// ------------------------------------------------------------------------------
/**
 * @brief [StringTest] StringBuilder append/format
 */
SW_TEST_CASE( StringTest, StringBuilderAppendAndFormat )
{
    sw::StringBuilder<64> sb;
    sb.append( "Hello" );
    sb.append( ' ' );
    sb.append( 42 );
    SW_EXPECT_STREQ( "Hello 42", sb.c_str() );
    SW_EXPECT_EQUAL( 8u, sb.size() );

    sb.appendFormat( " x=%#", 7 );
    SW_EXPECT_STREQ( "Hello 42 x=7", sb.c_str() );

    sb.clear();
    SW_EXPECT_EQUAL( 0u, sb.size() );
    SW_EXPECT_STREQ( "", sb.c_str() );
    SW_EXPECT_EMPTY( sb.view() );
}

/**
 * @brief [StringTest] StringBuilder 정적 용량 초과 성장
 */
SW_TEST_CASE( StringTest, StringBuilderGrowsBeyondStaticCapacity )
{
    sw::StringBuilder<8> sb;
    const uint32         initialCapacity = sb.capacity();
    SW_EXPECT_EQUAL( 8u, initialCapacity );

    sb.append( "0123456789ABCDEF" );
    SW_EXPECT_TRUE_MSG( sb.capacity() > initialCapacity, "StringBuilder should grow when content exceeds static capacity" );
    SW_EXPECT_STREQ( "0123456789ABCDEF", sb.c_str() );
    SW_EXPECT_EQUAL( 16u, sb.size() );

    // 이동 생성자 검증 (힙 버퍼 이동)
    sw::StringBuilder<8> movedSb{ std::move( sb ) };
    SW_EXPECT_STREQ( "0123456789ABCDEF", movedSb.c_str() );
    SW_EXPECT_EQUAL( 16u, movedSb.size() );
}

/**
 * @brief [StringTest] StringBuilder 이동 생성 · 이동 대입이 스택 버퍼 · 힙 버퍼 모두에서 내용을 넘기고 원본을 빈 스택 상태로 되돌린다
 */
SW_TEST_CASE( StringTest, StringBuilderMoveTransfersStackAndHeapBuffers )
{
    // 스택 버퍼 → 이동 생성: 내용은 복사되고, 원본은 비고 다시 쓸 수 있다.
    sw::StringBuilder<8> stackSource;
    stackSource.append( "abc" );
    sw::StringBuilder<8> fromStack{ std::move( stackSource ) };
    SW_EXPECT_STREQ( "abc", fromStack.c_str() );
    SW_EXPECT_EQUAL( 3u, fromStack.size() );
    SW_EXPECT_EQUAL( 8u, fromStack.capacity() );
    SW_EXPECT_EQUAL( 0u, stackSource.size() );
    SW_EXPECT_STREQ( "", stackSource.c_str() );
    stackSource.append( "re" );
    SW_EXPECT_STREQ( "re", stackSource.c_str() );

    // 힙 버퍼를 가진 대상에 힙 버퍼를 이동 대입: 대상의 옛 힙 버퍼는 해제되고(누수 검사), 원본은 스택 용량으로 돌아간다.
    sw::StringBuilder<8> heapTarget;
    heapTarget.append( "target-heap-buffer" );
    sw::StringBuilder<8> heapSource;
    heapSource.append( "source-heap-buffer!" );
    const uint32 heapCapacity = heapSource.capacity();
    heapTarget                = std::move( heapSource );
    SW_EXPECT_STREQ( "source-heap-buffer!", heapTarget.c_str() );
    SW_EXPECT_EQUAL( 19u, heapTarget.size() );
    SW_EXPECT_EQUAL( heapCapacity, heapTarget.capacity() );
    SW_EXPECT_EQUAL( 8u, heapSource.capacity() );
    SW_EXPECT_EQUAL( 0u, heapSource.size() );
    SW_EXPECT_STREQ( "", heapSource.c_str() );

    // 힙 버퍼를 가진 대상에 스택 버퍼를 이동 대입: 대상은 자기 스택 버퍼로 돌아온다.
    sw::StringBuilder<8> smallSource;
    smallSource.append( "xy" );
    heapTarget = std::move( smallSource );
    SW_EXPECT_STREQ( "xy", heapTarget.c_str() );
    SW_EXPECT_EQUAL( 8u, heapTarget.capacity() );
    heapTarget.append( "z" );
    SW_EXPECT_STREQ( "xyz", heapTarget.c_str() );
    SW_EXPECT_EQUAL( 0u, smallSource.size() );
}

/**
 * @brief [StringTest] fixed_string string_view 및 hash 지원 검증
 */
SW_TEST_CASE( StringTest, FixedStringModernFeatures )
{
    std::string_view     sv = "ModernCpp";
    sw::fixed_string<32> fs{ sv };
    SW_EXPECT_STREQ( "ModernCpp", fs.c_str() );
    SW_EXPECT_EQUAL( 9u, fs.size() );
    SW_EXPECT_EQUAL( sv, fs.view() );

    SW_EXPECT_TRUE( fs.equals( "MODERNCPP", true ) );
    SW_EXPECT_FALSE( fs.equals( "MODERNCPP", false ) );
    SW_EXPECT_TRUE( fs.equals( sw::fixed_string<32>( "moderncpp" ), true ) );
    SW_EXPECT_FALSE( fs.equals( "Other", true ) );

    std::hash<sw::fixed_string<32>> hasher;
    size_t                          h1 = hasher( fs );
    size_t                          h2 = hasher( sw::fixed_string<32>( "ModernCpp" ) );
    SW_EXPECT_EQUAL( h1, h2 );

    // 대소문자는 구분한다 — operator== 와 같다.
    SW_EXPECT_NOT_EQUAL( hasher( sw::fixed_string<32>( "moderncpp" ) ), hasher( fs ) );
#if !defined( SW_ENABLE_STL_CONTAINER )
    // sw::string 과 같은 해시다(이종 조회가 같은 버킷을 본다). STL 구성에서는 std::hash<sw::string> 이 표준 것이라 다르다.
    SW_EXPECT_EQUAL( std::hash<sw::string>{}( sw::string( "ModernCpp" ) ), hasher( fs ) );
#endif
}

/**
 * @brief [StringTest] UTF-8 및 UTF-16 유니코드 상호 인코딩/디코딩 라운드트립 검증
 */
SW_TEST_CASE( StringTest, UnicodeConversionRoundTrip )
{
    // 한글, 이모지, 특수문자
    const utf8*       kOriginalUtf8 = "안녕하세요 Engine 🚀 (SW_Engine)";
    const sw::wstring utf16Str      = sw::StringUtil::utf8ToUtf16( kOriginalUtf8 );
    SW_EXPECT_FALSE( utf16Str.empty() );

    const sw::string roundTripUtf8 = sw::StringUtil::utf16ToUtf8( utf16Str.c_str() );
    SW_EXPECT_STREQ( kOriginalUtf8, roundTripUtf8.c_str() );
}

/**
 * @brief [StringTest] StringBuilder 복합 다중 appendFormat 및 무할당 성능 검증
 */
SW_TEST_CASE( StringTest, StringBuilderComplexFormatting )
{
    sw::StringBuilder<256> sb;
    sb.append( "Entity[" ).append( 42 ).append( "]: pos=(" );
    sb.append( 12.5f ).append( ", " ).append( -34.75f ).append( ")" );
    sb.append( "\n" );

    SW_EXPECT_STREQ( "Entity[42]: pos=(12.5, -34.75)\n", sb.c_str() );
}

/**
 * @brief [StringTest] StringUtil::isValidUtf8 종합 유효성 및 경계/오류 시퀀스 검증
 */
SW_TEST_CASE( StringTest, StringUtilUtf8Validation )
{
    // 1) Null 및 빈 문자열
    SW_EXPECT_FALSE( sw::StringUtil::isValidUtf8( nullptr ) );
    SW_EXPECT_TRUE( sw::StringUtil::isValidUtf8( "" ) );

    // 2) 순수 ASCII (8바이트 미만 및 8바이트 이상 SWAR 경로)
    SW_EXPECT_TRUE( sw::StringUtil::isValidUtf8( "A" ) );
    SW_EXPECT_TRUE( sw::StringUtil::isValidUtf8( "Short" ) );
    SW_EXPECT_TRUE( sw::StringUtil::isValidUtf8( "Exactly8" ) );
    SW_EXPECT_TRUE( sw::StringUtil::isValidUtf8( "This is a int32 ASCII sentence for SWAR fast path testing." ) );

    // 3) 유효한 2바이트, 3바이트, 4바이트 UTF-8
    SW_EXPECT_TRUE( sw::StringUtil::isValidUtf8( "\xC2\xA9" ) );               // © (U+00A9)
    SW_EXPECT_TRUE( sw::StringUtil::isValidUtf8( "\xC3\xA9" ) );               // é (U+00E9)
    SW_EXPECT_TRUE( sw::StringUtil::isValidUtf8( "\xE2\x82\xAC" ) );           // € (U+20AC)
    SW_EXPECT_TRUE( sw::StringUtil::isValidUtf8( "안녕하세요 엔진 테스트" ) ); // 한글 3바이트
    SW_EXPECT_TRUE( sw::StringUtil::isValidUtf8( "\xF0\x9F\x9A\x80" ) );       // 🚀 (U+1F680)
    SW_EXPECT_TRUE( sw::StringUtil::isValidUtf8( "\xF0\x9F\x98\x80" ) );       // 😀 (U+1F600)

    // 4) 불완전/잘린 시퀀스 (Truncated sequences)
    SW_EXPECT_FALSE( sw::StringUtil::isValidUtf8( "\xC2" ) );         // 2바이트 리드 바이트만 존재
    SW_EXPECT_FALSE( sw::StringUtil::isValidUtf8( "\xE2\x82" ) );     // 3바이트 중 2바이트만 존재
    SW_EXPECT_FALSE( sw::StringUtil::isValidUtf8( "\xF0\x9F\x9A" ) ); // 4바이트 중 3바이트만 존재

    // 5) 비정상 후속 바이트 (Invalid continuation bytes)
    SW_EXPECT_FALSE( sw::StringUtil::isValidUtf8( "\xC2\x20" ) );         // 후속 바이트가 공백 (0x20 != 0x80..0xBF)
    SW_EXPECT_FALSE( sw::StringUtil::isValidUtf8( "\xE2\x82\x20" ) );     // 3번째 바이트 비정상
    SW_EXPECT_FALSE( sw::StringUtil::isValidUtf8( "\xF0\x9F\x9A\xC0" ) ); // 4번째 바이트 비정상

    // 6) Overlong 인코딩 (보안 취약점 방지 검증)
    SW_EXPECT_FALSE( sw::StringUtil::isValidUtf8( "\xC0\xAF" ) );         // Overlong 2바이트 '/'
    SW_EXPECT_FALSE( sw::StringUtil::isValidUtf8( "\xC1\xBF" ) );         // Overlong 2바이트
    SW_EXPECT_FALSE( sw::StringUtil::isValidUtf8( "\xE0\x80\xAF" ) );     // Overlong 3바이트
    SW_EXPECT_FALSE( sw::StringUtil::isValidUtf8( "\xF0\x80\x80\xAF" ) ); // Overlong 4바이트

    // 7) UTF-16 Surrogate 영역 (U+D800 ~ U+DFFF 금지)
    SW_EXPECT_FALSE( sw::StringUtil::isValidUtf8( "\xED\xA0\x80" ) ); // U+D800
    SW_EXPECT_FALSE( sw::StringUtil::isValidUtf8( "\xED\xBF\xBF" ) ); // U+DFFF

    // 8) 최대 유니코드 초과 (> U+10FFFF)
    SW_EXPECT_FALSE( sw::StringUtil::isValidUtf8( "\xF4\x90\x80\x80" ) ); // U+110000
    SW_EXPECT_FALSE( sw::StringUtil::isValidUtf8( "\xF7\xBF\xBF\xBF" ) ); // 유효 범위 초과
}

/**
 * @brief [StringTest] StringUtil 해시 일관성, CRC32 및 공백 트림 유틸리티 검증
 */
SW_TEST_CASE( StringTest, StringUtilHashingAndTransform )
{
    // 64비트 / 32비트 FNV1a 해시 일관성
    const uint64 h64_1 = sw::StringUtil::computeHash64( std::string_view( "EngineResourcePath" ) );
    const uint64 h64_2 = sw::StringUtil::computeHash64( std::string_view( "EngineResourcePath" ) );
    const uint64 h64_3 = sw::StringUtil::computeHash64( std::string_view( "engineResourcePath" ), false );

    SW_EXPECT_EQUAL( h64_1, h64_2 );
    SW_EXPECT_TRUE( h64_1 != h64_3 );

    const uint32 h32_1 = sw::StringUtil::computeHash32( std::string_view( "EngineResourcePath" ) );
    const uint32 h32_2 = sw::StringUtil::computeHash32( std::string_view( "EngineResourcePath" ) );
    SW_EXPECT_EQUAL( h32_1, h32_2 );

    // CRC32 체크섬 계산 검증
    const uint32 crc1 = sw::StringUtil::computeCrc32( "123456789", 9 );
    const uint32 crc2 = sw::StringUtil::computeCrc32( "123456789", 9 );
    SW_EXPECT_EQUAL( 0xCBF43926u, crc1 ); // 표준 IEEE 802.3 CRC32("123456789") = 0xCBF43926
    SW_EXPECT_EQUAL( crc1, crc2 );

    // trimStart & trimEnd
    SW_EXPECT_EQUAL( sw::string( "Hello  " ), sw::StringUtil::trimStart( "  Hello  " ) );
    SW_EXPECT_EQUAL( sw::string( "  Hello" ), sw::StringUtil::trimEnd( "  Hello  " ) );
}

/**
 * @brief [StringTest] fixed_string 추가 고급 연산 (반복자, 비우기, 검색)
 */
SW_TEST_CASE( StringTest, FixedStringExtendedOperations )
{
    sw::fixed_string<32> str( "Antigravity" );

    // 범위 기반 for 루프 반복자 순회 검증
    size_t charCount{ 0 };
    for ( const utf8 ch : str )
    {
        if ( ch != '\0' )
            ++charCount;
    }
    SW_EXPECT_EQUAL( 11u, charCount );

    // find 및 substr
    SW_EXPECT_EQUAL( 0u, str.find( "Anti" ) );
    SW_EXPECT_EQUAL( 4u, str.find( "grav" ) );
    SW_EXPECT_TRUE( str.substr( 0, 4 ) == "Anti" );
    SW_EXPECT_TRUE( str.substr( 4 ) == "gravity" );
    SW_EXPECT_EQUAL( sw::fixed_string<32>::npos, str.find( "NotFound" ) );

    // clear
    str.clear();
    SW_EXPECT_TRUE( str.empty() );
    SW_EXPECT_EQUAL( 0u, str.size() );
    SW_EXPECT_STREQ( "", str.c_str() );
}

/**
 * @brief [StringTest] fixed_string formatstring 및 data() 수정 후 자동 sync_size 검증
 */
SW_TEST_CASE( StringTest, FixedStringFormatAndAutoSync )
{
    // 1) formatstring 동작 및 자동 길이 동기화
    sw::fixed_string<sw::constant::kMaxBuffer64> strFmt;
    sw::formatstring( strFmt.data(), strFmt.capacity(), "Item #%#: %# (%#)", 42, "Potion", sw::Fmt( 12.5, sw::Format( 2 ) ) );
    SW_EXPECT_FALSE( strFmt.empty() );
    SW_EXPECT_STREQ( "Item #42: Potion (12.50)", strFmt.c_str() );
    SW_EXPECT_EQUAL( static_cast<uint32>( strlen( "Item #42: Potion (12.50)" ) ), strFmt.size() );
    SW_EXPECT_EQUAL( strFmt.size(), strFmt.length() );
    SW_EXPECT_EQUAL( std::string_view( "Item #42: Potion (12.50)" ), strFmt.view() );

    // 2) data() 버퍼에 직접 C-API 스타일로 작성했을 때 자동 sync_size 동작
    sw::fixed_string<sw::constant::kMaxBuffer32> rawBuf;
    SW_EXPECT_TRUE( rawBuf.empty() );
    SW_EXPECT_EQUAL( 0u, rawBuf.size() );

    // data()에 strcpy (ImGui::InputText 동작 모사)
    sw::StringUtil::strncpy( rawBuf.data(), "HeroPlayer", rawBuf.capacity() );

    // sync_size()를 명시적으로 호출하지 않아도 empty(), size(), length(), view(), basic_string 변환 자동 동기화
    SW_EXPECT_FALSE( rawBuf.empty() );
    SW_EXPECT_EQUAL( 10u, rawBuf.size() );
    SW_EXPECT_EQUAL( 10u, rawBuf.length() );
    SW_EXPECT_STREQ( "HeroPlayer", rawBuf.c_str() );
    SW_EXPECT_EQUAL( std::string_view( "HeroPlayer" ), rawBuf.view() );

    const std::string stdStr = rawBuf;
    SW_EXPECT_EQUAL( std::string( "HeroPlayer" ), stdStr );
    const sw::string swStr{ rawBuf.c_str() };
    SW_EXPECT_EQUAL( sw::string( "HeroPlayer" ), swStr );

    // Range-based for loop 자동 동기화
    size_t iteratedCount = 0;
    for ( const utf8 ch : rawBuf )
    {
        if ( ch != '\0' )
            ++iteratedCount;
    }
    SW_EXPECT_EQUAL( 10u, iteratedCount );
}

/**
 * @brief [StringTest] FileUtil::skipUtf8Bom 및 BOM 포함 텍스트 파일 읽기 검증
 */
SW_TEST_CASE( StringTest, Utf8BomHandling )
{
    // 1) string_view 기반 BOM 스킵 검증
    const utf8* pWithBom    = "\xEF\xBB\xBFHello UTF-8 BOM!";
    const utf8* pWithoutBom = "Hello Without BOM!";

    SW_EXPECT_EQUAL( std::string_view( "Hello UTF-8 BOM!" ), sw::FileUtil::skipUtf8Bom( pWithBom ) );
    SW_EXPECT_EQUAL( std::string_view( pWithoutBom ), sw::FileUtil::skipUtf8Bom( pWithoutBom ) );

    // 2) 포인터 및 크기 기반 BOM 스킵 검증
    const uint8* pBytes = reinterpret_cast<const uint8*>( pWithBom );
    size_t       size   = strlen( pWithBom );
    sw::FileUtil::skipUtf8Bom( pBytes, size );
    SW_EXPECT_EQUAL( strlen( "Hello UTF-8 BOM!" ), size );
    SW_EXPECT_EQUAL( 'H', static_cast<utf8>( pBytes[0] ) );

    // 3) 파일 I/O 자동 BOM 제거 검증
    const sw::string bomFilePath = test::makeTempPath( "test_bom.txt" );
    SW_EXPECT_TRUE( sw::FileUtil::writeFile( bomFilePath, reinterpret_cast<const uint8*>( pWithBom ), strlen( pWithBom ) ) );

    sw::string readText;
    SW_EXPECT_TRUE( sw::FileUtil::readTextFile( bomFilePath, readText ) );
    SW_EXPECT_STREQ( "Hello UTF-8 BOM!", readText.c_str() );
}

/**
 * @brief [StringTest] fixed_string 다양한 생성자, 대입 및 assign 동작 검증
 */
SW_TEST_CASE( StringTest, FixedStringConstructorsAndAssignments )
{
    // 1) 기본 생성자
    sw::fixed_string<32> defaultStr;
    SW_EXPECT_TRUE( defaultStr.empty() );
    SW_EXPECT_EQUAL( 0u, defaultStr.size() );
    SW_EXPECT_EQUAL( 0u, defaultStr.length() );
    SW_EXPECT_EQUAL( 32u, defaultStr.capacity() );
    SW_EXPECT_EQUAL( 32u, defaultStr.max_size() );
    SW_EXPECT_STREQ( "", defaultStr.c_str() );

    // 2) nullptr 생성자 (안전하게 빈 문자열로 초기화)
    sw::fixed_string<32> nullStr( static_cast<const utf8*>( nullptr ) );
    SW_EXPECT_TRUE( nullStr.empty() );
    SW_EXPECT_EQUAL( 0u, nullStr.size() );

    // 3) 채우기(fill) 생성자
    sw::fixed_string<32> fillStr( 5u, 'X' );
    SW_EXPECT_FALSE( fillStr.empty() );
    SW_EXPECT_EQUAL( 5u, fillStr.size() );
    SW_EXPECT_STREQ( "XXXXX", fillStr.c_str() );

    // 4) std::string 및 std::string_view 생성자
    const std::string      stdStrSource = "FromStdString";
    const std::string_view svSource     = "FromView";
    sw::fixed_string<32>   fromStd( stdStrSource );
    sw::fixed_string<32>   fromSv( svSource );
    SW_EXPECT_STREQ( "FromStdString", fromStd.c_str() );
    SW_EXPECT_STREQ( "FromView", fromSv.c_str() );

    // 5) 복사 생성자 및 이동 생성자
    sw::fixed_string<32> copyStr( fromStd );
    SW_EXPECT_STREQ( "FromStdString", copyStr.c_str() );
    sw::fixed_string<32> moveStr( std::move( copyStr ) );
    SW_EXPECT_STREQ( "FromStdString", moveStr.c_str() );

    // 6) 대입 연산자들 (const char*, nullptr, std::string, std::string_view, fixed_string)
    sw::fixed_string<32> assignTarget;
    assignTarget = "AssignedCStr";
    SW_EXPECT_STREQ( "AssignedCStr", assignTarget.c_str() );

    assignTarget = static_cast<const utf8*>( nullptr );
    SW_EXPECT_TRUE( assignTarget.empty() );
    SW_EXPECT_STREQ( "", assignTarget.c_str() );

    assignTarget = stdStrSource;
    SW_EXPECT_STREQ( "FromStdString", assignTarget.c_str() );

    assignTarget = svSource;
    SW_EXPECT_STREQ( "FromView", assignTarget.c_str() );

    assignTarget = fillStr;
    SW_EXPECT_STREQ( "XXXXX", assignTarget.c_str() );

    // 7) assign() 멤버 함수들
    assignTarget.assign( "NewAssign" );
    SW_EXPECT_STREQ( "NewAssign", assignTarget.c_str() );
    assignTarget.assign( fromStd );
    SW_EXPECT_STREQ( "FromStdString", assignTarget.c_str() );
}

/**
 * @brief [StringTest] fixed_string 비교 및 연산자 (==, !=, <, <=, >, >=, +, +=, <<, >>)
 */
SW_TEST_CASE( StringTest, FixedStringComparisonAndOperators )
{
    sw::fixed_string<32> strA( "Alpha" );
    sw::fixed_string<32> strA2( "Alpha" );
    sw::fixed_string<32> strB( "Beta" );

    // 비교 연산자 (fixed_string vs fixed_string)
    SW_EXPECT_TRUE( strA == strA2 );
    SW_EXPECT_FALSE( strA != strA2 );
    SW_EXPECT_TRUE( strA != strB );
    SW_EXPECT_TRUE( strA < strB );
    SW_EXPECT_TRUE( strA <= strB );
    SW_EXPECT_TRUE( strA <= strA2 );
    SW_EXPECT_TRUE( strB > strA );
    SW_EXPECT_TRUE( strB >= strA );
    SW_EXPECT_TRUE( strA2 >= strA );

    // 비교 연산자 (fixed_string vs const char*)
    SW_EXPECT_TRUE( strA == "Alpha" );
    SW_EXPECT_FALSE( strA == "Beta" );
    SW_EXPECT_TRUE( strA != "Beta" );
    SW_EXPECT_FALSE( strA != "Alpha" );

    // operator+ 및 operator+=
    sw::fixed_string<64> sum1 = strA + strB;
    SW_EXPECT_STREQ( "AlphaBeta", sum1.c_str() );

    sw::fixed_string<64> sum2 = strA + "Gamma";
    SW_EXPECT_STREQ( "AlphaGamma", sum2.c_str() );

    sw::fixed_string<64> sum3 = "Prefix" + strA;
    SW_EXPECT_STREQ( "PrefixAlpha", sum3.c_str() );

    sw::fixed_string<64> mutStr( "Base" );
    mutStr += "_";
    mutStr += strA;
    mutStr += '!';
    SW_EXPECT_STREQ( "Base_Alpha!", mutStr.c_str() );

    // stream << 및 >> 연산자
    std::ostringstream outStream;
    outStream << strA;
    SW_EXPECT_EQUAL( std::string( "Alpha" ), outStream.str() );

    std::istringstream   inStream( "StreamedContent" );
    sw::fixed_string<32> streamTarget;
    inStream >> streamTarget;
    SW_EXPECT_STREQ( "StreamedContent", streamTarget.c_str() );
}

/**
 * @brief [StringTest] fixed_string 최대 용량(N) 경계 조건 및 널 종단 무결성
 */
SW_TEST_CASE( StringTest, FixedStringBoundaryAndMaxCapacity )
{
    // 정확히 용량 16 문자를 채웠을 때 검증
    sw::fixed_string<16> maxStr( "0123456789ABCDEF" );
    SW_EXPECT_EQUAL( 16u, maxStr.size() );
    SW_EXPECT_EQUAL( 16u, maxStr.capacity() );
    SW_EXPECT_EQUAL( 16u, maxStr.max_size() );
    SW_EXPECT_STREQ( "0123456789ABCDEF", maxStr.c_str() );
    SW_EXPECT_EQUAL( '\0', maxStr.data()[16] ); // 16번 인덱스는 항상 널 종단

    // formatstring으로 용량(16) 내 작성 시 안전한 널 종단(15자 + '\0') 보장
    sw::fixed_string<16> fmtMax;
    sw::formatstring( fmtMax.data(), fmtMax.capacity(), "%#", "0123456789ABCDEF" );
    SW_EXPECT_EQUAL( 15u, fmtMax.size() );
    SW_EXPECT_STREQ( "0123456789ABCDE", fmtMax.c_str() );
    SW_EXPECT_EQUAL( '\0', fmtMax.data()[15] );
}

/**
 * @brief [StringTest] fixed_string data() 버퍼 직접 변경 후 컨테이너 연산(insert, erase, append 등) 통합 검증
 */
SW_TEST_CASE( StringTest, FixedStringDirectMutationAndContainerOps )
{
    sw::fixed_string<64> str;
    // 1) data()로 직접 기록
    sw::StringUtil::strncpy( str.data(), "Player", str.capacity() );
    SW_EXPECT_EQUAL( 6u, str.size() );
    SW_EXPECT_STREQ( "Player", str.c_str() );

    // 2) data() 수정 후 push_back
    str.push_back( '1' );
    SW_EXPECT_EQUAL( 7u, str.size() );
    SW_EXPECT_STREQ( "Player1", str.c_str() );

    // 3) data() 수정 후 append
    str.append( "_Knight" );
    SW_EXPECT_EQUAL( 14u, str.size() );
    SW_EXPECT_STREQ( "Player1_Knight", str.c_str() );

    // 4) data() 수정 후 insert
    str.insert( 0, "Hero_" );
    SW_EXPECT_EQUAL( 19u, str.size() );
    SW_EXPECT_STREQ( "Hero_Player1_Knight", str.c_str() );

    // 5) data() 수정 후 erase
    str.erase( 0, 5 ); // "Hero_" 제거
    SW_EXPECT_EQUAL( 14u, str.size() );
    SW_EXPECT_STREQ( "Player1_Knight", str.c_str() );

    // 6) data() 수정 후 find 및 substr
    SW_EXPECT_EQUAL( 8u, str.find( "Knight" ) );
    sw::fixed_string<64> sub = str.substr( 8, 6 );
    SW_EXPECT_STREQ( "Knight", sub.c_str() );

    // 7) data() 수정 후 pop_back
    str.pop_back(); // 't' 제거
    SW_EXPECT_EQUAL( 13u, str.size() );
    SW_EXPECT_STREQ( "Player1_Knigh", str.c_str() );
}

/**
 * @brief [StringTest] fixed_wstring (UTF-16) 광범위 동작 검증
 */
SW_TEST_CASE( StringTest, FixedWStringOperations )
{
    sw::fixed_wstring<32> wstr( L"UnicodeString" );
    SW_EXPECT_FALSE( wstr.empty() );
    SW_EXPECT_EQUAL( 13u, wstr.size() );
    SW_EXPECT_EQUAL( 32u, wstr.capacity() );
    SW_EXPECT_TRUE( wstr.view() == std::wstring_view( L"UnicodeString" ) );

    wstr.append( L"_W" );
    SW_EXPECT_EQUAL( 15u, wstr.size() );

    wstr.push_back( L'!' );
    SW_EXPECT_EQUAL( 16u, wstr.size() );

    wstr.insert( 0, L"Pre_" );
    SW_EXPECT_EQUAL( 20u, wstr.size() );

    wstr.erase( 0, 4 );
    SW_EXPECT_EQUAL( 16u, wstr.size() );

    SW_EXPECT_EQUAL( 0u, wstr.find( L"Unicode" ) );
    sw::fixed_wstring<32> sub = wstr.substr( 0, 7 );
    SW_EXPECT_EQUAL( 7u, sub.size() );
    SW_EXPECT_TRUE( sub == sw::fixed_wstring<32>( L"Unicode" ) );

    // std::hash 특수화 검증
    std::hash<sw::fixed_wstring<32>> hasher;
    size_t                           h1 = hasher( wstr );
    size_t                           h2 = hasher( sw::fixed_wstring<32>( wstr.c_str() ) );
    SW_EXPECT_EQUAL( h1, h2 );

    wstr.clear();
    SW_EXPECT_TRUE( wstr.empty() );
    SW_EXPECT_EQUAL( 0u, wstr.size() );
}

/**
 * @brief [StringTest] fixed_string 의 std::unordered_map 및 std::unordered_set 연동 검증
 */
SW_TEST_CASE( StringTest, FixedStringUnorderedContainers )
{
    // 1) std::unordered_set
    std::unordered_set<sw::fixed_string<32>> uniqueSet;
    uniqueSet.insert( sw::fixed_string<32>( "Entity_A" ) );
    uniqueSet.insert( sw::fixed_string<32>( "Entity_B" ) );
    uniqueSet.insert( sw::fixed_string<32>( "Entity_A" ) ); // 중복

    SW_EXPECT_EQUAL( 2u, uniqueSet.size() );
    SW_EXPECT_TRUE( uniqueSet.find( sw::fixed_string<32>( "Entity_A" ) ) != uniqueSet.end() );
    SW_EXPECT_TRUE( uniqueSet.find( sw::fixed_string<32>( "Entity_C" ) ) == uniqueSet.end() );

    // 2) std::unordered_map
    std::unordered_map<sw::fixed_string<32>, int32> mapScore;
    mapScore[sw::fixed_string<32>( "Player1" )] = 100;
    mapScore[sw::fixed_string<32>( "Player2" )] = 250;

    SW_EXPECT_EQUAL( 2u, mapScore.size() );
    SW_EXPECT_EQUAL( 100, mapScore[sw::fixed_string<32>( "Player1" )] );
    SW_EXPECT_EQUAL( 250, mapScore[sw::fixed_string<32>( "Player2" )] );
}

/**
 * @brief [StringTest] FormatString floatToString 및 폴백 널 종단 문자 보장 검증
 */
SW_TEST_CASE( StringTest, FormatStringFloatFallbackNullTerminator )
{
    utf8 buf[sw::constant::kMaxBuffer64]{ 0 };
    sw::formatstring( buf, sizeof( buf ), "Value: %#", 123.456f );
    sw::string formattedFloat( buf );
    SW_EXPECT_FALSE( formattedFloat.empty() );
    SW_EXPECT_TRUE( formattedFloat.find( "123.45" ) != sw::string::npos );
    SW_EXPECT_EQUAL( '\0', buf[formattedFloat.size()] );
}

/**
 * @brief [StringTest] StringBuilder 경계 크기 appendFormat 포맷팅 및 재할당 안전성 검증
 */
SW_TEST_CASE( StringTest, StringBuilderBoundaryAvailableMinusOne )
{
    sw::StringBuilder<256> builder;
    builder.append( "1234567890" );
    builder.appendFormat( "_%#_%#", 100, 200 );
    SW_EXPECT_EQUAL( sw::string( "1234567890_100_200" ), sw::string( builder.c_str() ) );
}

/**
 * @brief [StringTest] basic_fixed_string C 문자열 좌측 덧셈 연산자 및 O(1) size() 일관성 검증
 */
SW_TEST_CASE( StringTest, FixedStringOperatorPlusWithCStringLhsAndSizeO1 )
{
    sw::fixed_string<32> rhs( "World" );
    SW_EXPECT_EQUAL( 5u, rhs.size() );

    // C 문자열 좌측 덧셈: "Hello " + rhs
    auto combined = "Hello " + rhs;
    SW_EXPECT_EQUAL( sw::string( "Hello World" ), sw::string( combined.c_str() ) );
    SW_EXPECT_EQUAL( 11u, combined.size() );
}

/**
 * @brief [StringTest] 표준 서식 지정자(정밀도·너비·플래그)를 formatstring 이 이해하는지
 * @details `%#` 과 맨 변환 문자(`%d`, `%f`)만 알아보면 `%.3f` 가 `%.` 까지만 플레이스홀더로 먹히고
 *          `3f` 가 글자로 남아 **조용히 틀린 출력**이 나온다.
 */
SW_TEST_CASE( StringTest, FormatStringSupportsPrintfSpecifiers )
{
    utf8 buffer[128]{};

    // 정밀도 — `3f` 가 글자로 남으면 진다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%.3f", 3.14159f );
    SW_EXPECT_STREQ( "3.142", buffer );

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%.0f", 2.7f );
    SW_EXPECT_STREQ( "3", buffer );

    // 너비 — 오른쪽 정렬이 기본.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "[%5d]", 42 );
    SW_EXPECT_STREQ( "[   42]", buffer );

    // 왼쪽 정렬 플래그.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "[%-5d]", 42 );
    SW_EXPECT_STREQ( "[42   ]", buffer );

    // 0 채우기.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "[%05d]", 42 );
    SW_EXPECT_STREQ( "[00042]", buffer );

    // 부호 표시.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%+d", 42 );
    SW_EXPECT_STREQ( "+42", buffer );

    // 너비 + 정밀도 조합.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "[%8.2f]", 3.14159f );
    SW_EXPECT_STREQ( "[    3.14]", buffer );

    // 길이 수식어는 읽고 버린다 — 값의 타입은 인자가 정한다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%llu", static_cast<uint64>( 1234567890123ull ) );
    SW_EXPECT_STREQ( "1234567890123", buffer );

    // 16진수는 서식 앞에 옵션이 붙어도 유지된다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%08x", 255u );
    SW_EXPECT_STREQ( "000000ff", buffer );

    // 기존 %# 은 그대로 동작해야 한다 (하위호환).
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "Value: %#", 42 );
    SW_EXPECT_STREQ( "Value: 42", buffer );

    // %% 는 리터럴 퍼센트.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%d%% done", 100 );
    SW_EXPECT_STREQ( "100% done", buffer );
}

/**
 * @brief [StringTest] 서식이 붙은 긴 문자열이 임시 버퍼 크기에서 잘리지 않는지
 * @details 값은 스택 임시 버퍼(kTempBufferSize)를 거쳐 문자열이 된다. 문자열 인자가 그 버퍼를 건너뛰는
 *          지름길이 **서식 없는 경로에만** 있으면 `%s` 는 멀쩡한데 `%-20s` 처럼 폭을 주는 순간 버퍼 크기에서
 *          잘린다 — 로그에서 긴 메시지의 꼬리가 조용히 사라진다.
 */
SW_TEST_CASE( StringTest, FormatStringLongTextSurvivesWidthSpec )
{
    // 임시 버퍼(kTempBufferSize)보다 긴 문자열이어야 한다.
    sw::string longText;
    longText.reserve( 600 );
    for ( uint32 index = 0; index < 60; ++index )
        longText += "0123456789";

    utf8 buffer[1024]{};

    // 폭 지정이 붙어도 전체가 나와야 한다 (폭보다 길면 패딩은 없다).
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%-20s", longText.c_str() );
    SW_EXPECT_EQUAL( longText.size(), sw::StringUtil::strlen( buffer ) );
    SW_EXPECT_STREQ( longText.c_str(), buffer );

    // 서식 없는 경로도 그대로여야 한다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%#", longText.c_str() );
    SW_EXPECT_EQUAL( longText.size(), sw::StringUtil::strlen( buffer ) );

    // 짧은 문자열은 폭 맞춤이 실제로 적용된다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "[%-6s]", "ab" );
    SW_EXPECT_STREQ( "[ab    ]", buffer );
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "[%6s]", "ab" );
    SW_EXPECT_STREQ( "[    ab]", buffer );
}

/**
 * @brief [StringTest] `%#` 은 옵션이 붙지 않는 순수 자리표다 — 뒤 글자는 무조건 리터럴, 서식은 printf 형으로.
 * @details `#` 뒤를 서식으로 읽으면 `%#x%#`(가로x세로)가 가로를 16진수로 찍고(1280 → 500), `%#s`(초) 가
 *          단위 `s` 를 삼키고, `%#.txt` 가 `.tx` 를 잃어 로그 파일이 `.txt` 없이 남는다.
 */
SW_TEST_CASE( StringTest, PlaceholderNeverTakesSpecifiers )
{
    utf8 buffer[128]{};

    // `%#` 뒤의 'x' 는 리터럴이다 — 치수 로그의 "가로x세로".
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%#x%#", 1280, 720 );
    SW_EXPECT_STREQ( "1280x720", buffer );

    // 단위를 붙여 쓴 로그 — 'd', 's', 't' 가 서식으로 읽히지 않는다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%#dB %#s %#time", 3, 2.5f, 7 );
    SW_EXPECT_STREQ( "3dB 2.500000s 7time", buffer );

    // 서식은 printf 형으로 — 너비·정렬·0채움·16진수·정밀도·부호. 타입은 인자가 정한다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "[%3d|%-5s|%08x|%.2f|%+d]", 7, "w", 255, 3.14159, 5 );
    SW_EXPECT_STREQ( "[  7|w    |000000ff|3.14|+5]", buffer );

    // 공백은 플래그로 받지 않는다 — 뒤 단어의 첫 글자를 먹지 않아야 한다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%# x %#", 1280, 720 );
    SW_EXPECT_STREQ( "1280 x 720", buffer );

    // 알아볼 수 없는 `%…` 는 리터럴이고 인자를 소비하지 않는다 — 뒤 인자가 밀리지 않아야 한다. `%%` 는 퍼센트.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "100% done, %q=%#, %%=%#, tail%", 1, 2 );
    SW_EXPECT_STREQ( "100% done, %q=1, %=2, tail%", buffer );

    // 인자 수 불일치는 Debug 에서 포맷을 훑는 자리의 단언(디버그 브레이크)이 잡는다 — 그래서 여기서 재현할 수 없다.
    // 세는 규칙만 고정한다: `%#`·printf 서식은 1, `%%`·모르는 `%…` 는 0. 리터럴은 컴파일 시점에도 셀 수 있다.
    static_assert( sw::FormatString::countPlaceholders( "a=%# b=%3d c=%.2f %% %q tail%" ) == 3, "%#, %3d, %.2f 만 자리표다" );
    static_assert( sw::FormatString::countPlaceholders( "no placeholders" ) == 0 );
    static_assert( sw::FormatString::countPlaceholders( "%#x%# %#s %#.txt" ) == 4 );
    SW_EXPECT_EQUAL( 2u, sw::FormatString::countPlaceholders( sw::string( "%#-%#" ) ) );

    // 널 포인터는 종류와 무관하게 (null) — `nullptr` 리터럴과 널 `const utf8*` 가 string_view 지름길을 타면
    // strlen(nullptr) 로 죽는다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%# %# %# %-8s|", nullptr, static_cast<const void*>( nullptr ),
                      static_cast<const utf8*>( nullptr ), static_cast<const utf8*>( nullptr ) );
    SW_EXPECT_STREQ( "(null) (null) (null) (null)  |", buffer );

    // printf 서식 + Fmt 값 — 변환은 Fmt 의 서식(16진수), 너비는 서식 문자열. "[unsupported type]" 이 나오면 진다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "[%6d]", sw::Fmt( 255, sw::Format().hex() ) );
    SW_EXPECT_STREQ( "[    ff]", buffer );

    // 로그에 쓰이는 문장들 — 첫 글자가 변환 문자인 단어가 뒤에 온다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%# Passed, %# Failed", 3, 0 );
    SW_EXPECT_STREQ( "3 Passed, 0 Failed", buffer );

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%# of %# slots", 5, 8 );
    SW_EXPECT_STREQ( "5 of 8 slots", buffer );

    // 변환 문자도 플래그도 아닌 구분자는 리터럴로 남는다 — 치수 로그는 이 형태를 쓴다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%#Ã%#", 1280, 720 );
    SW_EXPECT_STREQ( "1280Ã720", buffer );

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "(%#, %#)", 1280, 720 );
    SW_EXPECT_STREQ( "(1280, 720)", buffer );

    // 의도된 16진수는 printf 형으로 쓴다.
    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "crc=%x", 255 );
    SW_EXPECT_STREQ( "crc=ff", buffer );
}

/**
 * @brief [StringTest] stristr 은 널 종단자를 지나 읽지 않는다
 * @details `stristr` 은 `string_view( pStr, subLen )` 를 만들어 비교한다 — 남은 문자열이 검색어보다
 *          짧아도 길이를 `subLen` 으로 잡으므로, 읽어 보면 종단자 뒤로 넘어갈 것처럼 생겼다.
 *          **실제로는 넘어가지 않는다.** `equals` 의 비교 루프가 첫 불일치에서 끊기고, 널 종단자는
 *          검색어의 어떤 문자와도(검색어에는 널이 없다) 반드시 불일치하기 때문이다. 즉 안전한
 *          이유가 구현 세부(단축 평가)에 걸려 있다.
 *
 *          그 세부가 깨지면 바로 경계 초과 읽기가 된다 — 예컨대 비교를 한 번에 여러 바이트씩
 *          처리하도록 "최적화" 하는 순간이다. 그래서 가드 페이지로 고정한다: 문자열을 페이지
 *          마지막 바이트에 붙여 놓고 다음 페이지를 접근 불가로 만들면, 한 바이트라도 넘어가는
 *          순간 죽는다. 에디터 검색 필드가 이 경로를 매 프레임 탄다(필드보다 긴 검색어).
 */
SW_TEST_CASE( StringTest, StristrStopsAtTerminator )
{
#if defined( SW_PLATFORM_WINDOWS )
    SYSTEM_INFO sysInfo{};
    GetSystemInfo( &sysInfo );
    const size_t pageSize = static_cast<size_t>( sysInfo.dwPageSize );

    // 두 페이지를 잡고 뒤 페이지를 접근 불가로 바꾼다.
    utf8* pBase = static_cast<utf8*>( VirtualAlloc( nullptr, pageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE ) );
    SW_ASSERT_TRUE( pBase != nullptr );

    DWORD oldProtect{ 0 };
    SW_EXPECT_TRUE( VirtualProtect( pBase + pageSize, pageSize, PAGE_NOACCESS, &oldProtect ) != 0 );

    // "ab\0" 의 종단자가 첫 페이지의 **마지막 바이트**가 되도록 붙인다.
    utf8* pHaystack = pBase + pageSize - 3;
    pHaystack[0]    = 'a';
    pHaystack[1]    = 'b';
    pHaystack[2]    = '\0';

    // 건초더미(2글자)보다 긴 검색어 — 길이만 보면 다음 페이지까지 읽어야 하는 모양이다.
    SW_EXPECT_TRUE( sw::StringUtil::stristr( pHaystack, "abcdefghij" ) == nullptr );
    SW_EXPECT_TRUE( sw::StringUtil::stristr( pHaystack, "bc" ) == nullptr );

    // 정상 동작도 같이 확인한다.
    SW_EXPECT_TRUE( sw::StringUtil::stristr( pHaystack, "AB" ) == pHaystack );
    SW_EXPECT_TRUE( sw::StringUtil::stristr( pHaystack, "b" ) == pHaystack + 1 );

    VirtualFree( pBase, 0, MEM_RELEASE );
#endif
}

/**
 * @brief [StringTest] 비-ASCII 바이트를 부호 없이 다룬다
 * @details `char` 의 부호성은 구현 정의이고, UTF-8 의 0x80 이상 바이트는 signed char 에서 음수다.
 *          그대로 int 로 넓히면 두 가지가 깨진다:
 *
 *          1. `compare` 가 한글처럼 비-ASCII 가 섞인 문자열을 ASCII 보다 **작다고** 답한다.
 *             `strcmp` 규약(부호 없는 바이트 비교)의 반대이고, 대소문자 구분 경로는 `uint8` 로
 *             비교하므로 두 모드가 서로 다른 순서를 낸다.
 *          2. `computeHash64` · `computeHash32` 의 `bIgnoreCase` 경로(기본값이 true 다)만 부호 확장되면
 *             같은 바이트가 경로에 따라 다른 값으로 해싱되고, `char` 가 unsigned 인 플랫폼에서는
 *             해시 자체가 달라진다.
 */
SW_TEST_CASE( StringTest, NonAsciiBytesAreUnsigned )
{
    // "가" = EA B0 80 — 모든 바이트가 signed char 에서 음수다.
    const sw::string korean{ "\xEA\xB0\x80" };
    const sw::string ascii{ "a" };

    // 부호 없는 바이트 비교라면 0xEA > 0x61 이므로 한글이 뒤에 온다.
    SW_EXPECT_TRUE( sw::StringUtil::compare( korean, ascii, false ) > 0 );
    SW_EXPECT_TRUE( sw::StringUtil::compare( korean, ascii, true ) > 0 );
    SW_EXPECT_TRUE( sw::StringUtil::compare( ascii, korean, true ) < 0 );

    // 두 모드의 순서가 일치해야 한다.
    const int32 sensitive   = sw::StringUtil::compare( korean, ascii, false );
    const int32 insensitive = sw::StringUtil::compare( korean, ascii, true );
    SW_EXPECT_TRUE( ( sensitive > 0 ) == ( insensitive > 0 ) );

    // 포인터 오버로드도 같아야 한다.
    SW_EXPECT_TRUE( sw::StringUtil::compare( korean.c_str(), ascii.c_str(), true ) > 0 );

    // 비-ASCII 에는 대소문자가 없으므로 두 해시 경로가 같은 값을 내야 한다.
    const uint64 hashIgnore = sw::StringUtil::computeHash64( korean.c_str(), korean.size(), true );
    const uint64 hashExact  = sw::StringUtil::computeHash64( korean.c_str(), korean.size(), false );
    SW_EXPECT_EQUAL( hashExact, hashIgnore );

    // **32비트 쌍둥이도 같아야 한다.** `computeHash32` 의 bIgnoreCase 경로가 기본값이고,
    // `hashed_string` 의 intern 이 바로 그것을 쓴다.
    const uint32 hash32Ignore = sw::StringUtil::computeHash32( korean.c_str(), korean.size(), true );
    const uint32 hash32Exact  = sw::StringUtil::computeHash32( korean.c_str(), korean.size(), false );
    SW_EXPECT_EQUAL( hash32Exact, hash32Ignore );

    // ASCII 는 대문자만 접힌다.
    SW_EXPECT_EQUAL( sw::StringUtil::computeHash64( "ABC", 3, true ), sw::StringUtil::computeHash64( "abc", 3, true ) );
    SW_EXPECT_TRUE( sw::StringUtil::computeHash64( "ABC", 3, false ) != sw::StringUtil::computeHash64( "abc", 3, false ) );
}

/**
 * @brief [StringTest] `%#` 바로 뒤의 `.확장자` 는 리터럴이다 — 로그 파일 이름이 `.txt` 를 잃으면 안 된다.
 * @details `%#.txt` 를 "정밀도 0 + 길이 수식어 t + 16진수 x" 로 읽으면 `.tx` 가 사라지고 `t` 만 남는다.
 *          `%#` 은 두 글자만 소비한다. printf 형 정밀도(`%.2f`)와 `%#.%#`(버전 표기)은 그대로여야 한다.
 */
SW_TEST_CASE( StringTest, FormatPlaceholderFollowedByExtensionIsLiteral )
{
    utf8 buffer[128]{};

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "LOG_%#-%#_%#.txt", 2026, 9, "8a19215712386c90" );
    SW_EXPECT_STREQ( "LOG_2026-9_8a19215712386c90.txt", buffer );

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "api %#.%#.%#", 1, 3, 250 );
    SW_EXPECT_STREQ( "api 1.3.250", buffer );

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%.2f", 3.14159 );
    SW_EXPECT_STREQ( "3.14", buffer );

    sw::formatstring( buffer, static_cast<uint32>( sizeof( buffer ) ), "%#. done", "x" );
    SW_EXPECT_STREQ( "x. done", buffer );
}

/**
 * @brief [StringTest] 큰 실수는 고정소수점 자릿수를 전부 담는다 — |x| ≥ 1e121 에서 uint64 캐스트(UB)로 떨어지면 안 된다.
 * @details 1e300 의 double 값은 정수부 301자리(1000000000000000052504760255…)다. 그리고 값 변환은 목적지에 자리가 있으면
 *          바로 쓰고, 작은 버퍼에서는 임시를 거쳐 앞부분만 남긴다 — 두 경로의 결과가 같아야 한다.
 */
SW_TEST_CASE( StringTest, FormatStringHugeFloatAndDirectWrite )
{
    utf8 wide[1024]{};
    sw::formatstring( wide, static_cast<uint32>( sizeof( wide ) ), "%#", 1e300 );
    SW_EXPECT_EQUAL( 301u + 7u, static_cast<uint32>( sw::StringUtil::strlen( wide ) ) ); // 301자리 + ".000000"
    SW_EXPECT_TRUE( sw::string_view( wide ).substr( 0, 29 ) == "10000000000000000525047602552" );
    SW_EXPECT_TRUE( sw::string_view( wide ).substr( 301 ) == ".000000" );

    // 목적지가 넉넉한 경우(직접 쓰기)와 빠듯한 경우(임시 경유)가 같은 글자를 낸다. 빠듯한 쪽은 앞부분만 남는다.
    utf8 direct[1024]{};
    utf8 tight[12]{};
    sw::formatstring( direct, static_cast<uint32>( sizeof( direct ) ), "v=%# %#", 1234567, 3.5 );
    sw::formatstring( tight, static_cast<uint32>( sizeof( tight ) ), "v=%# %#", 1234567, 3.5 );
    SW_EXPECT_STREQ( "v=1234567 3.500000", direct );
    SW_EXPECT_STREQ( "v=1234567 3", tight );
}

/**
 * @brief [StringTest] strncpy 가 플랫폼과 무관하게 **언제나 끝을 맺고** 넘치면 자르는지 검증
 * @details 플랫폼 함수에 그대로 넘기면 답이 갈린다. Windows 의 `strncpy_s( dst, length, src, length )`
 *          는 원본이 종결자까지 들어가지 않으면 목적지를 **빈 문자열로 만들고** 잘못된 파라미터
 *          핸들러를 부른다. Linux 의 `::strncpy` 는 `length` 글자를 복사하고 **종결자를
 *          붙이지 않는다** — 뒤이어 읽는 쪽이 버퍼 밖까지 훑는다.
 */
SW_TEST_CASE( StringTest, StrncpyAlwaysTerminatesAndTruncates )
{
    BLOCK( "들어가는 경우 — 그대로 복사하고 끝을 맺는다" )
    {
        utf8 buffer[16]{};
        sw::StringUtil::strncpy( buffer, "Hero", 16 );
        SW_EXPECT_STREQ( "Hero", buffer );
    }

    BLOCK( "안 들어가는 경우 — 자르되 반드시 끝을 맺는다" )
    {
        // 마지막 칸까지 미리 더럽혀 두고, 종결자가 실제로 쓰이는지 본다.
        utf8 buffer[5] = { 'X', 'X', 'X', 'X', 'X' };
        sw::StringUtil::strncpy( buffer, "HeroPlayer", 5 );
        SW_EXPECT_STREQ( "Hero", buffer );
    }

    BLOCK( "길이 0 이면 아무것도 건드리지 않는다" )
    {
        utf8 buffer[4] = { 'a', 'b', 'c', '\0' };
        sw::StringUtil::strncpy( buffer, "zzz", 0 );
        SW_EXPECT_STREQ( "abc", buffer );
    }

    BLOCK( "널 원본이면 빈 문자열이 된다" )
    {
        utf8 buffer[4] = { 'a', 'b', 'c', '\0' };
        sw::StringUtil::strncpy( buffer, static_cast<const utf8*>( nullptr ), 4 );
        SW_EXPECT_STREQ( "", buffer );
    }

    BLOCK( "utf16 판도 같은 규약이다" )
    {
        utf16 buffer[5] = { L'X', L'X', L'X', L'X', L'X' };
        sw::StringUtil::strncpy( buffer, L"HeroPlayer", 5 );
        SW_EXPECT_EQUAL( size_t( 4 ), sw::wstring_view( buffer ).size() );
    }
}

/**
 * @brief [StringTest] fixed_string::erase 가 큰 길이에서 첨자를 접지 않는지 검증
 * @details 판정을 `pos + length >= currentSize` 로 하면 `npos` 가 아닌 큰 길이가 들어올 때
 *          (끝-시작 이 뒤집힌 계산 같은 것) 그 합이 `uint32` 안에서 접혀 작은 수가 되고,
 *          "끝까지 지운다" 가 아니라 **범위 이동** 쪽으로 빠진다 — 거기서
 *          `_arrData + pos + length` 라는 엉뚱한 주소를 읽는다. 뺄셈 형태(`currentSize - pos`)로 판정해야 한다.
 */
SW_TEST_CASE( StringTest, FixedStringEraseWithHugeLengthDoesNotWrap )
{
    BLOCK( "npos 가 아닌 큰 길이 — 끝까지 지운 것과 같아야 한다" )
    {
        sw::fixed_string<32> str( "0123456789" );
        str.erase( 5, 0xFFFFFFFCu );
        SW_EXPECT_STREQ( "01234", str.c_str() );
        SW_EXPECT_EQUAL( 5u, str.size() );
    }

    BLOCK( "평범한 지우기는 그대로다" )
    {
        sw::fixed_string<32> str( "0123456789" );
        str.erase( 3, 4 );
        SW_EXPECT_STREQ( "012789", str.c_str() );
        SW_EXPECT_EQUAL( 6u, str.size() );
    }

    BLOCK( "npos 는 끝까지" )
    {
        sw::fixed_string<32> str( "0123456789" );
        str.erase( 7 );
        SW_EXPECT_STREQ( "0123456", str.c_str() );
    }
}

/**
 * @brief [StringTest] 용량 0 으로 부른 formatstring 이 버퍼 밖에 쓰지 않는지 검증
 * @details 입구의 `SW_ASSERT( capacity > 0 )` 는 **Debug 밖에서는 통째로 사라진다.**
 *          `capacity` 는 어디서나 `capacity - 1` 로 쓰이므로(남은 자리 계산, 종결자 위치)
 *          가드가 없으면 0 에서 그 뺄셈이 뒤집혀 4,294,967,295 가 되고, 길이 제한 없이 복사한다.
 */
SW_TEST_CASE( StringTest, FormatStringWithZeroCapacityWritesNothing )
{
    // 한 칸이면 종결자만 들어간다 — 이쪽은 어느 빌드에서나 잰다.
    sw::vector<utf8> oneBuffer( 1, utf8{ 'Z' } );
    sw::formatstring( oneBuffer.data(), 1, "hello" );
    SW_EXPECT_EQUAL( utf8{ 0 }, oneBuffer[0] );

    // 용량 0 은 Debug 에서 `SW_ASSERT` 가 먼저 울린다 — 단언 가로채기 안에서 불러 그 뒤의 가드까지 지나가게 한다.
    // 딱 한 칸짜리 힙 버퍼 — 넘치면 ASAN 이 그 자리에서 잡고, 값으로도 드러난다.
    test::ScopedAssertCapture assertCapture;
    sw::vector<utf8>          tinyBuffer( 1, utf8{ 'Z' } );
    sw::formatstring( tinyBuffer.data(), 0, "hello world %#", 42 );
    SW_EXPECT_EQUAL( utf8{ 'Z' }, tinyBuffer[0] );
    if ( test::ScopedAssertCapture::kAssertsAreActive )
        SW_EXPECT_EQUAL( 1u, assertCapture.getCount() );
}

/**
 * @brief [StringTest] `contains` 는 `startsWith` · `endsWith` 와 같은 규칙을 따른다
 * @details 셋은 같은 질문의 세 자리다. 가운데가 빠지면 부르는 쪽이 `str.find( sub ) != npos` 로
 *          손수 적고 그때마다 `bIgnoreCase` 를 잃는다. 빈 부분 문자열은 표준 `find` 와 같이 참이다.
 */
SW_TEST_CASE( StringTest, ContainsFollowsTheSameRulesAsItsTwoSiblings )
{
    using sw::StringUtil;

    SW_EXPECT_TRUE( StringUtil::contains( "levels/dungeon_01.scene", "dungeon" ) );
    SW_EXPECT_TRUE( StringUtil::contains( "levels/dungeon_01.scene", "levels" ) );
    SW_EXPECT_TRUE( StringUtil::contains( "levels/dungeon_01.scene", ".scene" ) );
    SW_EXPECT_TRUE( StringUtil::contains( "levels/dungeon_01.scene", "" ) );
    SW_EXPECT_TRUE( StringUtil::contains( "abc", "abc" ) );

    SW_EXPECT_TRUE( StringUtil::contains( "levels/dungeon_01.scene", "castle" ) == false );
    SW_EXPECT_TRUE( StringUtil::contains( "ab", "abc" ) == false );
    SW_EXPECT_TRUE( StringUtil::contains( "", "a" ) == false );

    // 대소문자 무시는 **선택**이고, 켜면 세 자리 어디서든 같게 동작한다.
    SW_EXPECT_TRUE( StringUtil::contains( "Levels/Dungeon_01.scene", "dungeon" ) == false );
    SW_EXPECT_TRUE( StringUtil::contains( "Levels/Dungeon_01.scene", "dungeon", true ) );
    SW_EXPECT_TRUE( StringUtil::contains( "levels/DUNGEON.scene", "Dungeon", true ) );
    SW_EXPECT_TRUE( StringUtil::startsWith( "Levels/Dungeon_01.scene", "levels", true ) );
    SW_EXPECT_TRUE( StringUtil::endsWith( "Levels/Dungeon_01.SCENE", ".scene", true ) );

    // 끝자리에 걸친 것도 찾는다 — 마지막 시작 위치를 빠뜨리기 쉬운 자리다.
    SW_EXPECT_TRUE( StringUtil::contains( "abcXYZ", "xyz", true ) );
}

/**
 * @brief [StringTest] 정수 파서 셋(int32 · int64 · uint64)의 경계 — 부호 · 기수 접두사 · 최솟값 · 넘침 · 꼬리 글자
 * @details 셋이 같은 앞머리 처리(`splitIntegerToken`)를 쓰므로 그 규칙을 여기 못박는다. 최솟값은 절댓값이 최댓값 + 1 이라
 *          부호 없는 쪽으로 읽은 뒤 따로 다룬다 — 그 한 칸이 가장 틀리기 쉽다.
 */
SW_TEST_CASE( StringTest, IntegerParsersShareSignPrefixAndRangeRules )
{
    int32 value32{ 0 };
    SW_EXPECT_TRUE( sw::StringUtil::parseInt( " 42 ", value32 ) && value32 == 42 );
    SW_EXPECT_TRUE( sw::StringUtil::parseInt( "+7", value32 ) && value32 == 7 );
    SW_EXPECT_TRUE( sw::StringUtil::parseInt( "-2147483648", value32 ) && value32 == sw::MathUtil::kMinInt32 );
    SW_EXPECT_TRUE( sw::StringUtil::parseInt( "2147483647", value32 ) && value32 == sw::MathUtil::kMaxInt32 );
    SW_EXPECT_FALSE( sw::StringUtil::parseInt( "2147483648", value32 ) );
    SW_EXPECT_FALSE( sw::StringUtil::parseInt( "-2147483649", value32 ) );
    SW_EXPECT_TRUE( sw::StringUtil::parseInt( "0x1F", value32, 0 ) && value32 == 31 );
    SW_EXPECT_TRUE( sw::StringUtil::parseInt( "1F", value32, 16 ) && value32 == 31 );
    SW_EXPECT_TRUE( sw::StringUtil::parseInt( "-0x10", value32, 16 ) && value32 == -16 );
    SW_EXPECT_TRUE( sw::StringUtil::parseInt( "017", value32, 0 ) && value32 == 17 ); // 기수 0 은 0x 만 본다 — 8 진 접두사는 없다
    SW_EXPECT_FALSE( sw::StringUtil::parseInt( "12a", value32 ) );
    SW_EXPECT_FALSE( sw::StringUtil::parseInt( "-", value32 ) );
    SW_EXPECT_FALSE( sw::StringUtil::parseInt( "", value32 ) );
    SW_EXPECT_FALSE( sw::StringUtil::parseInt( "0x", value32, 16 ) );
    SW_EXPECT_FALSE( sw::StringUtil::parseInt( "5", value32, 1 ) );

    int64 value64{ 0 };
    SW_EXPECT_TRUE( sw::StringUtil::parseInt64( "-9223372036854775808", value64 ) && value64 == sw::MathUtil::kMinInt64 );
    SW_EXPECT_TRUE( sw::StringUtil::parseInt64( "9223372036854775807", value64 ) && value64 == sw::MathUtil::kMaxInt64 );
    SW_EXPECT_FALSE( sw::StringUtil::parseInt64( "9223372036854775808", value64 ) );

    uint64 valueU64{ 0 };
    SW_EXPECT_TRUE( sw::StringUtil::parseUint64( "18446744073709551615", valueU64 ) && valueU64 == ~uint64{ 0 } );
    SW_EXPECT_TRUE( sw::StringUtil::parseUint64( "+0xff", valueU64, 0 ) && valueU64 == 255 );
    SW_EXPECT_FALSE( sw::StringUtil::parseUint64( "-1", valueU64 ) ); // 부호 없는 쪽은 `-` 를 받지 않는다
    SW_EXPECT_FALSE( sw::StringUtil::parseUint64( "18446744073709551616", valueU64 ) );
}

/**
 * @brief [StringTest] 대소문자 변환과 비교의 utf16 판 — utf8 판과 같은 뼈대(`mapEachChar` · `equalsView`)를 쓴다
 */
SW_TEST_CASE( StringTest, WideCaseMappingAndEqualsMatchNarrow )
{
    SW_EXPECT_TRUE( sw::StringUtil::toUpper( L"abcXyz09" ) == sw::wstring( L"ABCXYZ09" ) );
    SW_EXPECT_TRUE( sw::StringUtil::toLower( L"ABCxYZ09" ) == sw::wstring( L"abcxyz09" ) );
    SW_EXPECT_TRUE( sw::StringUtil::toUpper( static_cast<const utf16*>( nullptr ) ).empty() );
    SW_EXPECT_TRUE( sw::StringUtil::toLower( "" ).empty() );
    SW_EXPECT_TRUE( sw::StringUtil::equals( sw::wstring_view( L"Scene" ), sw::wstring_view( L"SCENE" ), true ) );
    SW_EXPECT_FALSE( sw::StringUtil::equals( sw::wstring_view( L"Scene" ), sw::wstring_view( L"SCENE" ), false ) );
    SW_EXPECT_FALSE( sw::StringUtil::equals( sw::wstring_view( L"Scene" ), sw::wstring_view( L"Scenes" ), true ) );
}

/**
 * @brief [StringTest] 잘못된 UTF-8 은 U+FFFD 로 바뀌고, 그 뒤의 글자를 삼키지 않는다.
 * @details `utf8ToUtf16` 이 검증 없이 비트만 이어 붙이면 선두가 될 수 없는 바이트가 U+0000 이 되어 Win32 경로 API 에서 문자열이
 *          거기서 끊기고, 연속 바이트를 확인하지 않으면 잘린 시퀀스 뒤의 ASCII(`/` · `.`)를 삼킨다.
 */
SW_TEST_CASE( StringTest, Utf8ToUtf16ReplacesInvalidBytesWithoutSwallowing )
{
    // 선두가 될 수 없는 바이트 → U+FFFD, 뒤의 ASCII 는 그대로
    const sw::wstring stray = sw::StringUtil::utf8ToUtf16( "\x80"
                                                           "abc" );
    SW_ASSERT_EQUAL( 4u, static_cast<uint32>( stray.size() ) );
    SW_EXPECT_TRUE( stray[0] == 0xFFFD );
    SW_EXPECT_TRUE( stray[1] == L'a' && stray[2] == L'b' && stray[3] == L'c' );

    // 두 바이트 시퀀스가 ASCII 에서 끊겼다 → U+FFFD 하나 + 'A'(삼키지 않는다)
    const sw::wstring cut = sw::StringUtil::utf8ToUtf16( "\xC3"
                                                         "A/" );
    SW_ASSERT_EQUAL( 3u, static_cast<uint32>( cut.size() ) );
    SW_EXPECT_TRUE( cut[0] == 0xFFFD );
    SW_EXPECT_TRUE( cut[1] == L'A' && cut[2] == L'/' );

    // 문자열 끝에서 잘렸다
    const sw::wstring tail = sw::StringUtil::utf8ToUtf16( "a"
                                                          "\xEA\xB0" );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( tail.size() ) );
    SW_EXPECT_TRUE( tail[0] == L'a' && tail[1] == 0xFFFD );

    // 과잉 인코딩('/' 를 두 바이트로) · 서로게이트 구간 → U+FFFD 한 글자
    const sw::wstring overlong = sw::StringUtil::utf8ToUtf16( "\xC0\xAF" );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( overlong.size() ) );
    SW_EXPECT_TRUE( overlong[0] == 0xFFFD );
    const sw::wstring surrogate = sw::StringUtil::utf8ToUtf16( "\xED\xA0\x80" );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( surrogate.size() ) );
    SW_EXPECT_TRUE( surrogate[0] == 0xFFFD );

    // 올바른 입력은 그대로
    const sw::wstring hangul = sw::StringUtil::utf8ToUtf16( "가/" );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( hangul.size() ) );
    SW_EXPECT_TRUE( hangul[0] == 0xAC00 && hangul[1] == L'/' );
}

namespace
{
    enum class SignedCodeInternal : int32
    {
        Negative = -1,
        Positive = 3
    };
} // namespace

/**
 * @brief [StringTest] 0 채움은 부호 뒤에 붙고, 부호 있는 열거형은 음수로 찍히고, 버퍼 끝에서는 UTF-8 글자 경계에서 자른다.
 * @details 틀리면 "%05d" 로 -42 가 "00-42", 열거형 -1 이 18446744073709551615 가 되고, 버퍼 끝에 걸친 한글 한 글자가 깨진 바이트로 남는다.
 */
SW_TEST_CASE( StringTest, FormatSignPaddingSignedEnumAndUtf8Truncation )
{
    sw::fixed_string<sw::constant::kMaxBuffer64> text;
    sw::formatstring( text.data(), text.capacity(), "%05d", -42 );
    SW_EXPECT_STREQ( "-0042", text.c_str() );

    sw::formatstring( text.data(), text.capacity(), "%#", SignedCodeInternal::Negative );
    SW_EXPECT_STREQ( "-1", text.c_str() );

    // 버퍼 5 바이트(글자 4 바이트 + 종료) 에 "가나"(6 바이트) — "가" 3 바이트만 남아야 한다.
    utf8 arrSmall[5]{};
    sw::formatstring( arrSmall, 5, "%#", "가나" );
    SW_EXPECT_TRUE( sw::StringUtil::isValidUtf8( arrSmall ) );
    SW_EXPECT_STREQ( "가", arrSmall );
}

/**
 * @brief [StringTest] `fixed_string` 은 `data()` 로 직접 쓴 뒤에도 복사가 맞고, 자기 자신을 끼워 넣어도 맞다. `StringBuilder` 는 자기 내용을 이어 붙여도 맞다.
 */
SW_TEST_CASE( StringTest, FixedStringAndBuilderSelfReference )
{
    sw::fixed_string<sw::constant::kMaxBuffer64> source;
    std::memcpy( source.data(), "hello", 6 ); // ImGui 입력칸처럼 버퍼에 직접 쓴다 — 캐시된 길이는 0 그대로다
    sw::fixed_string<sw::constant::kMaxBuffer64> target = "world";
    target                                              = source;
    SW_EXPECT_STREQ( "hello", target.c_str() );
    const sw::fixed_string<sw::constant::kMaxBuffer64> copied( source );
    SW_EXPECT_STREQ( "hello", copied.c_str() );

    sw::fixed_string<sw::constant::kMaxBuffer64> selfInsert = "abc";
    selfInsert.insert( 1, selfInsert.c_str() );
    SW_EXPECT_STREQ( "aabcbc", selfInsert.c_str() );

    sw::StringBuilder<sw::constant::kMaxBuffer16> builder;
    builder.append( "0123456789" );
    for ( uint32 repeat = 0; repeat < 4; ++repeat )
        builder.append( builder.view() );
    SW_EXPECT_EQUAL( 160u, static_cast<uint32>( builder.view().size() ) );
    SW_EXPECT_TRUE( builder.view().substr( 150 ) == "0123456789" );
}

/**
 * @brief [StringTest] 부동소수 파서는 유한한 수만, 부호는 하나만 받는다
 * @details 앞의 `+` 를 떼고 `from_chars` 에 넘기면 `"+-5"` 가 -5 가 되고(`parseInt` 는 거절한다), "nan" · "inf" 를 받으면 설정 ·
 *          에셋의 그 글자가 트랜스폼 · 물리 값으로 조용히 흘러든다(NaN 은 비교마다 거짓이라 범위 검사도 지나간다).
 */
SW_TEST_CASE( StringTest, FloatParsersAcceptOnlyFiniteNumbersWithOneSign )
{
    float32 value32{ 0.0f };
    float64 value64{ 0.0 };
    SW_EXPECT_TRUE( sw::StringUtil::parseFloat( " 1.5 ", value32 ) && value32 == 1.5f );
    SW_EXPECT_TRUE( sw::StringUtil::parseFloat( "+2.25", value32 ) && value32 == 2.25f );
    SW_EXPECT_TRUE( sw::StringUtil::parseFloat( "-0.5", value32 ) && value32 == -0.5f );
    SW_EXPECT_TRUE( sw::StringUtil::parseDouble( "1e-3", value64 ) && value64 == 1e-3 );

    for ( const utf8* pBad : { "+-5", "++5", "+", "nan", "NaN", "inf", "-inf", "+inf", "infinity", "1e400", "0.5f", "" } )
    {
        SW_EXPECT_FALSE_MSG( sw::StringUtil::parseFloat( pBad, value32 ), pBad );
        SW_EXPECT_FALSE_MSG( sw::StringUtil::parseDouble( pBad, value64 ), pBad );
    }
}

/**
 * @brief [StringTest] 잘못된 UTF-8 바이트만 백슬래시 + `xNN` 으로 바뀌고 올바른 글자(한글 포함)는 그대로다 — 로그 한 줄이 통째로 깨지지 않는다
 * @details 한 바이트만 틀려도 줄 전체를 로캘 변환하면 C 로캘이라 Windows 에서는 한글까지 깨지고 glibc 에서는 줄이 빈다.
 */
SW_TEST_CASE( StringTest, EscapeInvalidUtf8KeepsValidText )
{
    const sw::string mixed = sw::StringUtil::escapeInvalidUtf8( "\xED\x95\x9C\xEA\xB8\x80 \xFF ok \xC3" ); // "한글 <FF> ok <잘린 2바이트>"
    SW_EXPECT_STREQ( "\xED\x95\x9C\xEA\xB8\x80 \\xFF ok \\xC3", mixed.c_str() );
    SW_EXPECT_TRUE( sw::StringUtil::isValidUtf8( mixed.c_str() ) );

    SW_EXPECT_STREQ( "plain ascii", sw::StringUtil::escapeInvalidUtf8( "plain ascii" ).c_str() );
    SW_EXPECT_STREQ( "\\xC0\\xAF", sw::StringUtil::escapeInvalidUtf8( "\xC0\xAF" ).c_str() );          // overlong '/'
    SW_EXPECT_STREQ( "\\xED\\xA0\\x80", sw::StringUtil::escapeInvalidUtf8( "\xED\xA0\x80" ).c_str() ); // 서로게이트
}

/**
 * @brief [StringTest] 바이트 오프셋 → 줄 · 열(1 부터, 열은 UTF-8 글자 수)
 */
SW_TEST_CASE( StringTest, LineAndColumnCountCharactersNotBytes )
{
    uint32 line   = 0;
    uint32 column = 0;
    sw::StringUtil::getLineAndColumn( "ab\ncd", 0, line, column );
    SW_EXPECT_TRUE( line == 1 && column == 1 );
    sw::StringUtil::getLineAndColumn( "ab\ncd", 4, line, column ); // 'd'
    SW_EXPECT_TRUE( line == 2 && column == 2 );
    sw::StringUtil::getLineAndColumn( "\xED\x95\x9C\xEA\xB8\x80x", 6, line, column ); // "한글" 뒤의 x — 6 바이트 뒤지만 3 번째 글자
    SW_EXPECT_TRUE( line == 1 && column == 3 );
    sw::StringUtil::getLineAndColumn( "ab", 99, line, column ); // 글 밖이면 끝 자리
    SW_EXPECT_TRUE( line == 1 && column == 3 );
}

/**
 * @brief [StringTest] fixed_string 이 넘치는 글을 자를 때 글자 한가운데가 아니라 그 글자의 앞에서 자른다(UTF-8 · UTF-16)
 * @details 바이트 수로만 자르면 긴 한글 이름의 끝 글자가 반 토막(잘못된 UTF-8)으로 남아 에디터가 `?` 로 그린다.
 */
SW_TEST_CASE( StringTest, FixedStringTruncatesOnCharacterBoundary )
{
    test::ScopedDefensiveTestLog expected( "fixed_string capacity overflow" );

    const sw::fixed_string<4> constructed( "ab\xED\x95\x9C" ); // "ab한" = 5 바이트
    SW_EXPECT_STREQ( "ab", constructed.c_str() );

    sw::fixed_string<5> appended( "abc" );
    appended.append( "\xED\x95\x9C" ); // 남은 2 바이트에 3 바이트 글자 — 아무것도 붙이지 않는다
    SW_EXPECT_STREQ( "abc", appended.c_str() );

    sw::fixed_string<4> assigned;
    assigned = sw::string_view( "\xED\x95\x9C\xEA\xB8\x80" ); // "한글" 6 바이트 → "한"
    SW_EXPECT_STREQ( "\xED\x95\x9C", assigned.c_str() );
    SW_EXPECT_TRUE( sw::StringUtil::isValidUtf8( assigned.c_str() ) );

    // `utf16` 은 wchar_t 다 — Windows 에서만 2 바이트(UTF-16)이고 서로게이트 쌍이 있다. 4 바이트(UTF-32)면 쌍이 없다.
    if constexpr ( sizeof( utf16 ) == 2 )
    {
        const utf16                arrWide[] = { L'a', static_cast<utf16>( 0xD83D ), static_cast<utf16>( 0xDE00 ), 0 }; // "a😀"
        const sw::fixed_wstring<2> wide( arrWide );
        SW_EXPECT_EQUAL( 1u, wide.size() );
    }

    // 들어맞는 글은 그대로다.
    const sw::fixed_string<6> exact( "ab\xED\x95\x9C" );
    SW_EXPECT_STREQ( "ab\xED\x95\x9C", exact.c_str() );
}

/**
 * @brief [StringTest] fixed_string 에 자기 버퍼 안쪽(포인터 · 뷰)을 대입해도 맞게 옮긴다
 * @details 시작 주소가 같은 자기 대입만 거르면 `s = s.c_str() + 2` 는 겹친 memcpy(정의되지 않은 동작)가 된다. Windows 의 memcpy 는 우연히 맞게 옮겨
 *          이 시험은 여기서 늘 통과한다 — 겹침은 리눅스 ASan CI(`memcpy-param-overlap`)가 잡는다.
 */
SW_TEST_CASE( StringTest, FixedStringAssignFromItsOwnInterior )
{
    sw::fixed_string<sw::constant::kMaxBuffer32> text = "abcdef";
    text                                              = text.c_str() + 2;
    SW_EXPECT_STREQ( "cdef", text.c_str() );

    text = sw::string_view( text.c_str() + 1, 2 );
    SW_EXPECT_STREQ( "de", text.c_str() );
}

/**
 * @brief [StringTest] decodeUtf8 · appendUtf8 는 글자 단위로 왕복하고, 잘못된 바이트는 U+FFFD 로 읽고도 앞으로 나아간다
 * @details 의사 로컬라이제이션 · 메시지 포맷이 글자 단위로 글을 바꾼다 — 바이트 단위로 바꾸면 한글 · 이모지가 깨진다.
 */
SW_TEST_CASE( StringTest, Utf8DecodeAndAppendRoundTrip )
{
    const sw::string_view kText = "a\xC3\xA9\xEA\xB0\x80\xF0\x9F\x9A\x80"; // a é 가 🚀
    size_t                offset{ 0 };
    SW_EXPECT_EQUAL( uint32( 'a' ), sw::StringUtil::decodeUtf8( kText, offset ) );
    SW_EXPECT_EQUAL( uint32( 0xE9 ), sw::StringUtil::decodeUtf8( kText, offset ) );
    SW_EXPECT_EQUAL( uint32( 0xAC00 ), sw::StringUtil::decodeUtf8( kText, offset ) );
    SW_EXPECT_EQUAL( uint32( 0x1F680 ), sw::StringUtil::decodeUtf8( kText, offset ) );
    SW_EXPECT_EQUAL( kText.size(), offset );
    SW_EXPECT_EQUAL( uint32( 0 ), sw::StringUtil::decodeUtf8( kText, offset ) );

    sw::string rebuilt;
    for ( const uint32 codepoint : { uint32( 'a' ), uint32( 0xE9 ), uint32( 0xAC00 ), uint32( 0x1F680 ) } )
    {
        sw::StringUtil::appendUtf8( rebuilt, codepoint );
    }
    SW_EXPECT_TRUE( rebuilt == kText );

    const sw::string_view kBroken = "\xC2x";
    size_t                brokenOffset{ 0 };
    SW_EXPECT_EQUAL( uint32( 0xFFFD ), sw::StringUtil::decodeUtf8( kBroken, brokenOffset ) );
    SW_EXPECT_TRUE( brokenOffset >= 1u );
    SW_EXPECT_EQUAL( uint32( 'x' ), sw::StringUtil::decodeUtf8( kBroken, brokenOffset ) );
}

/**
 * @brief [StringTest] 불리언 글은 한 표로 읽는다 — `parseBool` 은 `tryParseBool` 이 읽은 값, 읽지 못하면 폴백이다
 * @details 설정 · 텔레메트리 동의 · 개발 명령이 저마다 `== "true"` · `"on"` · `"1"` 을 비교해 같은 값이 길마다 다르게 읽혔다. 철자 표는 `tryParseBool` 하나다.
 */
SW_TEST_CASE( StringTest, BoolTextIsReadByOneTable )
{
    const utf8* const arrTrue[]    = { "true", "TRUE", "True", "1", "yes", "Yes", "on", "ON", "  true  " };
    const utf8* const arrFalse[]   = { "false", "FALSE", "0", "no", "No", "off", "OFF", " 0 " };
    const utf8* const arrUnknown[] = { "", "ture", "2", "-1", "y", "n", "enabled", "truex" };
    for ( const utf8* pText : arrTrue )
    {
        bool bValue{ false };
        SW_EXPECT_TRUE_MSG( sw::StringUtil::tryParseBool( pText, bValue ) && bValue, pText );
        SW_EXPECT_TRUE_MSG( sw::StringUtil::parseBool( pText, false ), pText );
    }
    for ( const utf8* pText : arrFalse )
    {
        bool bValue{ true };
        SW_EXPECT_TRUE_MSG( sw::StringUtil::tryParseBool( pText, bValue ) && bValue == false, pText );
        SW_EXPECT_FALSE_MSG( sw::StringUtil::parseBool( pText, true ), pText );
    }
    for ( const utf8* pText : arrUnknown )
    {
        bool bValue{ true };
        SW_EXPECT_FALSE_MSG( sw::StringUtil::tryParseBool( pText, bValue ), pText );
        SW_EXPECT_TRUE_MSG( bValue, pText ); // 읽지 못하면 값은 그대로
        SW_EXPECT_TRUE_MSG( sw::StringUtil::parseBool( pText, true ), pText );
        SW_EXPECT_FALSE_MSG( sw::StringUtil::parseBool( pText, false ), pText );
    }
}

#if !defined( SW_SHIPPING )
/**
 * @brief [StringTest] StringBuilder 가 늘리다 할당에 실패하면 쌓은 글을 그대로 두고, 다음 할당이 되면 다시 이어 붙는다
 * @details `ensureCapacity` 가 실패를 확인하지 않고 늘린 용량만 적으면 버퍼가 nullptr 인 채로 다음 append 가 그 자리에 쓴다.
 */
SW_TEST_CASE( StringTest, StringBuilderKeepsItsTextWhenGrowingFails )
{
    sw::StringBuilder<sw::constant::kMaxBuffer16> builder;
    builder.append( "0123456789" );
    sw::Memory::injectAllocationFailures( 1 );
    builder.append( "this text does not fit in sixteen bytes" );
    sw::Memory::injectAllocationFailures( 0 );
    SW_EXPECT_EQUAL( 10u, builder.size() );
    SW_EXPECT_TRUE( builder.view() == "0123456789" );

    builder.append( "abcdefghijklmnopqrstuvwxyz" );
    SW_EXPECT_EQUAL( 36u, builder.size() );
    SW_EXPECT_TRUE( builder.view() == "0123456789abcdefghijklmnopqrstuvwxyz" );
}
#endif
