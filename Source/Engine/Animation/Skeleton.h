#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/String/hashed_string.h"

namespace sw
{
	/**
	 * @brief 3D 스켈레탈 애니메이션의 단일 본(Bone) 노드
	 */
	struct SW_API Bone
	{
		hashed_string _name;
		int32		  _parentIndex{ -1 };
		float4x4	  _invReferencePose{ float4x4::Identity };
		float4x4	  _boneSpaceTransform{ float4x4::Identity };
		float4x4	  _characterSpaceTransform{ float4x4::Identity };
	};

	/**
	 * @brief 계층형 본 구조체와 최종 스키닝 행렬을 관리하는 스켈레톤 클래스
	 */
	class SW_API Skeleton
	{
	public:
		Skeleton()								   = default;
		~Skeleton()								   = default;
		Skeleton( const Skeleton& )				   = default;
		Skeleton& operator=( const Skeleton& )	   = default;
		Skeleton( Skeleton&& ) noexcept			   = default;
		Skeleton& operator=( Skeleton&& ) noexcept = default;

		int32 addBone( string_view name, int32 parentIndex, const float4x4& invReferencePose, const float4x4& boneSpaceTransform );
		int32 findBoneIndex( string_view name ) const;
		int32 findBoneIndex( const hashed_string& name ) const;

		void setBoneSpaceTransform( int32 boneIndex, const float4x4& boneSpaceTransform );
		void updateCharacterSpaceTransforms();

		const vector<float4x4>& getSkinningMatrices() const { return _listSkinningMatrix; }
		const vector<Bone>&		getBones() const { return _listBone; }
		size_t					getBoneCount() const { return _listBone.size(); }
		void					clear();

	private:
		vector<Bone>	 _listBone;
		vector<float4x4> _listSkinningMatrix;
	};
} // namespace sw
