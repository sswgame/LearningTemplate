#include "pch.h"

#include "Engine/Animation/Skeletal/SkeletonBoneLOD.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    SW_LOG_CALLER( "SkeletonBoneLOD" );

    namespace
    {
        /** @brief 프로세스 전역 내용 번호입니다(0 은 쓰지 않는다). */
        uint64 allocateRevision()
        {
            static atomic<uint64> s_nextRevision{ 1 };
            return s_nextRevision.fetch_add( 1, std::memory_order_relaxed );
        }
    } // namespace
} // namespace sw

namespace sw
{

    bool SkeletonBoneLOD::parseXML( string_view xml, string_view sourceLabel )
    {
        _listLevel.clear();
        XMLDocument document;
        XMLNode     root;
        if ( XMLReadUtil::parseRoot( document, xml, sourceLabel, "SkeletonBoneLOD", root ) != XMLRootStatus::Found )
        {
            SW_LOG_ERROR( "Bone LOD '%#': malformed XML (expected a <SkeletonBoneLOD> root)", sourceLabel );
            return false;
        }
        if ( parseRoot( root, sourceLabel ) )
        {
            _revision = allocateRevision();
            return true;
        }
        _listLevel.clear();
        return false;
    }

    bool SkeletonBoneLOD::loadFromResource( string_view path )
    {
        SW_MEMORY_SCOPE( Animation );
        string text;
        if ( ResourceIO::readTextResource( ResourceID{ path }, text ) == false )
        {
            SW_LOG_ERROR( "Bone LOD '%#' could not be read", path );
            _listLevel.clear();
            return false;
        }
        return parseXML( text, path );
    }

    bool SkeletonBoneLOD::parseRoot( const XMLNode& root, string_view sourceLabel )
    {
        if ( XMLReadUtil::hasOnlyKnownNames( root, {}, { "levels" }, sourceLabel ) == false )
            return false;
        const XMLNode levels = root.findChild( "levels" );
        if ( levels.isValid() == false )
        {
            SW_LOG_ERROR( "Bone LOD '%#': a <levels> element is required", sourceLabel );
            return false;
        }
        size_t levelIndex{ 0 };
        for ( XMLNode level = levels.findChild(); level.isValid(); level = level.findNextSibling(), ++levelIndex )
        {
            if ( string_view{ level.getName() } != "level" || XMLReadUtil::hasOnlyKnownNames( level, { "max_screen_size" }, { "remove" }, sourceLabel ) == false )
            {
                SW_LOG_ERROR( "Bone LOD '%#': <levels> holds only <level> elements", sourceLabel );
                return false;
            }
            const XMLNode                removed          = level.findChild( "remove" );
            const std::optional<float32> maxScreenSize    = level.readRequiredAttribute<float32>( "max_screen_size" );
            const bool                   bInvalidLODLevel = maxScreenSize.has_value() == false || removed.isValid() == false || removed.findChild().isValid() == false;
            if ( bInvalidLODLevel )
            {
                SW_LOG_ERROR( "Bone LOD '%#': level %# needs 'max_screen_size' (number) and a non-empty <remove> list", sourceLabel, levelIndex );
                return false;
            }
            SkeletonBoneLODLevel entry{};
            entry._maxScreenSize = *maxScreenSize;
            if ( _listLevel.empty() == false && entry._maxScreenSize >= _listLevel.back()._maxScreenSize )
            {
                SW_LOG_ERROR( "Bone LOD '%#': level %# max_screen_size %# must be smaller than the previous level's", sourceLabel, levelIndex, entry._maxScreenSize );
                return false;
            }
            for ( XMLNode bone = removed.findChild(); bone.isValid(); bone = bone.findNextSibling() )
            {
                if ( string_view{ bone.getName() } != "item" )
                {
                    SW_LOG_ERROR( "Bone LOD '%#': level %# <remove> must hold <item> bone names", sourceLabel, levelIndex );
                    return false;
                }
                entry._listRemovedBone.push_back( hashed_string( string_view{ bone.getText() } ) );
            }
            _listLevel.push_back( std::move( entry ) );
        }
        return true;
    }

    uint32 SkeletonBoneLOD::selectLevel( float32 screenSize ) const
    {
        uint32 level = 0;
        for ( uint32 levelIndex = 0; levelIndex < static_cast<uint32>( _listLevel.size() ); ++levelIndex )
        {
            if ( screenSize <= _listLevel[levelIndex]._maxScreenSize )
                level = levelIndex + 1;
        }
        return level;
    }

    bool SkeletonBoneLOD::computeMasks( const Skeleton& skeleton, vector<vector<uint8>>& outListMask, string_view sourceLabel ) const
    {
        outListMask.clear();
        const uint32  boneCount = skeleton.getBoneCount();
        vector<uint8> mask( boneCount, SW_TRUE );
        for ( const SkeletonBoneLODLevel& level : _listLevel )
        {
            for ( const hashed_string& boneName : level._listRemovedBone )
            {
                const int32 boneIndex = skeleton.findBoneIndex( boneName );
                if ( boneIndex < 0 )
                {
                    SW_LOG_ERROR( "Bone LOD '%#': bone '%#' is not in the skeleton", sourceLabel, boneName.c_str() );
                    outListMask.clear();
                    return false;
                }
                mask[static_cast<uint32>( boneIndex )] = SW_FALSE;
            }
            // 부모가 앞에 있으므로 한 번 훑으면 뺀 본의 자손이 모두 빠진다.
            for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
            {
                const int32 parentIndex = skeleton.getBone( boneIndex )._parentIndex;
                if ( parentIndex >= 0 && mask[static_cast<uint32>( parentIndex )] == SW_FALSE )
                    mask[boneIndex] = SW_FALSE;
            }
            outListMask.push_back( mask );
        }
        return true;
    }

    string SkeletonBoneLOD::makePathForSkeleton( string_view skeletonPath )
    {
        if ( StringUtil::endsWith( skeletonPath, Skeleton::kExtension ) == false )
            return string{};
        const string_view stemPath = skeletonPath.substr( 0, skeletonPath.size() - Skeleton::kExtension.size() );
        string_view       folder;
        string_view       stem;
        FileUtil::splitPath( stemPath, folder, stem );
        if ( folder.empty() == false )
            folder.remove_suffix( 1 ); // 끝 구분자
        string_view folderName;
        FileUtil::getFileNamePart( folder, folderName );
        // 임포트 옆 폴더(폴더 이름 = 파일 이름)는 다시 임포트할 때 지워진다. 그 밖의 메시 옆에 둔다.
        string path = ( folder.empty() == false && folderName == stem ) ? string{ folder } : string{ stemPath };
        path.append( kExtension.data(), kExtension.size() );
        return path;
    }

    void SkeletonBoneLOD::makeSkeletonCandidatePaths( string_view boneLODPath, string& outImportedPath, string& outSiblingPath )
    {
        outImportedPath.clear();
        outSiblingPath.clear();
        if ( StringUtil::endsWith( boneLODPath, kExtension ) == false )
            return;
        const string_view stemPath = boneLODPath.substr( 0, boneLODPath.size() - kExtension.size() );
        string_view       stem;
        FileUtil::getFileNamePart( stemPath, stem );
        outImportedPath = string{ stemPath } + "/" + string{ stem } + string{ Skeleton::kExtension };
        outSiblingPath  = string{ stemPath } + string{ Skeleton::kExtension };
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( SkeletonBoneLOD, SkeletonBoneLOD::kAssetType );
} // namespace sw
