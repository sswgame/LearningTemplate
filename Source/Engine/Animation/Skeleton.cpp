#include "pch.h"

#include "Engine/Animation/Skeleton.h"

namespace sw
{
	int32 Skeleton::addBone( string_view name, int32 parentIndex, const float4x4& invReferencePose, const float4x4& boneSpaceTransform )
	{
		Bone bone{};
		bone._name					  = string{ name };
		bone._parentIndex			  = parentIndex;
		bone._invReferencePose		  = invReferencePose;
		bone._boneSpaceTransform	  = boneSpaceTransform;
		bone._characterSpaceTransform = boneSpaceTransform;

		const int32 newIndex = static_cast<int32>( _listBones.size() );
		_listBones.push_back( std::move( bone ) );
		_listSkinningMatrices.push_back( float4x4::Identity );
		return newIndex;
	}

	int32 Skeleton::findBoneIndex( string_view name ) const
	{
		for ( size_t index = 0; index < _listBones.size(); ++index )
		{
			if ( _listBones[index]._name == name )
				return static_cast<int32>( index );
		}
		return -1;
	}

	void Skeleton::setBoneSpaceTransform( int32 boneIndex, const float4x4& boneSpaceTransform )
	{
		if ( boneIndex >= 0 && static_cast<size_t>( boneIndex ) < _listBones.size() )
			_listBones[static_cast<size_t>( boneIndex )]._boneSpaceTransform = boneSpaceTransform;
	}

	void Skeleton::updateCharacterSpaceTransforms()
	{
		for ( size_t index = 0; index < _listBones.size(); ++index )
		{
			Bone& bone = _listBones[index];
			if ( bone._parentIndex >= 0 && static_cast<size_t>( bone._parentIndex ) < index )
			{
				bone._characterSpaceTransform = _listBones[static_cast<size_t>( bone._parentIndex )]._characterSpaceTransform * bone._boneSpaceTransform;
			}
			else
			{
				bone._characterSpaceTransform = bone._boneSpaceTransform;
			}

			_listSkinningMatrices[index] = bone._characterSpaceTransform * bone._invReferencePose;
		}
	}

	void Skeleton::clear()
	{
		_listBones.clear();
		_listSkinningMatrices.clear();
	}
} // namespace sw
