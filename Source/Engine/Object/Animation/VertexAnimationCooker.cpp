#include "pch.h"

#include "Engine/Object/Animation/VertexAnimationCooker.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/AnimJsonUtil.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshVertexAnimation.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
    SW_LOG_CALLER( "VertexAnimationCooker" );

    namespace
    {
        struct VertexAnimationCookerInternal
        {
            /** @brief 상대 경로가 활성 게임이 아닌 게임 팩 아래인지입니다(SceneCooker 와 같은 규칙). */
            static bool isInOtherGamePack( string_view relativePath, string_view activePackRoot )
            {
                if ( activePackRoot.empty() || StringUtil::startsWith( relativePath, "game/", true ) == false )
                    return false;
                const string activePrefix = string( activePackRoot ) + "/";
                return StringUtil::startsWith( relativePath, activePrefix, true ) == false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool VertexAnimationCookList::parseJson( string_view json, string_view sourceLabel )
    {
        *this = VertexAnimationCookList{};
        JsonDocument document;
        if ( document.parse( json, sourceLabel ) == false )
        {
            SW_LOG_ERROR( "Vertex animation list '%#': malformed JSON", sourceLabel );
            return false;
        }
        if ( parseRoot( document.getRoot(), sourceLabel ) )
            return true;
        *this = VertexAnimationCookList{};
        return false;
    }

    bool VertexAnimationCookList::loadFromResource( string_view path )
    {
        string text;
        if ( ResourceUtil::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "Vertex animation list '%#' could not be read", path );
            *this = VertexAnimationCookList{};
            return false;
        }
        return parseJson( text, path );
    }

    bool VertexAnimationCookList::parseRoot( const JsonValue& root, string_view sourceLabel )
    {
        if ( AnimJsonUtil::hasOnlyKnownKeys( root, { "mesh", "skeleton", "clip_folder", "clips" }, sourceLabel ) == false )
            return false;
        const JsonValue clips = root.get( "clips" );
        if ( root.get( "mesh" ).isString() == false || root.get( "skeleton" ).isString() == false || root.get( "clip_folder" ).isString() == false ||
             clips.isArray() == false || clips.size() == 0 )
        {
            SW_LOG_ERROR( "Vertex animation list '%#' needs mesh, skeleton, clip_folder and a non-empty clips array", sourceLabel );
            return false;
        }
        _meshPath     = root.get( "mesh" ).asString();
        _skeletonPath = root.get( "skeleton" ).asString();
        _clipFolder   = root.get( "clip_folder" ).asString();
        for ( size_t clipIndex = 0; clipIndex < clips.size(); ++clipIndex )
        {
            if ( clips.at( clipIndex ).isString() == false )
            {
                SW_LOG_ERROR( "Vertex animation list '%#': clips must be names", sourceLabel );
                return false;
            }
            _listClip.push_back( hashed_string( clips.at( clipIndex ).asString() ) );
        }
        // 가리키는 파일이 모두 있어야 한다 — 이름이 틀린 목록이 쿠킹 때 조용히 빠지지 않게.
        if ( ResourceUtil::hasResource( _meshPath ) == false || ResourceUtil::hasResource( _skeletonPath ) == false )
        {
            SW_LOG_ERROR( "Vertex animation list '%#': mesh '%#' or skeleton '%#' does not exist", sourceLabel, _meshPath.c_str(), _skeletonPath.c_str() );
            return false;
        }
        for ( const hashed_string& clipName : _listClip )
        {
            if ( ResourceUtil::hasResource( makeClipPath( clipName ) ) == false )
            {
                SW_LOG_ERROR( "Vertex animation list '%#': clip '%#' does not exist in '%#'", sourceLabel, clipName.c_str(), _clipFolder.c_str() );
                return false;
            }
        }
        return true;
    }

    string VertexAnimationCookList::makeClipPath( const hashed_string& clipName ) const
    {
        const string fileName = StringUtil::toLower( clipName.c_str() ) + string( AnimClip::kExtension );
        return _clipFolder.empty() ? fileName : FileUtil::joinPath( _clipFolder, fileName );
    }

    uint32 VertexAnimationCooker::cookList( const VertexAnimationCookList& list, const string& cookedDir, float32 framesPerSecond, uint32& outFailedCount )
    {
        MeshAssetData meshData;
        Skeleton      skeleton;
        if ( MeshAssetFormat::loadFromResource( list._meshPath, meshData ) == false || skeleton.loadFromResource( list._skeletonPath ) == false )
        {
            SW_LOG_ERROR( "Vertex animation cook: could not read mesh '%#' or skeleton '%#'", list._meshPath.c_str(), list._skeletonPath.c_str() );
            ++outFailedCount;
            return 0;
        }
        shared_ptr<Mesh> mesh = Mesh::create();
        mesh->setVertices( std::move( meshData._listVertex ) );
        mesh->setSkin( std::move( meshData._listSkinVertex ), meshData._skinBoneCount );

        uint32 writtenCount = 0;
        for ( const hashed_string& clipName : list._listClip )
        {
            AnimClip            clip;
            MeshVertexAnimation animation;
            const string        cookedPath = MeshVertexAnimation::makeCookedPath( list._meshPath, clipName );
            const string        outputPath = cookedDir.empty() ? cookedPath : FileUtil::joinPath( cookedDir, cookedPath );
            const bool          bBaked     = clip.loadFromResource( list.makeClipPath( clipName ) ) &&
                                MeshVertexAnimationBaker::bake( *mesh, skeleton, clip, framesPerSecond, false, animation );
            if ( bBaked == false || animation.saveToFile( outputPath ) == false )
            {
                SW_LOG_ERROR( "Vertex animation cook failed for '%#' clip '%#'", list._meshPath.c_str(), clipName.c_str() );
                ++outFailedCount;
                continue;
            }
            ++writtenCount;
        }
        return writtenCount;
    }

    uint32 VertexAnimationCooker::cookAll( const string& resourceRoot, const string& cookedDir, float32 framesPerSecond, uint32& outFailedCount )
    {
        vector<string> listFile;
        FileUtil::collectFiles( resourceRoot, ".json", listFile, true );
        const string normalizedRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot ) );
        const string activePackRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( GameConfig::getActive()._packRoot ) );
        uint32       writtenCount   = 0;
        for ( const string& filePath : listFile )
        {
            if ( StringUtil::endsWith( filePath, VertexAnimationCookList::kExtension, true ) == false )
                continue;
            const string normalizedFile = FileUtil::normalizeSeparators( filePath );
            const string relativePath   = normalizedFile.substr( std::min( normalizedRoot.size() + 1, normalizedFile.size() ) );
            if ( VertexAnimationCookerInternal::isInOtherGamePack( relativePath, activePackRoot ) )
                continue;
            VertexAnimationCookList list;
            if ( list.loadFromResource( relativePath ) == false )
            {
                ++outFailedCount;
                continue;
            }
            writtenCount += cookList( list, cookedDir, framesPerSecond, outFailedCount );
        }
        return writtenCount;
    }
} // namespace sw
