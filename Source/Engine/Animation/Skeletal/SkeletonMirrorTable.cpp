#include "pch.h"

#include "Engine/Animation/Skeletal/SkeletonMirrorTable.h"

#include "Core/Log/Logger.h"

#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Asset/Cook/AssetCompanion.h"
#include "Engine/Config/ConfigManager.h"

namespace sw
{
    bool SkeletonMirrorTable::loadFromXMLText( string_view xml, string_view sourceLabel )
    {
        _listPair.clear();
        const string label{ sourceLabel };
        if ( ConfigManager::readConfigXMLDocument( this, *StaticType(), xml, label.c_str() ) )
            return true;
        _listPair.clear();
        return false;
    }

    bool SkeletonMirrorTable::buildBoneMap( const Skeleton& skeleton, vector<uint32>& outListMirroredBone, string_view sourceLabel ) const
    {
        outListMirroredBone.resize( skeleton.getBoneCount() );
        for ( uint32 boneIndex = 0; boneIndex < skeleton.getBoneCount(); ++boneIndex )
        {
            outListMirroredBone[boneIndex] = boneIndex;
        }
        bool bValid = true;
        for ( const SkeletonMirrorPair& pair : _listPair )
        {
            const optional<uint32> bone         = skeleton.findBoneIndex( pair._bone );
            const optional<uint32> mirroredBone = skeleton.findBoneIndex( pair._mirroredBone );
            if ( bone.has_value() == false || mirroredBone.has_value() == false )
            {
                SW_LOG_ERROR_ID( "SkeletonMirrorTable.UnknownBone", "Mirror table '%#': pair '%#' - '%#' names a bone the skeleton does not have", sourceLabel, pair._bone.c_str(), pair._mirroredBone.c_str() );
                bValid = false;
                continue;
            }
            if ( *bone == *mirroredBone )
            {
                SW_LOG_ERROR_ID( "SkeletonMirrorTable.SelfPair", "Mirror table '%#': bone '%#' is paired with itself - unlisted bones already mirror onto themselves", sourceLabel, pair._bone.c_str() );
                bValid = false;
                continue;
            }
            if ( outListMirroredBone[*bone] != *bone || outListMirroredBone[*mirroredBone] != *mirroredBone )
            {
                SW_LOG_ERROR_ID( "SkeletonMirrorTable.BoneInTwoPairs", "Mirror table '%#': pair '%#' - '%#' reuses a bone that is already in another pair", sourceLabel, pair._bone.c_str(), pair._mirroredBone.c_str() );
                bValid = false;
                continue;
            }
            outListMirroredBone[*bone]         = *mirroredBone;
            outListMirroredBone[*mirroredBone] = *bone;
        }
        if ( bValid == false )
            outListMirroredBone.clear();
        return bValid;
    }

    string SkeletonMirrorTable::makePathForSkeleton( string_view skeletonPath )
    {
        return Skeleton::makeSidePath( skeletonPath, kExtension );
    }
} // namespace sw

// 스켈레톤 옆의 미러 표 — 스켈레톤이 담기면 함께 담는다(없으면 넘어간다). 스켈레톤이 아니면 빈 글이다.
SW_ASSET_COMPANION( SkeletonMirrorTable, &::sw::SkeletonMirrorTable::makePathForSkeleton );
