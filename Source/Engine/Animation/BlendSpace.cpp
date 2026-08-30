#include "pch.h"

#include "Engine/Animation/BlendSpace.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Math/Math.h"

namespace sw
{
	void BlendSpace1D::addSample( float32 parameter, string_view clipName, const float4x4& pose )
	{
		BlendSample1D sample{};
		sample._parameter = parameter;
		sample._clipName  = string{ clipName };
		sample._pose	  = pose;

		_listSample.push_back( std::move( sample ) );

		std::sort( _listSample.begin(), _listSample.end(), []( const BlendSample1D& a, const BlendSample1D& b )
		{
			return a._parameter < b._parameter;
		} );
	}

	float4x4 BlendSpace1D::evaluate( float32 parameter ) const
	{
		if ( _listSample.empty() )
			return float4x4::Identity;

		if ( _listSample.size() == 1 || parameter <= _listSample.front()._parameter )
			return _listSample.front()._pose;

		if ( parameter >= _listSample.back()._parameter )
			return _listSample.back()._pose;

		for ( size_t index = 0; index + 1 < _listSample.size(); ++index )
		{
			const BlendSample1D& s0 = _listSample[index];
			const BlendSample1D& s1 = _listSample[index + 1];

			if ( s0._parameter <= parameter && parameter <= s1._parameter )
			{
				const float32 span = s1._parameter - s0._parameter;
				const float32 t	   = span > 1e-5f ? ( ( parameter - s0._parameter ) / span ) : 0.0f;

				const DualQuaternion dq0 = DualQuaternion::fromMatrix( s0._pose );
				const DualQuaternion dq1 = DualQuaternion::fromMatrix( s1._pose );
				const DualQuaternion dqb = DualQuaternion::dlb( dq0, dq1, t );
				return dqb.toMatrix4x4();
			}
		}

		return _listSample.back()._pose;
	}

	void BlendSpace1D::evaluateSkeleton( float32 parameter, Skeleton& inoutSkeleton ) const
	{
		const float4x4 rootTransform = evaluate( parameter );
		if ( inoutSkeleton.getBoneCount() > 0 )
		{
			inoutSkeleton.setBoneSpaceTransform( 0, rootTransform );
			inoutSkeleton.updateCharacterSpaceTransforms();
		}
	}

	void BlendSpace2D::addSample( float32 paramX, float32 paramY, string_view clipName, const float4x4& pose )
	{
		BlendSample2D sample{};
		sample._parameter = float2{ paramX, paramY };
		sample._clipName  = string{ clipName };
		sample._pose	  = pose;

		_listSample.push_back( std::move( sample ) );
	}

	float4x4 BlendSpace2D::evaluate( float32 paramX, float32 paramY ) const
	{
		if ( _listSample.empty() )
			return float4x4::Identity;

		if ( _listSample.size() == 1 )
			return _listSample.front()._pose;

		// Inverse Distance Weighting (IDW)
		float32		 totalWeight = 0.0f;
		float32		 weights[32];
		const size_t sampleCount = MathUtil::min( _listSample.size(), static_cast<size_t>( 32 ) );

		for ( size_t index = 0; index < sampleCount; ++index )
		{
			const float32 dx	 = paramX - _listSample[index]._parameter._x;
			const float32 dy	 = paramY - _listSample[index]._parameter._y;
			const float32 distSq = dx * dx + dy * dy;

			if ( distSq < 1e-5f )
				return _listSample[index]._pose;

			weights[index] = 1.0f / distSq;
			totalWeight += weights[index];
		}

		if ( totalWeight < 1e-6f )
			return _listSample.front()._pose;

		DualQuaternion accumDQ	   = DualQuaternion::fromMatrix( _listSample[0]._pose );
		float32		   accumWeight = weights[0] / totalWeight;

		for ( size_t index = 1; index < sampleCount; ++index )
		{
			const float32		 normalizedWeight = weights[index] / totalWeight;
			const float32		 blendFactor	  = normalizedWeight / ( accumWeight + normalizedWeight );
			const DualQuaternion currentDQ		  = DualQuaternion::fromMatrix( _listSample[index]._pose );

			accumDQ = DualQuaternion::dlb( accumDQ, currentDQ, blendFactor );
			accumWeight += normalizedWeight;
		}

		return accumDQ.toMatrix4x4();
	}
} // namespace sw
