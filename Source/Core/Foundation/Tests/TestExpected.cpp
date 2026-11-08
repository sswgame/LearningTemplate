#include "pch.h"

#include "Core/Foundation/Expected.h"
#include "Core/Testing/TestFramework.h"

// ------------------------------------------------------------------------------
// sw::expected · sw::optional — 값 · 오류 · 없음, 잇기, 복사 · 이동, 비교
// ------------------------------------------------------------------------------

namespace
{
    sw::expected<int32> parseDigit( utf8 character )
    {
        if ( character < '0' || character > '9' )
            return sw::unexpected( sw::Error{
                sw::ErrorKind::InvalidArgument, "not a digit", sw::string_view{ &character, 1 }
            } );
        return character - '0';
    }

    sw::optional<int32> findEven( int32 value )
    {
        if ( value % 2 != 0 )
            return sw::nullopt;
        return value;
    }

    sw::expected<void> requirePositive( int32 value )
    {
        if ( value <= 0 )
            return sw::unexpected( sw::Error{ sw::ErrorKind::InvalidArgument, "not positive" } );
        return {};
    }
} // namespace

SW_TEST_CASE( ExpectedTest, ValueAndError )
{
    const sw::expected<int32> seven = parseDigit( '7' );
    SW_REQUIRE_TRUE( seven.has_value() );
    SW_EXPECT_TRUE( static_cast<bool>( seven ) );
    SW_EXPECT_EQUAL( *seven, 7 );
    SW_EXPECT_EQUAL( seven.value(), 7 );
    SW_EXPECT_TRUE( seven == 7 );

    const sw::expected<int32> bad = parseDigit( 'x' );
    SW_REQUIRE_FALSE( bad.has_value() );
    SW_EXPECT_TRUE( bad.error().kind() == sw::ErrorKind::InvalidArgument );
    SW_EXPECT_TRUE( bad.error().message() == "not a digit" );
    SW_EXPECT_TRUE( bad.error().detail() == "x" );
    SW_EXPECT_EQUAL( bad.value_or( -1 ), -1 );
}

SW_TEST_CASE( ExpectedTest, ErrorDetailIsCopiedAndTruncated )
{
    sw::string_view detail;
    sw::Error       error{ sw::ErrorKind::NotFound, "missing" };
    {
        const sw::string temporary( sw::Error::kDetailCapacity + 20, 'a' );
        error.appendDetail( temporary ); // 입력이 사라져도 오류는 제 사본을 든다
    }
    detail = error.detail();
    SW_EXPECT_EQUAL( detail.size(), sw::Error::kDetailCapacity );
    SW_EXPECT_TRUE( detail.front() == 'a' && detail.back() == 'a' );
    error.appendDetail( "more" ); // 꽉 찼으면 버린다
    SW_EXPECT_EQUAL( error.detail().size(), sw::Error::kDetailCapacity );
}

SW_TEST_CASE( ExpectedTest, OptionalAlias )
{
    sw::optional<int32> empty;
    SW_EXPECT_FALSE( empty.has_value() );
    SW_EXPECT_TRUE( empty == sw::nullopt );

    empty = 4;
    SW_REQUIRE_TRUE( empty.has_value() );
    SW_EXPECT_EQUAL( *empty, 4 );
    empty.reset();
    SW_EXPECT_FALSE( empty.has_value() );
    empty.emplace( 6 );
    SW_EXPECT_EQUAL( *empty, 6 );
    empty = sw::nullopt;
    SW_EXPECT_FALSE( empty.has_value() );

    SW_EXPECT_TRUE( findEven( 3 ) == sw::nullopt );
    SW_EXPECT_TRUE( findEven( 8 ) == 8 );
    SW_EXPECT_EQUAL( findEven( 5 ).value_or( 0 ), 0 );
}

SW_TEST_CASE( ExpectedTest, VoidSpecialization )
{
    SW_EXPECT_TRUE( requirePositive( 3 ).has_value() );
    const sw::expected<void> failed = requirePositive( 0 );
    SW_REQUIRE_FALSE( failed.has_value() );
    SW_EXPECT_TRUE( failed.error().message() == "not positive" );

    sw::expected<void> copy = failed;
    SW_EXPECT_FALSE( copy.has_value() );
    copy.emplace();
    SW_EXPECT_TRUE( copy.has_value() );
}

SW_TEST_CASE( ExpectedTest, Chaining )
{
    const auto doubled = parseDigit( '4' ).transform( []( int32 value )
    { return value * 2; } );
    SW_REQUIRE_TRUE( doubled.has_value() );
    SW_EXPECT_EQUAL( *doubled, 8 );

    const auto chained = parseDigit( '3' ).and_then( []( int32 value ) -> sw::expected<int32>
    {
        if ( value < 5 )
            return sw::unexpected( sw::Error{ sw::ErrorKind::InvalidArgument, "too small" } );
        return value;
    } );
    SW_REQUIRE_FALSE( chained.has_value() );
    SW_EXPECT_TRUE( chained.error().message() == "too small" );

    const auto passedError = parseDigit( 'q' ).transform( []( int32 value )
    { return value + 1; } );
    SW_EXPECT_TRUE( passedError.error().message() == "not a digit" );

    const sw::expected<int32> recovered = parseDigit( 'q' ).or_else( []( const sw::Error& ) -> sw::expected<int32>
    { return 0; } );
    SW_EXPECT_TRUE( recovered == 0 );

    const sw::expected<void> afterVoid = requirePositive( 1 ).and_then( []()
    { return requirePositive( -1 ); } );
    SW_EXPECT_FALSE( afterVoid.has_value() );
}

SW_TEST_CASE( ExpectedTest, CopyMoveKeepsNonTrivialValue )
{
    sw::optional<sw::string> name = sw::string( "alpha" );
    sw::optional<sw::string> copy = name;
    SW_EXPECT_TRUE( copy == sw::string( "alpha" ) );

    sw::optional<sw::string> moved = std::move( copy );
    SW_EXPECT_TRUE( *moved == "alpha" );

    moved = sw::nullopt; // 값 → 없음
    SW_EXPECT_FALSE( moved.has_value() );
    moved = name; // 없음 → 값
    SW_EXPECT_TRUE( *moved == "alpha" );
    SW_EXPECT_EQUAL( moved->size(), static_cast<size_t>( 5 ) );
}
