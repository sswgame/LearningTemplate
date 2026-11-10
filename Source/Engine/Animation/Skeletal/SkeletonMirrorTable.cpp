#include "pch.h"

#include "Engine/Animation/Skeletal/SkeletonMirrorTable.h"

#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Asset/Cook/AssetCompanion.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"

namespace sw
{
    bool SkeletonMirrorTable::loadFromXMLText( string_view xml, string_view sourceLabel )
    {
        _listPair.clear();
        const string label{ sourceLabel };
        if ( ConfigManager::readConfigXMLDocument( this, *StaticType(), xml, label.c_str() ) )
        {
            _revision = IdentityToken::make(); // 짝 번호 표를 만든 쪽이 들고 비교한다(`SkeletonMirrorBoneMap`)
            return true;
        }
        _listPair.clear();
        return false;
    }

    bool SkeletonMirrorTable::loadFromAsset( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        SW_MEMORY_SCOPE( Animation );
        _listPair.clear();
        string text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "SkeletonMirrorTable.CouldNotRead", "Mirror table '%#' could not be read", path );
            return false;
        }
        // 리소스 id 로 합친다 — 덮어쓰기 층 · 다시 읽기 기록을 받는다(본 LOD 와 같은 입구).
        string flatText;
        bool   bComposed = false;
        if ( ResourceComposer::compose( text, path, ResourceContext::forType( *StaticType() ).withAsset( assetID ), flatText, bComposed ) == false )
            return false;
        return loadFromXMLText( bComposed ? string_view{ flatText } : string_view{ text }, path );
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

namespace sw
{
    SkeletonMirrorBoneMap::SkeletonMirrorBoneMap()
        : _table{}
        , _listMirroredBone{}
        , _builtRevision{}
        , _pBuiltSkeleton{ nullptr }
    {
    }

    void SkeletonMirrorBoneMap::setTable( shared_ptr<const SkeletonMirrorTable> table )
    {
        _table          = std::move( table );
        _pBuiltSkeleton = nullptr;
        _listMirroredBone.clear();
    }

    const vector<uint32>* SkeletonMirrorBoneMap::find( const Skeleton& skeleton, string_view sourceLabel )
    {
        if ( _table == nullptr )
            return nullptr;
        if ( _pBuiltSkeleton != &skeleton || _builtRevision != _table->getRevision() )
        {
            _pBuiltSkeleton = &skeleton;
            _builtRevision  = _table->getRevision();
            (void)_table->buildBoneMap( skeleton, _listMirroredBone, sourceLabel ); // 틀리면 오류를 남기고 비운다 — 같은 내용으로 다시 만들지 않는다
        }
        return _listMirroredBone.empty() ? nullptr : &_listMirroredBone;
    }
} // namespace sw

// 스켈레톤 옆의 미러 표 — 스켈레톤이 담기면 함께 담는다(없으면 넘어간다). 스켈레톤이 아니면 빈 글이다.
SW_ASSET_COMPANION( SkeletonMirrorTable, &::sw::SkeletonMirrorTable::makePathForSkeleton );
