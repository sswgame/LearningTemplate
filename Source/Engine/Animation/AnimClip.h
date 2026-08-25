/**
 * @file AnimClip.h
 * @brief 최소 애니메이션 클립 샘플 스텁.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Math/MatrixMath.h"

namespace sw
{
	/** @brief 지정 시각에 클립을 샘플한 결과입니다. */
	struct AnimSample
	{
		float32	 _weight{ 0.0f };
		float4x4 _transform{};
	};

	/**
	 * @class AnimClip
	 * @brief 이름과 길이를 가진 클립. sample()은 스텁 weight/transform을 반환합니다.
	 */
	class SW_API AnimClip
	{
	public:
		/** @brief 기본 클립입니다. */
		AnimClip() = default;
		/** @brief 이름과 길이로 클립을 만듭니다. */
		AnimClip( string_view name, float32 durationSeconds );

		/** @brief 클립 이름을 반환합니다. */
		const string& getName() const { return _name; }
		/** @brief 클립 길이(초)를 반환합니다. */
		float32 getDuration() const { return _durationSeconds; }

		/** @brief 클립 이름을 설정합니다. */
		void setName( const string& name ) { _name = std::move( name ); }
		/** @brief 클립 길이(초)를 설정합니다. */
		void setDuration( float32 durationSeconds );

		/**
		 * @brief @p timeSeconds에서 클립을 샘플합니다 (루프면 래핑).
		 * @details 스텁: weight = [0,1] 정규화 시간, transform = 항등.
		 */
		AnimSample sample( float32 timeSeconds, bool bLooping = true ) const;

	private:
		string	_name;
		float32 _durationSeconds{ 1.0f };
	};
} // namespace sw
