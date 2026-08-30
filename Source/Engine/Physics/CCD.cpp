#include "pch.h"

#include "Engine/Physics/CCD.h"

#include "Core/Math/Math.h"

namespace sw
{
	bool CCD::sweepAABB( const AABB& movingBox, const float3& displacement, const AABB& targetBox, SweepHit& outHit )
	{
		const float3 movingHalfExtents{
			( movingBox._max._x - movingBox._min._x ) * 0.5f,
			( movingBox._max._y - movingBox._min._y ) * 0.5f,
			( movingBox._max._z - movingBox._min._z ) * 0.5f };

		const float3 movingCenter{
			( movingBox._min._x + movingBox._max._x ) * 0.5f,
			( movingBox._min._y + movingBox._max._y ) * 0.5f,
			( movingBox._min._z + movingBox._max._z ) * 0.5f };

		const AABB expanded{
			float3{targetBox._min._x - movingHalfExtents._x, targetBox._min._y - movingHalfExtents._y, targetBox._min._z - movingHalfExtents._z},
			float3{targetBox._max._x + movingHalfExtents._x, targetBox._max._y + movingHalfExtents._y, targetBox._max._z + movingHalfExtents._z}
		   };

		if ( expanded.contains( movingCenter ) )
		{
			outHit._bHit	  = true;
			outHit._time	  = 0.0f;
			outHit._hitPoint  = movingCenter;
			outHit._hitNormal = float3{ 0.0f, 1.0f, 0.0f };
			return true;
		}

		float32 tNear = -MathUtil::MaxFloat;
		float32 tFar  = MathUtil::MaxFloat;
		float3	nearNormal{ 0.0f, 0.0f, 0.0f };

		// X-axis slab
		if ( MathUtil::abs( displacement._x ) < 1e-7f )
		{
			if ( movingCenter._x < expanded._min._x || movingCenter._x > expanded._max._x )
				return false;
		}
		else
		{
			const float32 invDx = 1.0f / displacement._x;
			float32		  t1	= ( expanded._min._x - movingCenter._x ) * invDx;
			float32		  t2	= ( expanded._max._x - movingCenter._x ) * invDx;
			float3		  n1{ -1.0f, 0.0f, 0.0f };
			float3		  n2{ 1.0f, 0.0f, 0.0f };

			if ( t1 > t2 )
			{
				std::swap( t1, t2 );
				std::swap( n1, n2 );
			}

			if ( t1 > tNear )
			{
				tNear	   = t1;
				nearNormal = n1;
			}
			tFar = MathUtil::min( tFar, t2 );

			if ( tNear > tFar || tFar < 0.0f )
				return false;
		}

		// Y-axis slab
		if ( MathUtil::abs( displacement._y ) < 1e-7f )
		{
			if ( movingCenter._y < expanded._min._y || movingCenter._y > expanded._max._y )
				return false;
		}
		else
		{
			const float32 invDy = 1.0f / displacement._y;
			float32		  t1	= ( expanded._min._y - movingCenter._y ) * invDy;
			float32		  t2	= ( expanded._max._y - movingCenter._y ) * invDy;
			float3		  n1{ 0.0f, -1.0f, 0.0f };
			float3		  n2{ 0.0f, 1.0f, 0.0f };

			if ( t1 > t2 )
			{
				std::swap( t1, t2 );
				std::swap( n1, n2 );
			}

			if ( t1 > tNear )
			{
				tNear	   = t1;
				nearNormal = n1;
			}
			tFar = MathUtil::min( tFar, t2 );

			if ( tNear > tFar || tFar < 0.0f )
				return false;
		}

		// Z-axis slab
		if ( MathUtil::abs( displacement._z ) < 1e-7f )
		{
			if ( movingCenter._z < expanded._min._z || movingCenter._z > expanded._max._z )
				return false;
		}
		else
		{
			const float32 invDz = 1.0f / displacement._z;
			float32		  t1	= ( expanded._min._z - movingCenter._z ) * invDz;
			float32		  t2	= ( expanded._max._z - movingCenter._z ) * invDz;
			float3		  n1{ 0.0f, 0.0f, -1.0f };
			float3		  n2{ 0.0f, 0.0f, 1.0f };

			if ( t1 > t2 )
			{
				std::swap( t1, t2 );
				std::swap( n1, n2 );
			}

			if ( t1 > tNear )
			{
				tNear	   = t1;
				nearNormal = n1;
			}
			tFar = MathUtil::min( tFar, t2 );

			if ( tNear > tFar || tFar < 0.0f )
				return false;
		}

		if ( 0.0f <= tNear && tNear <= 1.0f )
		{
			outHit._bHit	 = true;
			outHit._time	 = tNear;
			outHit._hitPoint = float3{
				movingCenter._x + displacement._x * tNear,
				movingCenter._y + displacement._y * tNear,
				movingCenter._z + displacement._z * tNear };
			outHit._hitNormal = nearNormal;
			return true;
		}

		return false;
	}

	bool CCD::sweepSphere( const float3& startCenter, float32 radius, const float3& displacement, const AABB& targetBox, SweepHit& outHit )
	{
		outHit = SweepHit{};

		// 1) 초기 오버랩 검사 (t = 0)
		const float3 initialClosest{
			MathUtil::clamp( startCenter._x, targetBox._min._x, targetBox._max._x ),
			MathUtil::clamp( startCenter._y, targetBox._min._y, targetBox._max._y ),
			MathUtil::clamp( startCenter._z, targetBox._min._z, targetBox._max._z ) };

		const float3  toCenterInitial{ startCenter._x - initialClosest._x, startCenter._y - initialClosest._y, startCenter._z - initialClosest._z };
		const float32 distSqInitial = toCenterInitial._x * toCenterInitial._x + toCenterInitial._y * toCenterInitial._y + toCenterInitial._z * toCenterInitial._z;
		if ( distSqInitial <= radius * radius )
		{
			outHit._bHit	  = true;
			outHit._time	  = 0.0f;
			outHit._hitPoint  = initialClosest;
			const float32 len = MathUtil::sqrt( distSqInitial );
			outHit._hitNormal = len > 0.0001f ? float3{ toCenterInitial._x / len, toCenterInitial._y / len, toCenterInitial._z / len } : float3{ 0.0f, 1.0f, 0.0f };
			return true;
		}

		// 2) 변위 벡터가 0에 가까우면 추가 스윕 불필요
		const float32 dispLenSq = displacement._x * displacement._x + displacement._y * displacement._y + displacement._z * displacement._z;
		if ( dispLenSq < 0.000001f )
			return false;

		// 3) 확장 AABB (TargetBox + Radius) 에 대한 슬랩 스윕
		const AABB expandedBox{
			float3{targetBox._min._x - radius, targetBox._min._y - radius, targetBox._min._z - radius},
			float3{targetBox._max._x + radius, targetBox._max._y + radius, targetBox._max._z + radius}
		 };

		float32 tNear = -MathUtil::MaxFloat;
		float32 tFar  = MathUtil::MaxFloat;
		float3	nearNormal{ 0.0f, 0.0f, 0.0f };

		// X slab
		if ( MathUtil::abs( displacement._x ) < 1e-7f )
		{
			if ( startCenter._x < expandedBox._min._x || startCenter._x > expandedBox._max._x )
				return false;
		}
		else
		{
			const float32 invDx = 1.0f / displacement._x;
			float32		  t1	= ( expandedBox._min._x - startCenter._x ) * invDx;
			float32		  t2	= ( expandedBox._max._x - startCenter._x ) * invDx;
			float3		  n1{ -1.0f, 0.0f, 0.0f };
			float3		  n2{ 1.0f, 0.0f, 0.0f };

			if ( t1 > t2 )
			{
				std::swap( t1, t2 );
				std::swap( n1, n2 );
			}

			if ( t1 > tNear )
			{
				tNear	   = t1;
				nearNormal = n1;
			}
			tFar = MathUtil::min( tFar, t2 );

			if ( tNear > tFar || tFar < 0.0f )
				return false;
		}

		// Y slab
		if ( MathUtil::abs( displacement._y ) < 1e-7f )
		{
			if ( startCenter._y < expandedBox._min._y || startCenter._y > expandedBox._max._y )
				return false;
		}
		else
		{
			const float32 invDy = 1.0f / displacement._y;
			float32		  t1	= ( expandedBox._min._y - startCenter._y ) * invDy;
			float32		  t2	= ( expandedBox._max._y - startCenter._y ) * invDy;
			float3		  n1{ 0.0f, -1.0f, 0.0f };
			float3		  n2{ 0.0f, 1.0f, 0.0f };

			if ( t1 > t2 )
			{
				std::swap( t1, t2 );
				std::swap( n1, n2 );
			}

			if ( t1 > tNear )
			{
				tNear	   = t1;
				nearNormal = n1;
			}
			tFar = MathUtil::min( tFar, t2 );

			if ( tNear > tFar || tFar < 0.0f )
				return false;
		}

		// Z slab
		if ( MathUtil::abs( displacement._z ) < 1e-7f )
		{
			if ( startCenter._z < expandedBox._min._z || startCenter._z > expandedBox._max._z )
				return false;
		}
		else
		{
			const float32 invDz = 1.0f / displacement._z;
			float32		  t1	= ( expandedBox._min._z - startCenter._z ) * invDz;
			float32		  t2	= ( expandedBox._max._z - startCenter._z ) * invDz;
			float3		  n1{ 0.0f, 0.0f, -1.0f };
			float3		  n2{ 0.0f, 0.0f, 1.0f };

			if ( t1 > t2 )
			{
				std::swap( t1, t2 );
				std::swap( n1, n2 );
			}

			if ( t1 > tNear )
			{
				tNear	   = t1;
				nearNormal = n1;
			}
			tFar = MathUtil::min( tFar, t2 );

			if ( tNear > tFar || tFar < 0.0f )
				return false;
		}

		if ( tNear < 0.0f || tNear > 1.0f )
			return false;

		// 4) 충돌 시점의 구 중심 및 타겟 박스 최근접점 검증
		const float3 sphereCenterAtHit{
			startCenter._x + displacement._x * tNear,
			startCenter._y + displacement._y * tNear,
			startCenter._z + displacement._z * tNear };

		const float3 closestOnBox{
			MathUtil::clamp( sphereCenterAtHit._x, targetBox._min._x, targetBox._max._x ),
			MathUtil::clamp( sphereCenterAtHit._y, targetBox._min._y, targetBox._max._y ),
			MathUtil::clamp( sphereCenterAtHit._z, targetBox._min._z, targetBox._max._z ) };

		const float3  toCenter{ sphereCenterAtHit._x - closestOnBox._x, sphereCenterAtHit._y - closestOnBox._y, sphereCenterAtHit._z - closestOnBox._z };
		const float32 distSqHit = toCenter._x * toCenter._x + toCenter._y * toCenter._y + toCenter._z * toCenter._z;

		if ( distSqHit <= ( radius * radius + 0.01f ) )
		{
			outHit._bHit	  = true;
			outHit._time	  = tNear;
			outHit._hitPoint  = closestOnBox;
			const float32 len = MathUtil::sqrt( distSqHit );
			outHit._hitNormal = len > 0.0001f ? float3{ toCenter._x / len, toCenter._y / len, toCenter._z / len } : nearNormal;
			return true;
		}

		// Edge/Corner 영역 광선-구체 2차 보정
		const float3  rayToClosest{ closestOnBox._x - startCenter._x, closestOnBox._y - startCenter._y, closestOnBox._z - startCenter._z };
		const float32 dotVal	  = rayToClosest._x * displacement._x + rayToClosest._y * displacement._y + rayToClosest._z * displacement._z;
		const float32 proj		  = dotVal / dispLenSq;
		const float32 clampedProj = MathUtil::clamp( proj, 0.0f, 1.0f );
		const float3  closestPointOnRay{
			startCenter._x + displacement._x * clampedProj,
			startCenter._y + displacement._y * clampedProj,
			startCenter._z + displacement._z * clampedProj };

		const float3  diff{ closestPointOnRay._x - closestOnBox._x, closestPointOnRay._y - closestOnBox._y, closestPointOnRay._z - closestOnBox._z };
		const float32 edgeDistSq = diff._x * diff._x + diff._y * diff._y + diff._z * diff._z;
		if ( edgeDistSq <= radius * radius && clampedProj <= 1.0f )
		{
			outHit._bHit	  = true;
			outHit._time	  = clampedProj;
			outHit._hitPoint  = closestOnBox;
			const float32 len = MathUtil::sqrt( edgeDistSq );
			outHit._hitNormal = len > 0.0001f ? float3{ diff._x / len, diff._y / len, diff._z / len } : nearNormal;
			return true;
		}

		return false;
	}
} // namespace sw
