#include "pch.h"

#include "Core/Container/vector.h"

#include "RuntimeAPI/EditorAPI.h"
#include "RuntimeAPI/GameAPI.h"

#include "TestFramework/TestFramework.h"

namespace
{
	struct TrackedValue
	{
		static int32 s_liveCount;

		int32 _value;

		TrackedValue( int32 value = 0 )
			: _value{ value }
		{
			++s_liveCount;
		}

		TrackedValue( const TrackedValue& other )
			: _value{ other._value }
		{
			++s_liveCount;
		}

		TrackedValue( TrackedValue&& other ) noexcept
			: _value{ other._value }
		{
			++s_liveCount;
		}

		~TrackedValue()
		{
			--s_liveCount;
		}

		TrackedValue& operator=( const TrackedValue& other )
		{
			_value = other._value;
			return *this;
		}

		TrackedValue& operator=( TrackedValue&& other ) noexcept
		{
			_value = other._value;
			return *this;
		}
	};

	int32 TrackedValue::s_liveCount{ 0 };
} // namespace

/**
 * @brief [Core_Vector] small_vector가 인라인 저장소와 힙 저장소 사이를 안전하게 이동한다.
 */
SW_TEST_CASE( Core_Vector, SmallVectorStorageTransition )
{
	sw::small_vector<int32, 2> listValues{};
	const int32* const		   pInlineData = listValues.data();

	listValues.push_back( 10 );
	listValues.push_back( 20 );
	SW_EXPECT_TRUE( listValues.data() == pInlineData );

	listValues.push_back( 30 );
	SW_EXPECT_TRUE( listValues.data() != pInlineData );
	SW_EXPECT_EQUAL( 3u, listValues.size() );
	SW_EXPECT_EQUAL( 10, listValues[0] );
	SW_EXPECT_EQUAL( 30, listValues[2] );

	listValues.pop_back();
	listValues.shrink_to_fit();
	SW_EXPECT_TRUE( listValues.data() == pInlineData );
	SW_EXPECT_EQUAL( 2u, listValues.size() );
}

/**
 * @brief [Core_Vector] 복사·이동·삽입·삭제가 비트리비얼 타입의 수명을 보존한다.
 */
SW_TEST_CASE( Core_Vector, ValueLifetimeAndMutation )
{
	{
		sw::vector<TrackedValue> listValues{};
		listValues.emplace_back( 10 );
		listValues.emplace_back( 30 );
		listValues.insert( listValues.begin() + 1, TrackedValue{ 20 } );
		listValues.erase( listValues.begin() );

		sw::vector<TrackedValue> copiedValues{ listValues };
		sw::vector<TrackedValue> movedValues{ std::move( copiedValues ) };
		SW_EXPECT_EQUAL( 2u, movedValues.size() );
		SW_EXPECT_EQUAL( 20, movedValues[0]._value );
		SW_EXPECT_EQUAL( 30, movedValues[1]._value );
	}

	SW_EXPECT_EQUAL( 0, TrackedValue::s_liveCount );
}

/**
 * @brief [Core_RuntimeAPI] 모듈 C-ABI는 0이 아닌 명시적 버전을 사용한다.
 */
SW_TEST_CASE( Core_RuntimeAPI, AbiVersionsAreExplicit )
{
	SW_EXPECT_TRUE( sw::kGameAPIAbiVersion > 0 );
	SW_EXPECT_TRUE( sw::kEditorAPIAbiVersion > 0 );

	sw::GameAPI	  gameApi{};
	sw::EditorAPI editorApi{};
	SW_EXPECT_EQUAL( 0u, gameApi._abiVersion );
	SW_EXPECT_EQUAL( 0u, editorApi._abiVersion );
}
