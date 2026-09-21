#include "pch.h"

#include "Engine/Animation/Skeleton.h"

#include "Core/Log/Logger.h"

namespace sw
{
    SW_LOG_CALLER( "Skeleton" );

    int32 Skeleton::addBone( string_view name, int32 parentIndex, const float4x4& invReferencePose, const float4x4& boneSpaceTransform )
    {
        // 부모가 자식보다 뒤에 있으면 updateCharacterSpaceTransforms 가 그 본을 루트로 취급해
        // 계층을 통째로 잃는다 — 로그도 없이. 들어오는 자리에서 막는다.
        const int32 boneCount = static_cast<int32>( _listBone.size() );
        if ( parentIndex < -1 || boneCount <= parentIndex )
        {
            SW_LOG_ERROR( "Bone '%#' rejected: parent index %# is not an already-added bone (bone count %#)", name, parentIndex, boneCount );
            return -1;
        }

        Bone bone{};
        bone._name                    = hashed_string( name );
        bone._parentIndex             = parentIndex;
        bone._invReferencePose        = invReferencePose;
        bone._boneSpaceTransform      = boneSpaceTransform;
        bone._characterSpaceTransform = boneSpaceTransform;

        const int32 newIndex = boneCount;
        _listBone.push_back( bone );
        _listSkinningMatrix.push_back( float4x4::Identity );
        return newIndex;
    }

    int32 Skeleton::findBoneIndex( const hashed_string& name ) const
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
            // addBone 이 부모가 자식보다 앞에 오는 것을 보장한다 — 그래서 한 번 훑으면 끝난다.
            SW_ASSERT( bone._parentIndex < static_cast<int32>( index ) );
            if ( 0 <= bone._parentIndex && static_cast<size_t>( bone._parentIndex ) < index )
                bone._characterSpaceTransform = _listBone[static_cast<size_t>( bone._parentIndex )]._characterSpaceTransform * bone._boneSpaceTransform;
            else
                bone._characterSpaceTransform = bone._boneSpaceTransform;

            _listSkinningMatrix[index] = bone._characterSpaceTransform * bone._invReferencePose;
        }
    }

    void Skeleton::clear()
    {
        _listBone.clear();
        _listSkinningMatrix.clear();
    }
} // namespace sw
