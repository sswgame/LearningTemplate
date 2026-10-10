#include "pch.h"

#include "Engine/Animation/Skeletal/SkeletonBoneLOD.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/StringUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/AnimJSONUtil.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw
{
    SW_LOG_CALLER( "SkeletonBoneLOD" );

    namespace
    {
        struct SkeletonBoneLODInternal
        {
            /** @brief 프로세스 전역 내용 번호입니다(0 은 쓰지 않는다). */
            static uint64 allocateRevision()
            {
                static atomic<uint64> s_nextRevision{ 1 };
                return s_nextRevision.fetch_add( 1, std::memory_order_relaxed );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{

    bool SkeletonBoneLOD::parseJSON( string_view json, string_view sourceLabel )
    {
        _listLevel.clear();
        JSONDocument document;
        if ( document.parse( json, sourceLabel ) == false )
        {
            SW_LOG_ERROR( "Bone LOD '%#': malformed JSON", sourceLabel );
            return false;
        }
        if ( parseRoot( document.getRoot(), sourceLabel ) )
        {
            _revision = SkeletonBoneLODInternal::allocateRevision();
            return true;
        }
        _listLevel.clear();
        return false;
    }

    bool SkeletonBoneLOD::loadFromResource( string_view path )
    {
        SW_MEMORY_SCOPE( Animation );
        string text;
        if ( ResourceUtil::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "Bone LOD '%#' could not be read", path );
            _listLevel.clear();
            return false;
        }
        return parseJSON( text, path );
    }

    bool SkeletonBoneLOD::parseRoot( const JSONValue& root, string_view sourceLabel )
    {
        if ( AnimJSONUtil::hasOnlyKnownKeys( root, { "levels" }, sourceLabel ) == false )
            return false;
        const JSONValue levels = root.get( "levels" );
        if ( levels.isArray() == false )
        {
            SW_LOG_ERROR( "Bone LOD '%#': 'levels' must be an array", sourceLabel );
            return false;
        }
        for ( size_t levelIndex = 0; levelIndex < levels.size(); ++levelIndex )
        {
            const JSONValue level = levels.at( levelIndex );
            if ( AnimJSONUtil::hasOnlyKnownKeys( level, { "max_screen_size", "remove" }, sourceLabel ) == false )
                return false;
            const JSONValue removed = level.get( "remove" );
            if ( level.get( "max_screen_size" ).isNumber() == false || removed.isArray() == false || removed.size() == 0 )
            {
                SW_LOG_ERROR( "Bone LOD '%#': level %# needs 'max_screen_size' (number) and a non-empty 'remove' array", sourceLabel, levelIndex );
                return false;
            }
            SkeletonBoneLODLevel entry{};
            entry._maxScreenSize = static_cast<float32>( level.get( "max_screen_size" ).asFloat() );
            if ( _listLevel.empty() == false && entry._maxScreenSize >= _listLevel.back()._maxScreenSize )
            {
                SW_LOG_ERROR( "Bone LOD '%#': level %# max_screen_size %# must be smaller than the previous level's", sourceLabel, levelIndex, entry._maxScreenSize );
                return false;
            }
            for ( size_t boneIndex = 0; boneIndex < removed.size(); ++boneIndex )
            {
                if ( removed.at( boneIndex ).isString() == false )
                {
                    SW_LOG_ERROR( "Bone LOD '%#': level %# 'remove' must hold bone names", sourceLabel, levelIndex );
                    return false;
                }
                entry._listRemovedBone.push_back( hashed_string( removed.at( boneIndex ).asString() ) );
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

    bool SkeletonBoneLOD::buildMasks( const Skeleton& skeleton, vector<vector<uint8>>& outListMask, string_view sourceLabel ) const
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
        const string_view stemPath   = skeletonPath.substr( 0, skeletonPath.size() - Skeleton::kExtension.size() );
        const size_t      slash      = stemPath.find_last_of( '/' );
        const string_view stem       = ( slash == string_view::npos ) ? stemPath : stemPath.substr( slash + 1 );
        const string_view folder     = ( slash == string_view::npos ) ? string_view{} : stemPath.substr( 0, slash );
        const size_t      parentCut  = folder.find_last_of( '/' );
        const string_view folderName = ( parentCut == string_view::npos ) ? folder : folder.substr( parentCut + 1 );
        // 임포트 옆 폴더(폴더 이름 = 파일 이름)는 다시 임포트할 때 지워진다 — 그 밖, 메시 곁에 둔다.
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
        const size_t      slash    = stemPath.find_last_of( '/' );
        const string_view stem     = ( slash == string_view::npos ) ? stemPath : stemPath.substr( slash + 1 );
        outImportedPath            = string{ stemPath } + "/" + string{ stem } + string{ Skeleton::kExtension };
        outSiblingPath             = string{ stemPath } + string{ Skeleton::kExtension };
    }
} // namespace sw
