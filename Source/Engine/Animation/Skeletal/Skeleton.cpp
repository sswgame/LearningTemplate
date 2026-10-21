#include "pch.h"

#include "Engine/Animation/Skeletal/Skeleton.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Serialization/Format/JSONSerializer.h"

namespace sw
{
    SW_LOG_CALLER( "Skeleton" );

    int32 Skeleton::addBone( const hashed_string& name, int32 parentIndex, const BoneTransform& referencePose, const float4x4& inverseBind )
    {
        // 부모가 자식보다 뒤에 있으면 모델 공간을 한 번 훑어서 구할 수 없다. 읽을 때 걸러 낸다.
        const int32 boneCount = static_cast<int32>( _listBone.size() );
        if ( parentIndex < -1 || boneCount <= parentIndex )
        {
            SW_LOG_ERROR( "Bone '%#' rejected: parent index %# is not an already-added bone (bone count %#)", name.c_str(), parentIndex, boneCount );
            return -1;
        }

        SkeletonBone bone{};
        bone._name          = name;
        bone._parentIndex   = parentIndex;
        bone._referencePose = referencePose;
        bone._inverseBind   = inverseBind;
        _listBone.push_back( bone );
        _listParentIndex.push_back( parentIndex );
        return boneCount;
    }

    void Skeleton::computeInverseBindFromReference()
    {
        Pose referencePose;
        referencePose.setToReference( *this );
        vector<float4x4> listModel;
        referencePose.computeModelSpace( _listParentIndex, listModel );
        for ( size_t boneIndex = 0; boneIndex < _listBone.size(); ++boneIndex )
        {
            _listBone[boneIndex]._inverseBind = listModel[boneIndex].invert();
        }
    }

    int32 Skeleton::findBoneIndex( const hashed_string& name ) const
    {
        for ( size_t boneIndex = 0; boneIndex < _listBone.size(); ++boneIndex )
        {
            if ( _listBone[boneIndex]._name == name )
                return static_cast<int32>( boneIndex );
        }
        return -1;
    }

    void Skeleton::clear()
    {
        _listBone.clear();
        _listParentIndex.clear();
        _listAttachment.clear();
    }

    bool Skeleton::parseJSON( string_view json, string_view sourceLabel )
    {
        clear();
        const string label{ sourceLabel };
        // 키는 대소문자까지 맞아야 하고, 모르는 키 · 빠진 키 · 읽지 못한 값은 오류다(`readConfigJSON` 이 로그로 알린다).
        if ( ConfigManager::readConfigJSON( *this, json, label.c_str() ) && rebuildFromLoaded( sourceLabel ) )
            return true;
        clear();
        return false;
    }

    bool Skeleton::loadFromResource( string_view path )
    {
        SW_MEMORY_SCOPE( Animation );
        string text;
        if ( ResourceIO::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "Skeleton '%#' could not be read", path );
            clear();
            return false;
        }
        return parseJSON( text, path );
    }

    bool Skeleton::rebuildFromLoaded( string_view sourceLabel )
    {
        if ( _listBone.empty() )
        {
            SW_LOG_ERROR( "Skeleton '%#': _listBone must not be empty", sourceLabel );
            return false;
        }
        // 부모가 자식보다 뒤에 있으면 모델 공간을 한 번 훑어서 구할 수 없다. `addBone` 이 거르므로 그 길로 다시 쌓는다.
        const vector<SkeletonBone>       listLoadedBone       = std::move( _listBone );
        const vector<SkeletonAttachment> listLoadedAttachment = std::move( _listAttachment );
        clear();
        for ( const SkeletonBone& bone : listLoadedBone )
        {
            if ( addBone( bone._name, bone._parentIndex, bone._referencePose, bone._inverseBind ) < 0 )
            {
                SW_LOG_ERROR( "Skeleton '%#': bone '%#' must name an earlier bone as its parent", sourceLabel, bone._name.c_str() );
                return false;
            }
        }
        for ( const SkeletonAttachment& attachment : listLoadedAttachment )
        {
            if ( findBoneIndex( attachment._parentBone ) < 0 )
            {
                SW_LOG_ERROR( "Skeleton '%#': attachment '%#' names unknown bone '%#'", sourceLabel, attachment._name.c_str(), attachment._parentBone.c_str() );
                return false;
            }
            _listAttachment.push_back( attachment );
        }
        return true;
    }

    string Skeleton::toJSON() const
    {
        return JSONSerializer::serializePretty( this, *StaticType(), 2 );
    }

    bool Skeleton::saveToFile( string_view path ) const
    {
        if ( FileUtil::ensureParentDirectoryExists( path ) == false )
            return false;
        return FileUtil::writeTextFile( path, toJSON() );
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( Skeleton, Skeleton::kAssetType );
} // namespace sw
