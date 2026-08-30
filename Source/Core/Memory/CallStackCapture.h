/**
 * @file CallStackCapture.h
 * @brief 크로스플랫폼 콜 스택 캡처 및 심볼 변환 유틸리티
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) CallStack — 프레임 포인터 배열과 해시
	// ------------------------------------------------------------------------------
	/**
	 * @brief 캡처된 프레임 주소와 해시입니다. 심볼화는 CallStackCapture::symbolize.
	 */
	struct SW_API CallStack
	{
		static constexpr uint32 kMaxFrames			  = 16;
		void*					_arrFrame[kMaxFrames] = { nullptr };
		uint64					_hash{ 0 };
		uint32					_frameCount{ 0 };

		/** @brief 해시·프레임 수·주소가 모두 같으면 true 입니다. */
		bool operator==( const CallStack& other ) const
		{
			if ( _hash != other._hash || _frameCount != other._frameCount )
				return false;
			for ( uint32 frameIndex = 0; frameIndex < _frameCount; ++frameIndex )
			{
				if ( _arrFrame[frameIndex] != other._arrFrame[frameIndex] )
					return false;
			}
			return true;
		}

		/** @brief 해시 또는 프레임이 다르면 true 입니다. */
		bool operator!=( const CallStack& other ) const { return !( *this == other ); }
	};

} // namespace sw

namespace std
{
	/** @brief CallStack 을 unordered_map 키로 쓸 때 해시는 stack._hash 입니다. */
	template <>
	struct hash<sw::CallStack>
	{
		/** @brief 미리 계산된 스택 해시를 size_t 로 돌려줍니다. */
		size_t operator()( const sw::CallStack& stack ) const { return stack._hash; }
	};
} // namespace std

namespace sw
{
	// ------------------------------------------------------------------------------
	// 2) CallStackCapture — initialize → capture → symbolize → shutdown
	//    initialize 에서 심볼을 로드합니다
	// ------------------------------------------------------------------------------
	/** @brief 현재 스레드 스택을 캡처하고 심볼 문자열로 바꿉니다. */
	class SW_API CallStackCapture
	{
	public:
		/**
		 * @brief 심볼 핸들을 로드합니다. capture 전에 한 번 호출합니다.
		 */
		static void initialize();

		/**
		 * @brief 심볼 핸들을 닫습니다.
		 */
		static void shutdown();

		/**
		 * @brief 현재 스레드의 콜 스택을 캡처합니다.
		 * @param outStack 캡처된 콜 스택을 저장할 구조체
		 * @param skipFrames 캡처를 건너뛸 상단 프레임 수
		 */
		static void capture( CallStack& outStack, uint32 skipFrames = 1 );

		/**
		 * @brief 캡처된 프레임을 심볼·파일·라인 문자열로 바꿉니다.
		 * @param stack 변환할 콜 스택
		 * @return 콜 스택 문자열
		 */
		static string symbolize( const CallStack& stack );
	};
} // namespace sw
