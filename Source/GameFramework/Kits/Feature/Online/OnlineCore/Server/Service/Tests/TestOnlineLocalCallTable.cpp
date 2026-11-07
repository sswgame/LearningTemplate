#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Service/OnlineProtocolService.h"

// OnlineLocalCallTableTest — 같은 프로세스 호출의 표: 응답은 한 번이고, 번호는 호스트 호출과 겹치지 않으며, 응답 안에서 다시 올려도 된다(nogpu).

using namespace sw;

namespace
{
    struct LocalReply
    {
        int32 _value{ 0 };
    };

    using LocalCallTable = OnlineLocalCallTable<LocalReply>;

    struct LocalReplySink
    {
        vector<int32>   _listValue{};
        LocalCallTable* _pReopenTable{ nullptr }; ///< 있으면 첫 응답 안에서 호출 하나를 더 올린다

        void onReply( const LocalReply& reply )
        {
            _listValue.push_back( reply._value );
            if ( _pReopenTable != nullptr && _listValue.size() == 1 )
                (void)_pReopenTable->open( LocalCallTable::ReplyDelegate::create<&LocalReplySink::onReply>( this ) );
        }
    };
} // namespace

/** @brief [OnlineLocalCallTableTest] 응답은 한 번이고 표에서 빠진다. 모르는 번호는 false 이다. 번호는 로컬 표지가 켜져 있다 */
SW_TEST_CASE( OnlineLocalCallTableTest, CallIsAnsweredOnce )
{
    LocalCallTable table;
    LocalReplySink sink;
    const uint64   callID = table.open( LocalCallTable::ReplyDelegate::create<&LocalReplySink::onReply>( &sink ) );
    SW_EXPECT_TRUE( LocalCallTable::isLocal( callID ) );
    SW_EXPECT_FALSE( LocalCallTable::isLocal( 1 ) );
    SW_EXPECT_EQUAL( 1, table.getCount() );

    SW_EXPECT_TRUE( table.finish( callID, LocalReply{ 7 } ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), sink._listValue.size() );
    SW_EXPECT_EQUAL( 7, sink._listValue[0] );
    SW_EXPECT_TRUE( table.isEmpty() );
    SW_EXPECT_FALSE( table.finish( callID, LocalReply{ 8 } ) );
    SW_EXPECT_EQUAL( size_t( 1 ), sink._listValue.size() );
}

/** @brief [OnlineLocalCallTableTest] 응답 델리게이트 안에서 다시 올린 호출은 남는다 */
SW_TEST_CASE( OnlineLocalCallTableTest, ReplyDelegateMayOpenAnotherCall )
{
    LocalCallTable table;
    LocalReplySink sink;
    sink._pReopenTable  = &table;
    const uint64 callID = table.open( LocalCallTable::ReplyDelegate::create<&LocalReplySink::onReply>( &sink ) );
    SW_EXPECT_TRUE( table.finish( callID, LocalReply{ 1 } ) );
    SW_EXPECT_EQUAL( 1, table.getCount() );
    table.clear();
    SW_EXPECT_TRUE( table.isEmpty() );
}
