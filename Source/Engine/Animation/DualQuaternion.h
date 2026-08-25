#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
	/**
	 * @brief 3D 강체 변환(회전 + 이동)을 표현하는 듀얼 쿼터니언. 캔디랩퍼 왜곡 없는 스키닝에 사용됩니다.
	 */
	struct SW_API DualQuaternion
	{
		quaternion _real{ 0.0f, 0.0f, 0.0f, 1.0f };
		quaternion _dual{ 0.0f, 0.0f, 0.0f, 0.0f };

		DualQuaternion() = default;
		DualQuaternion( const quaternion& r, const float3& t );
		DualQuaternion( const quaternion& real, const quaternion& dual );

		static DualQuaternion fromTransform( const float3& translation, const quaternion& rotation );
		static DualQuaternion fromMatrix( const float4x4& mat );

		void		   normalize();
		DualQuaternion normalized() const;

		float3	   getTranslation() const;
		quaternion getRotation() const;
		float4x4   toMatrix4x4() const;

		static DualQuaternion dlb( const DualQuaternion& a, const DualQuaternion& b, float32 t );
	};
} // namespace sw
