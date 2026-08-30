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

		const int32 newIndex = static_cast<int32>( _listBone.size() );
		_listBone.push_back( std::move( bone ) );
		_listSkinningMatrix.push_back( float4x4::Identity );
		return newIndex;
	}

	int32 Skeleton::findBoneIndex( string_view name ) const
	{
		for ( size_t index = 0; index < _listBone.size(); ++index )
		{
			if ( _listBone[index]._name == name )
				return static_cast<int32>( index );
		}
		return -1;
	}

	void Skeleton::setBoneSpaceTransform( int32 boneIndex, const float4x4& boneSpaceTransform )
	{
		if ( 0 <= boneIndex && static_cast<size_t>( boneIndex ) < _listBone.size() )
			_listBone[static_cast<size_t>( boneIndex )]._boneSpaceTransform = boneSpaceTransform;
	}

	void Skeleton::updateCharacterSpaceTransforms()
	{
		for ( size_t index = 0; index < _listBone.size(); ++index )
		{
			Bone& bone = _listBone[index];
			if ( 0 <= bone._parentIndex && static_cast<size_t>( bone._parentIndex ) < index )
			{
				bone._characterSpaceTransform = _listBone[static_cast<size_t>( bone._parentIndex )]._characterSpaceTransform * bone._boneSpaceTransform;
			}
			else
			{
				bone._characterSpaceTransform = bone._boneSpaceTransform;
			}

			_listSkinningMatrix[index] = bone._characterSpaceTransform * bone._invReferencePose;
		}
	}

	void Skeleton::clear()
	{
		_listBone.clear();
		_listSkinningMatrix.clear();
	}
} // namespace sw
