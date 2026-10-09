#include "pch.h"

#include "Engine/Animation/Skeleton.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/AnimJsonUtil.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/Json/JsonDocument.h"

namespace sw
{
    SW_LOG_CALLER( "Skeleton" );

    int32 Skeleton::addBone( const hashed_string& name, int32 parentIndex, const BoneTransform& referencePose, const float4x4& inverseBind )
    {
        // 부모가 자식보다 뒤에 있으면 모델 공간을 한 번 훑어 구할 수 없다. 들어오는 자리에서 막는다.
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

    bool Skeleton::parseJson( string_view json, string_view sourceLabel )
    {
        clear();
        JsonDocument document;
        if ( document.parse( json, sourceLabel ) == false )
        {
            SW_LOG_ERROR( "Skeleton '%#': malformed JSON", sourceLabel );
            return false;
        }
        if ( parseRoot( document.getRoot(), sourceLabel ) )
            return true;
        clear();
        return false;
    }

    bool Skeleton::loadFromResource( string_view path )
    {
        SW_MEMORY_SCOPE( Animation );
        string text;
        if ( ResourceUtil::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "Skeleton '%#' could not be read", path );
            clear();
            return false;
        }
        return parseJson( text, path );
    }

    bool Skeleton::parseRoot( const JsonValue& root, string_view sourceLabel )
    {
        if ( AnimJsonUtil::hasOnlyKnownKeys( root, { "bones", "attachments" }, sourceLabel ) == false )
            return false;
        const JsonValue bones = root.get( "bones" );
        if ( bones.isArray() == false || bones.size() == 0 )
        {
            SW_LOG_ERROR( "Skeleton '%#': 'bones' must be a non-empty array", sourceLabel );
            return false;
        }
        for ( size_t boneIndex = 0; boneIndex < bones.size(); ++boneIndex )
        {
            const JsonValue bone = bones.at( boneIndex );
            if ( AnimJsonUtil::hasOnlyKnownKeys( bone, { "name", "parent", "translation", "rotation", "scale", "inverse_bind" }, sourceLabel ) == false )
                return false;
            BoneTransform referencePose{};
            float32       arrInverseBind[16]{};
            const bool    bComplete = bone.get( "name" ).isString() && bone.get( "parent" ).isNumber() && AnimJsonUtil::readBoneTransform( bone, referencePose ) &&
                                   AnimJsonUtil::readFloats( bone.get( "inverse_bind" ), arrInverseBind, 16 );
            if ( bComplete == false )
            {
                SW_LOG_ERROR( "Skeleton '%#': bone %# is missing a field or has a malformed one", sourceLabel, boneIndex );
                return false;
            }
            const string name        = bone.get( "name" ).asString();
            const int32  parentIndex = static_cast<int32>( bone.get( "parent" ).asInt( -2 ) );
            if ( addBone( hashed_string( name ), parentIndex, referencePose, float4x4{ arrInverseBind } ) < 0 )
                return false;
        }

        const JsonValue attachments = root.get( "attachments" );
        if ( attachments.isArray() == false )
        {
            SW_LOG_ERROR( "Skeleton '%#': 'attachments' must be an array", sourceLabel );
            return false;
        }
        for ( size_t attachmentIndex = 0; attachmentIndex < attachments.size(); ++attachmentIndex )
        {
            const JsonValue attachment = attachments.at( attachmentIndex );
            if ( AnimJsonUtil::hasOnlyKnownKeys( attachment, { "name", "mesh", "bone", "translation", "rotation", "scale" }, sourceLabel ) == false )
                return false;
            SkeletonAttachment entry{};
            const bool         bComplete = attachment.get( "name" ).isString() && attachment.get( "mesh" ).isString() && attachment.get( "bone" ).isString() &&
                                   AnimJsonUtil::readBoneTransform( attachment, entry._localTransform );
            if ( bComplete == false )
            {
                SW_LOG_ERROR( "Skeleton '%#': attachment %# is missing a field or has a malformed one", sourceLabel, attachmentIndex );
                return false;
            }
            entry._name       = attachment.get( "name" ).asString();
            entry._meshPath   = attachment.get( "mesh" ).asString();
            entry._parentBone = hashed_string( attachment.get( "bone" ).asString() );
            if ( findBoneIndex( entry._parentBone ) < 0 )
            {
                SW_LOG_ERROR( "Skeleton '%#': attachment '%#' names unknown bone '%#'", sourceLabel, entry._name.c_str(), entry._parentBone.c_str() );
                return false;
            }
            _listAttachment.push_back( entry );
        }
        return true;
    }

    string Skeleton::toJson() const
    {
        JsonDocument    document;
        const JsonValue root = document.makeObject();
        {
            const JsonValue bones = root.set( "bones" );
            bones.setArray();
            for ( const SkeletonBone& bone : _listBone )
            {
                const JsonValue entry = bones.pushBack();
                entry.setObject();
                entry.set( "name" ).setString( bone._name.c_str() );
                entry.set( "parent" ).setInt( bone._parentIndex );
                AnimJsonUtil::writeBoneTransform( entry, bone._referencePose );
                AnimJsonUtil::writeFloats( entry.set( "inverse_bind" ), &bone._inverseBind._11, 16 );
            }
        }
        {
            const JsonValue attachments = root.set( "attachments" );
            attachments.setArray();
            for ( const SkeletonAttachment& attachment : _listAttachment )
            {
                const JsonValue entry = attachments.pushBack();
                entry.setObject();
                entry.set( "name" ).setString( attachment._name );
                entry.set( "mesh" ).setString( attachment._meshPath );
                entry.set( "bone" ).setString( attachment._parentBone.c_str() );
                AnimJsonUtil::writeBoneTransform( entry, attachment._localTransform );
            }
        }
        return document.dump( 2 );
    }

    bool Skeleton::saveToFile( string_view path ) const
    {
        if ( FileUtil::ensureParentDirectoryExists( path ) == false )
            return false;
        return FileUtil::writeTextFile( path, toJson() );
    }
} // namespace sw
