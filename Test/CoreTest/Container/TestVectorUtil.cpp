#include "pch.h"

#include "Core/Container/CompareUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/vector.h"

#include "TestFramework/TestFramework.h"

namespace
{
    struct KeyedItem
    {
        int32 _id;
        int32 _layer;
        int32 _order;

        int32 getDoubleID() const { return _id * 2; }
        bool  operator==( const KeyedItem& other ) const { return _id == other._id && _layer == other._layer && _order == other._order; }
    };
} // namespace

/**
 * @brief [VectorUtilTest] findBy 는 키가 같은 첫 원소의 주소를, 없으면 nullptr 를 돌려준다(데이터 멤버 · const 멤버 함수 둘 다)
 */
SW_TEST_CASE( VectorUtilTest, FindByReturnsFirstMatchOrNull )
{
    sw::vector<KeyedItem> listItem{
        {1, 0, 0},
        {2, 0, 1},
        {2, 0, 2}
    };

    KeyedItem* pFound = sw::VectorUtil::findBy<&KeyedItem::_id>( listItem, 2 );
    SW_EXPECT_TRUE( pFound == &listItem[1] );
    SW_EXPECT_TRUE( sw::VectorUtil::findBy<&KeyedItem::_id>( listItem, 9 ) == nullptr );
    SW_EXPECT_TRUE( sw::VectorUtil::findBy<&KeyedItem::getDoubleID>( listItem, 2 ) == &listItem[0] );

    const sw::vector<KeyedItem>& constList  = listItem;
    const KeyedItem*             pConstItem = sw::VectorUtil::findBy<&KeyedItem::_order>( constList, 2 );
    SW_EXPECT_TRUE( pConstItem == &listItem[2] );
}

/**
 * @brief [VectorUtilTest] 원소가 원시 포인터 · unique_ptr 이면 findBy 는 가리키는 객체의 포인터를 돌려준다
 */
SW_TEST_CASE( VectorUtilTest, FindByThroughPointerElements )
{
    KeyedItem                             first{ 1, 0, 0 };
    KeyedItem                             second{ 2, 0, 0 };
    sw::vector<KeyedItem*>                listPointer{ &first, &second };
    sw::vector<sw::unique_ptr<KeyedItem>> listOwned;
    listOwned.push_back( sw::make_unique<KeyedItem>( KeyedItem{ 7, 0, 0 } ) );

    SW_EXPECT_TRUE( sw::VectorUtil::findBy<&KeyedItem::_id>( listPointer, 2 ) == &second );
    SW_EXPECT_TRUE( sw::VectorUtil::findBy<&KeyedItem::_id>( listOwned, 7 ) == listOwned[0].get() );
    SW_EXPECT_TRUE( sw::VectorUtil::findBy<&KeyedItem::_id>( listOwned, 8 ) == nullptr );
}

/**
 * @brief [VectorUtilTest] findIndexBy 는 첫 일치의 인덱스를, 없으면 빈 값을 돌려주고 containsBy 는 그 유무다
 */
SW_TEST_CASE( VectorUtilTest, FindIndexByAndContainsBy )
{
    const sw::vector<KeyedItem> listItem{
        {5, 0, 0},
        {6, 0, 0},
        {6, 1, 0}
    };

    const std::optional<size_t> index = sw::VectorUtil::findIndexBy<&KeyedItem::_id>( listItem, 6 );
    SW_EXPECT_TRUE( index.has_value() );
    SW_EXPECT_EQUAL( size_t{ 1 }, *index );
    SW_EXPECT_FALSE( sw::VectorUtil::findIndexBy<&KeyedItem::_id>( listItem, 4 ).has_value() );
    SW_EXPECT_TRUE( sw::VectorUtil::containsBy<&KeyedItem::_layer>( listItem, 1 ) );
    SW_EXPECT_FALSE( sw::VectorUtil::containsBy<&KeyedItem::_layer>( listItem, 2 ) );
}

/**
 * @brief [VectorUtilTest] removeIf 는 조건에 맞는 원소를 모두 지우고 남은 순서를 지키며 지운 개수를 돌려준다
 */
SW_TEST_CASE( VectorUtilTest, RemoveIfKeepsOrderAndCounts )
{
    sw::vector<int32> list{ 1, 2, 3, 4, 5, 6 };
    const size_t      removeCount = sw::VectorUtil::removeIf( list, []( int32 value )
         { return value % 2 == 0; } );
    SW_EXPECT_EQUAL( size_t{ 3 }, removeCount );
    SW_EXPECT_EQUAL( size_t{ 3 }, list.size() );
    SW_EXPECT_EQUAL( 1, list[0] );
    SW_EXPECT_EQUAL( 3, list[1] );
    SW_EXPECT_EQUAL( 5, list[2] );
    SW_EXPECT_EQUAL( size_t{ 0 }, sw::VectorUtil::removeIf( list, []( int32 value )
    { return value > 100; } ) );
}

/**
 * @brief [VectorUtilTest] sortUnique 는 정렬한 뒤 같은 이웃을 하나만 남기고, 비교자를 주면 그 비교자로 같음을 판단한다
 */
SW_TEST_CASE( VectorUtilTest, SortUniqueDropsDuplicates )
{
    sw::vector<int32> list{ 3, 1, 3, 2, 1 };
    sw::VectorUtil::sortUnique( list );
    SW_EXPECT_EQUAL( size_t{ 3 }, list.size() );
    SW_EXPECT_EQUAL( 1, list[0] );
    SW_EXPECT_EQUAL( 2, list[1] );
    SW_EXPECT_EQUAL( 3, list[2] );

    sw::vector<KeyedItem> listItem{
        {2, 0, 9},
        {1, 0, 0},
        {2, 1, 5}
    };
    sw::VectorUtil::sortUnique( listItem, sw::Ascending<&KeyedItem::_id>{} );
    SW_EXPECT_EQUAL( size_t{ 2 }, listItem.size() );
    SW_EXPECT_EQUAL( 1, listItem[0]._id );
    SW_EXPECT_EQUAL( 2, listItem[1]._id );
}

/**
 * @brief [CompareUtilTest] Ascending · Descending 은 멤버 키로 비교하고, 키를 여럿 주면 앞 키가 같을 때만 다음 키를 본다
 */
SW_TEST_CASE( CompareUtilTest, AscendingAndDescendingByMember )
{
    const KeyedItem low{ 1, 2, 0 };
    const KeyedItem high{ 1, 3, 0 };

    SW_EXPECT_TRUE( ( sw::Ascending<&KeyedItem::_layer>{}( low, high ) ) );
    SW_EXPECT_FALSE( ( sw::Ascending<&KeyedItem::_layer>{}( high, low ) ) );
    SW_EXPECT_FALSE( ( sw::Ascending<&KeyedItem::_id>{}( low, high ) ) ); // 같으면 앞이 아니다
    SW_EXPECT_TRUE( ( sw::Descending<&KeyedItem::_layer>{}( high, low ) ) );
    SW_EXPECT_TRUE( ( sw::Ascending<&KeyedItem::_id, &KeyedItem::_layer>{}( low, high ) ) );
    SW_EXPECT_TRUE( ( sw::Descending<&KeyedItem::_id, &KeyedItem::_layer>{}( high, low ) ) );

    const KeyedItem* pLow  = &low;
    const KeyedItem* pHigh = &high;
    SW_EXPECT_TRUE( ( sw::Ascending<&KeyedItem::getDoubleID, &KeyedItem::_layer>{}( pLow, pHigh ) ) ); // 포인터 원소 · 멤버 함수 키
}

/**
 * @brief [CompareUtilTest] Lexicographic 은 앞 비교자가 같다고 볼 때만 다음 비교자를 본다(방향이 섞인 정렬)
 */
SW_TEST_CASE( CompareUtilTest, LexicographicMixesDirections )
{
    sw::vector<KeyedItem> listItem{
        {1, 0, 1},
        {2, 0, 5},
        {3, 0, 5},
        {4, 0, 1}
    };
    std::sort( listItem.begin(), listItem.end(), sw::Lexicographic<sw::Descending<&KeyedItem::_order>, sw::Ascending<&KeyedItem::_id>>{} );
    SW_EXPECT_EQUAL( 2, listItem[0]._id );
    SW_EXPECT_EQUAL( 3, listItem[1]._id );
    SW_EXPECT_EQUAL( 1, listItem[2]._id );
    SW_EXPECT_EQUAL( 4, listItem[3]._id );
}
