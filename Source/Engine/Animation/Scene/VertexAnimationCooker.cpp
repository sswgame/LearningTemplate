#include "pch.h"

#include "Engine/Animation/Scene/VertexAnimationCooker.h"

#include "Core/Common/BitUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Scene/MeshVertexAnimationBaker.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Asset/AssetDataKind.h"
#include "Engine/Asset/AssetIO.h"
#include "Engine/Asset/AssetPaths.h"
#include "Engine/Asset/Cook/CookStore.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshVertexAnimation.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    SW_LOG_CALLER( "VertexAnimationCooker" );

    namespace
    {
        /** @brief 상대 경로가 활성 게임이 아닌 게임 팩 아래인지입니다(SceneCooker 와 같은 규칙). */
        bool isInInactiveGamePack( string_view relativePath, string_view activePackRoot )
        {
            if ( activePackRoot.empty() || StringUtil::startsWith( relativePath, "game/", true ) == false )
                return false;
            const string activePrefix = string( activePackRoot ) + "/";
            return StringUtil::startsWith( relativePath, activePrefix, true ) == false;
        }
    } // namespace
} // namespace sw

namespace sw
{
    bool VertexAnimationCookList::parseXML( string_view xml, string_view sourceLabel )
    {
        *this = VertexAnimationCookList{};
        XMLDocument document;
        XMLNode     root;
        if ( XMLReadUtil::parseRoot( document, xml, sourceLabel, "VertexAnimationCookList", root ) != XMLRootStatus::Found )
        {
            SW_LOG_ERROR( "Vertex animation list '%#': malformed XML (expected a <VertexAnimationCookList> root)", sourceLabel );
            return false;
        }
        if ( parseRoot( root, sourceLabel ) )
            return true;
        *this = VertexAnimationCookList{};
        return false;
    }

    bool VertexAnimationCookList::loadFromAsset( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        string            text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR( "Vertex animation list '%#' could not be read", path );
            *this = VertexAnimationCookList{};
            return false;
        }
        return parseXML( text, path );
    }

    bool VertexAnimationCookList::parseRoot( const XMLNode& root, string_view sourceLabel )
    {
        if ( XMLReadUtil::hasOnlyKnownNames( root, { "mesh", "skeleton", "clip_folder" }, { "clips" }, sourceLabel ) == false )
            return false;
        const XMLNode clips                 = root.findChild( "clips" );
        const bool    bInvalidAnimationList = root.findAttribute( "mesh" ) == nullptr || root.findAttribute( "skeleton" ) == nullptr ||
                                           root.findAttribute( "clip_folder" ) == nullptr || clips.isValid() == false || clips.findChild().isValid() == false;
        if ( bInvalidAnimationList )
        {
            SW_LOG_ERROR( "Vertex animation list '%#' needs mesh, skeleton, clip_folder and a non-empty <clips> list", sourceLabel );
            return false;
        }
        _meshPath     = string{ root.getAttributeText( "mesh" ) };
        _skeletonPath = string{ root.getAttributeText( "skeleton" ) };
        _clipFolder   = string{ root.getAttributeText( "clip_folder" ) };
        for ( XMLNode clip = clips.findChild(); clip.isValid(); clip = clip.findNextSibling() )
        {
            if ( string_view{ clip.getName() } != "item" )
            {
                SW_LOG_ERROR( "Vertex animation list '%#': <clips> must hold <item> names", sourceLabel );
                return false;
            }
            _listClip.push_back( hashed_string( string_view{ clip.getText() } ) );
        }
        // 가리키는 파일이 모두 있어야 한다 — 이름이 틀린 목록이 쿠킹 때 조용히 빠지지 않게.
        if ( AssetIO::hasAsset( AssetID{ _meshPath } ) == false || AssetIO::hasAsset( AssetID{ _skeletonPath } ) == false )
        {
            SW_LOG_ERROR( "Vertex animation list '%#': mesh '%#' or skeleton '%#' does not exist", sourceLabel, _meshPath.c_str(), _skeletonPath.c_str() );
            return false;
        }
        for ( const hashed_string& clipName : _listClip )
        {
            if ( AssetIO::hasAsset( AssetID{ makeClipPath( clipName ) } ) == false )
            {
                SW_LOG_ERROR( "Vertex animation list '%#': clip '%#' does not exist in '%#'", sourceLabel, clipName.c_str(), _clipFolder.c_str() );
                return false;
            }
        }
        return true;
    }

    string VertexAnimationCookList::makeClipPath( const hashed_string& clipName ) const
    {
        const string fileName = StringUtil::toLower( clipName.c_str() ) + string( AnimationClip::kExtension );
        return _clipFolder.empty() ? fileName : FileUtil::joinPath( _clipFolder, fileName );
    }

    bool VertexAnimationCooker::cookClip( const VertexAnimationCookList& list, const hashed_string& clipName, const string& outputPath, float32 framesPerSecond )
    {
        MeshAssetData meshData;
        Skeleton      skeleton;
        if ( MeshAssetFormat::loadFromAsset( AssetID{ list._meshPath }, meshData ) == false )
        {
            SW_LOG_ERROR( "Vertex animation cook: could not read mesh '%#'", list._meshPath.c_str() );
            return false;
        }
        if ( skeleton.loadFromAsset( AssetID{ list._skeletonPath } ) == false )
        {
            SW_LOG_ERROR( "Vertex animation cook: could not read skeleton '%#'", list._skeletonPath.c_str() );
            return false;
        }
        shared_ptr<Mesh> mesh = Mesh::create();
        mesh->setVertices( std::move( meshData._listVertex ) );
        mesh->setSkin( std::move( meshData._listSkinVertex ), meshData._skinBoneCount );

        AnimationClip       clip;
        MeshVertexAnimation animation;
        const bool          bBaked =
            clip.loadFromAsset( AssetID{ list.makeClipPath( clipName ) } ) && MeshVertexAnimationBaker::bake( *mesh, skeleton, clip, framesPerSecond, false, animation );
        if ( bBaked == false || animation.saveToFile( outputPath ) == false )
        {
            SW_LOG_ERROR( "Vertex animation cook failed for '%#' clip '%#'", list._meshPath.c_str(), clipName.c_str() );
            return false;
        }
        return true;
    }

    uint32 VertexAnimationCooker::cookList( const VertexAnimationCookList& list, const string& cookedDir, float32 framesPerSecond, uint32& outFailedCount )
    {
        uint32 writtenCount = 0;
        for ( const hashed_string& clipName : list._listClip )
        {
            const string cookedPath = MeshVertexAnimation::makeCookedPath( list._meshPath, clipName );
            // 쿠킹 폴더가 비었으면 메시 곁(리소스 id 를 디스크 자리로 푼 곳)에 쓴다 — id 를 그대로 디스크 경로로 쓰면 작업 폴더 아래에 생긴다.
            const string outputPath = cookedDir.empty() ? AssetPaths::resolveWritePath( cookedPath ) : FileUtil::joinPath( cookedDir, cookedPath );
            if ( outputPath.empty() )
            {
                SW_LOG_ERROR( "VAT '%#' has no disk folder beside its mesh to be written to", cookedPath.c_str() );
                ++outFailedCount;
                continue;
            }
            if ( cookClip( list, clipName, outputPath, framesPerSecond ) == false )
            {
                ++outFailedCount;
                continue;
            }
            ++writtenCount;
        }
        return writtenCount;
    }

    CookJobDesc VertexAnimationCooker::makeCookJobDesc( float32 framesPerSecond )
    {
        CookJobDesc desc;
        desc._cookerName    = "VertexAnimation";
        desc._cookerVersion = kCookVersion;
        desc._target        = "Any";
        desc._settingsHash  = BitUtil::floatToBits( framesPerSecond ); // 설정 해시는 프레임율의 비트 그대로다
        return desc;
    }

    uint32 VertexAnimationCooker::cookAll( const string& resourceRoot, const string& cookedDir, float32 framesPerSecond, const CookStore& store,
                                           uint32& outFailedCount )
    {
        vector<string> listFile;
        if ( FileUtil::collectFiles( resourceRoot, ".xml", listFile, true ) == false )
        {
            SW_LOG_ERROR( "Vertex animation cook could not list '%#' - nothing is cooked", resourceRoot );
            ++outFailedCount;
            return 0;
        }
        std::sort( listFile.begin(), listFile.end() );
        const string      normalizedRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot ) );
        const string      activePackRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( engine::getSubsystem<GameConfig>()._packRoot.get() ) );
        const CookJobDesc desc           = makeCookJobDesc( framesPerSecond );
        CookTally         tally;
        for ( const string& filePath : listFile )
        {
            if ( StringUtil::endsWith( filePath, VertexAnimationCookList::kExtension, true ) == false )
                continue;
            const string normalizedFile = FileUtil::normalizeSeparators( filePath );
            const string relativePath   = normalizedFile.substr( std::min( normalizedRoot.size() + 1, normalizedFile.size() ) );
            if ( isInInactiveGamePack( relativePath, activePackRoot ) )
                continue;
            // 목록은 무엇을 구울지 정하려고 먼저 읽는다. 이 읽기는 기록하지 않는다 — 클립 작업마다 목록을 다시 읽어 그 작업의 입력으로 남긴다.
            VertexAnimationCookList list;
            bool                    bListLoaded = false;
            {
                const CookJob discovery( desc );
                bListLoaded = list.loadFromAsset( AssetID{ relativePath } );
            }
            if ( bListLoaded == false )
            {
                ++outFailedCount;
                continue;
            }
            for ( const hashed_string& clipName : list._listClip )
            {
                const string      outputID = MeshVertexAnimation::makeCookedPath( list._meshPath, clipName );
                const CookOutcome outcome  = store.cookFile( desc, outputID, FileUtil::joinPath( cookedDir, outputID ), [&]()
                 {
                    VertexAnimationCookList jobList;
                    const bool              bCooked =
                        jobList.loadFromAsset( AssetID{ relativePath } ) && cookClip( jobList, clipName, FileUtil::joinPath( cookedDir, outputID ), framesPerSecond );
                    return bCooked ? CookStep::Written : CookStep::Failed;
                } );
                outFailedCount += outcome == CookOutcome::Failed ? 1u : 0u;
                tally.add( outcome );
            }
        }
        SW_LOG_INFO( "Vertex animation cook: %# reused from the DDC, %# cooked", tally._reusedCount, tally._cookedCount );
        return tally.getWrittenCount();
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( VertexAnimationCookList, VertexAnimationCookList::kAssetType );

    /** @brief VAT 쿠킹 목록 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 파일을 읽는다). */
    SW_RESOURCE_DATA_KIND( VertexAnimation, "vertexanimation", nullptr, nullptr, VertexAnimationCookList::kExtension.data(), &AssetDataKindUtil::loadCatalog<VertexAnimationCookList> );
} // namespace sw
