#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

using namespace sw;

namespace
{
    /** @brief 손 읽기 줄만 가진 작은 카탈로그입니다. 읽은 차례를 남겨 순서를 봅니다. */
    struct XMLCatalogTableTestCatalog
    {
        vector<hashed_string> _listOrder{};

        bool readFirst( const XMLNode& node, string_view sourceName )
        {
            (void)node;
            (void)sourceName;
            _listOrder.push_back( hashed_string( "First" ) );
            return true;
        }
        bool readSecond( const XMLNode& node, string_view sourceName )
        {
            (void)sourceName;
            _listOrder.push_back( hashed_string( "Second" ) );
            return node.findAttribute( "bad" ) == nullptr;
        }
        bool readNote( const XMLNode& node, string_view sourceName )
        {
            (void)node;
            (void)sourceName;
            _listOrder.push_back( hashed_string( "Note" ) );
            return true;
        }
    };

    struct XMLCatalogTableTestUtil
    {
        using Catalog = XMLCatalogTableTestCatalog;
        using Element = XMLCatalogElement<Catalog>;

        static constexpr Element kArrElement[] = {
            Element::of<&Catalog::readFirst>( "First" ),
            Element::of<&Catalog::readSecond>( "Second" ),
            Element::of<&Catalog::readNote>( "Note" ).uncounted(),
        };
        static constexpr Element kArrUncounted[] = {
            Element::of<&Catalog::readNote>( "Note" ).uncounted(),
        };

        static XMLNode parse( XMLDocument& outDocument, string_view xmlText )
        {
            SW_EXPECT_TRUE( outDocument.parse( xmlText ) );
            return outDocument.getRoot( "Root" );
        }
    };
} // namespace

/**
 * @brief [XMLCatalogTableTest] 문서 순서는 적힌 대로 읽고, 세지 않는 줄은 수에 들어가지 않는다
 */
SW_TEST_CASE( XMLCatalogTableTest, DocumentOrderFollowsTheFile )
{
    XMLDocument                document;
    const XMLNode              root = XMLCatalogTableTestUtil::parse( document, R"(<Root><Second/><Note/><First/></Root>)" );
    XMLCatalogTableTestCatalog catalog;

    const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( catalog, XMLCatalogRoot{ root }, "test", XMLCatalogTableTestUtil::kArrElement );

    SW_EXPECT_EQUAL( 2u, result._loadedCount );
    SW_EXPECT_TRUE( result.isClean() );
    SW_REQUIRE_TRUE( catalog._listOrder.size() == 3 );
    SW_EXPECT_TRUE( catalog._listOrder[0] == hashed_string( "Second" ) );
    SW_EXPECT_TRUE( catalog._listOrder[1] == hashed_string( "Note" ) );
    SW_EXPECT_TRUE( catalog._listOrder[2] == hashed_string( "First" ) );
}

/**
 * @brief [XMLCatalogTableTest] 표 순서는 파일 안의 순서와 상관없이 앞 줄을 먼저 다 읽는다
 */
SW_TEST_CASE( XMLCatalogTableTest, TableOrderReadsEarlierRowsFirst )
{
    XMLDocument                document;
    const XMLNode              root = XMLCatalogTableTestUtil::parse( document, R"(<Root><Second/><First/><Second/></Root>)" );
    XMLCatalogTableTestCatalog catalog;

    const XMLCatalogReadResult result = XMLCatalogReadResult::readInTableOrder( catalog, XMLCatalogRoot{ root }, "test", XMLCatalogTableTestUtil::kArrElement );

    SW_EXPECT_EQUAL( 3u, result._loadedCount );
    SW_REQUIRE_TRUE( catalog._listOrder.size() == 3 );
    SW_EXPECT_TRUE( catalog._listOrder[0] == hashed_string( "First" ) );
    SW_EXPECT_TRUE( catalog._listOrder[1] == hashed_string( "Second" ) );
    SW_EXPECT_TRUE( catalog._listOrder[2] == hashed_string( "Second" ) );
}

/**
 * @brief [XMLCatalogTableTest] 표에 없는 이름은 두 순서 모두 오류이고 실패로 센다. 대소문자가 다르면 다른 이름이다
 */
SW_TEST_CASE( XMLCatalogTableTest, UnknownElementIsAnError )
{
    SW_EXPECT_LOG( "Xml.UnknownElement" );
    XMLDocument                document;
    const XMLNode              root = XMLCatalogTableTestUtil::parse( document, R"(<Root><First/><first/><Mystery/></Root>)" );
    XMLCatalogTableTestCatalog catalog;

    const XMLCatalogReadResult documentResult = XMLCatalogReadResult::readInDocumentOrder( catalog, XMLCatalogRoot{ root }, "test", XMLCatalogTableTestUtil::kArrElement );
    const XMLCatalogReadResult tableResult    = XMLCatalogReadResult::readInTableOrder( catalog, XMLCatalogRoot{ root }, "test", XMLCatalogTableTestUtil::kArrElement );

    SW_EXPECT_EQUAL( 1u, documentResult._loadedCount );
    SW_EXPECT_EQUAL( 2u, documentResult._failedCount );
    SW_EXPECT_EQUAL( 1u, tableResult._loadedCount );
    SW_EXPECT_EQUAL( 2u, tableResult._failedCount );
    SW_EXPECT_FALSE( documentResult.isClean() );
}

/**
 * @brief [XMLCatalogTableTest] 읽지 못한 원소는 세지 않고 실패로 센다. 까닭은 읽는 쪽이 남기므로 빈 파일 경고를 더하지 않는다
 */
SW_TEST_CASE( XMLCatalogTableTest, FailedElementIsCountedAsFailed )
{
    XMLDocument                document;
    const XMLNode              root = XMLCatalogTableTestUtil::parse( document, R"(<Root><Second bad="1"/></Root>)" );
    XMLCatalogTableTestCatalog catalog;

    const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( catalog, XMLCatalogRoot{ root }, "test", XMLCatalogTableTestUtil::kArrElement );

    SW_EXPECT_EQUAL( 0u, result._loadedCount );
    SW_EXPECT_EQUAL( 1u, result._failedCount );
}

/**
 * @brief [XMLCatalogTableTest] 세는 줄이 있는데 적은 것이 없으면 경고하고, 세는 줄이 없는 표는 경고하지 않는다
 */
SW_TEST_CASE( XMLCatalogTableTest, EmptyFileWarnsOnlyWhenRowsCount )
{
    SW_EXPECT_LOG( "XMLCatalog.NoEntries" );
    XMLDocument                document;
    const XMLNode              root = XMLCatalogTableTestUtil::parse( document, R"(<Root><Note/></Root>)" );
    XMLCatalogTableTestCatalog catalog;

    const XMLCatalogReadResult result = XMLCatalogReadResult::readInDocumentOrder( catalog, XMLCatalogRoot{ root }, "test", XMLCatalogTableTestUtil::kArrElement );
    SW_EXPECT_EQUAL( 0u, result._loadedCount );
    SW_EXPECT_TRUE( result.isClean() );

    const XMLCatalogReadResult uncounted = XMLCatalogReadResult::readInDocumentOrder( catalog, XMLCatalogRoot{ root }, "test", XMLCatalogTableTestUtil::kArrUncounted );
    SW_EXPECT_TRUE( uncounted.isClean() );
}
