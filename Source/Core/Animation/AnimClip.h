#pragma once
/**
 * @file AnimClip.h
 * @brief Minimal animation clip sample stub.
 */
#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"
#include "Core/Utility/Math/MatrixMath.h"

namespace sw
{
	/** @brief Result of sampling a clip at a given time. */
	struct AnimSample
	{
		float32	 _weight	= 0.0f;
		float4x4 _transform{};
	};

	/**
	 * @class AnimClip
	 * @brief Named clip with duration; sample() returns a stub weight/transform.
	 */
	class SW_API AnimClip
	{
	public:
		AnimClip() = default;
		AnimClip( std::string name, float32 durationSeconds );

		const std::string& getName() const { return _name; }
		float32			   getDuration() const { return _durationSeconds; }

		void setName( std::string name ) { _name = std::move( name ); }
		void setDuration( float32 durationSeconds );

		/**
		 * @brief Sample clip at @p timeSeconds (wrapped if looping).
		 * @details Stub: weight = normalized time in [0,1]; transform = identity.
		 */
		AnimSample sample( float32 timeSeconds, bool bLooping = true ) const;

	private:
		std::string _name;
		float32		_durationSeconds = 1.0f;
	};
} // namespace sw
