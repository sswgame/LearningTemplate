#include "pch.h"

#include "Engine/Animation/Scene/VertexAnimationCookList.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Scene/MeshVertexAnimationBaker.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Asset/AssetFormat.h"
#include "Engine/Asset/Cook/CookStore.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/AssetTypeRegistry.h"
#include "Engine/Content/CookCodeFingerprint.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshVertexAnimation.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
} // namespace sw

namespace sw
{
    bool VertexAnimationCookList::loadFromXMLText( string_view xml, string_view sourceLabel )
    {
        *this = VertexAnimationCookList{};
        XMLDocument document;
        XMLNode     root;
        if ( XMLReadUtil::parseRoot( document, xml, sourceLabel, "VertexAnimationCookList", root ) != XMLRootStatus::Found )
        {
            SW_LOG_ERROR_ID( "VertexAnimationCooker.VertexAnimationListMalformed", "Vertex animation list '%#': malformed XML (expected a <VertexAnimationCookList> root)", sourceLabel );
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
            SW_LOG_ERROR_ID( "VertexAnimationCooker.VertexAnimationListCould", "Vertex animation list '%#' could not be read", path );
            *this = VertexAnimationCookList{};
            return false;
        }
        return loadFromXMLText( text, path );
    }

    bool VertexAnimationCookList::parseRoot( const XMLNode& root, string_view sourceLabel )
    {
        static constexpr const utf8* kArrRootChild[] = { "clips" };
        if ( XMLFields::readProperties( root, *this, sourceLabel ) == false || XMLNameCheck::reportUnknownChildren( root, kArrRootChild, sourceLabel ) == false )
            return false;
        const XMLNode clips                 = root.findChild( "clips" );
        const bool    bInvalidAnimationList = clips.isValid() == false || clips.findChild().isValid() == false;
        if ( bInvalidAnimationList )
        {
            SW_LOG_ERROR_ID( "VertexAnimationCooker.VertexAnimationListNeeds", "Vertex animation list '%#' needs a non-empty <clips> list", sourceLabel );
            return false;
        }
        for ( XMLNode clip = clips.findChild(); clip.isValid(); clip = clip.findNextSibling() )
        {
            if ( string_view{ clip.getName() } != "item" )
            {
                SW_LOG_ERROR_ID( "VertexAnimationCooker.VertexAnimationListClips", "Vertex animation list '%#': <clips> must hold <item> names", sourceLabel );
                return false;
            }
            _listClip.push_back( hashed_string( string_view{ clip.getText() } ) );
        }
        // 가리키는 파일이 모두 있어야 한다 — 이름이 틀린 목록이 쿠킹 때 조용히 빠지지 않게.
        if ( AssetIO::hasAsset( AssetID{ _meshPath } ) == false || AssetIO::hasAsset( AssetID{ _skeletonPath } ) == false )
        {
            SW_LOG_ERROR_ID( "VertexAnimationCooker.VertexAnimationListMesh", "Vertex animation list '%#': mesh '%#' or skeleton '%#' does not exist", sourceLabel, _meshPath.c_str(), _skeletonPath.c_str() );
            return false;
        }
        // 쿠킹 작업의 입력으로 기록되어야 하므로 공유 캐시가 아니라 이 자리에서 읽는다(`CookJob` — 캐시가 쥔 한 벌을 쓰면 입력이 빠진다).
        if ( _animationSet.loadCopyFromAsset( AssetID{ _animationSetPath } ) == false )
            return false; // 셋이 까닭을 남겼다
        for ( const hashed_string& clipName : _listClip )
        {
            const string clipPath = makeClipPath( clipName );
            if ( clipPath.empty() || AssetIO::hasAsset( AssetID{ clipPath } ) == false )
            {
                SW_LOG_ERROR_ID( "VertexAnimationCooker.VertexAnimationListClip", "Vertex animation list '%#': clip '%#' does not exist in '%#'", sourceLabel, clipName.c_str(), _animationSetPath.c_str() );
                return false;
            }
        }
        return true;
    }

    string VertexAnimationCookList::makeClipPath( const hashed_string& clipName ) const
    {
        const string* const pClipPath = _animationSet.findClipPath( clipName );
        return pClipPath != nullptr ? *pClipPath : string{};
    }

    bool VertexAnimationCookList::cookClip( const VertexAnimationCookList& list, const hashed_string& clipName, const AbsolutePath& outputPath, float32 framesPerSecond )
    {
        MeshAssetData meshData;
        Skeleton      skeleton;
        if ( MeshAssetFormat::loadFromAsset( AssetID{ list._meshPath }, meshData ) == false )
        {
            SW_LOG_ERROR_ID( "VertexAnimationCooker.VertexAnimationCookCould", "Vertex animation cook: could not read mesh '%#'", list._meshPath.c_str() );
            return false;
        }
        // 스켈레톤 · 클립은 쿠킹 작업의 입력으로 기록되도록 이 자리에서 읽는다(공유 캐시를 거치지 않는다 — `CookJob`).
        if ( skeleton.loadCopyFromAsset( AssetID{ list._skeletonPath } ) == false )
        {
            SW_LOG_ERROR_ID( "VertexAnimationCooker.VertexAnimationCookCould2", "Vertex animation cook: could not read skeleton '%#'", list._skeletonPath.c_str() );
            return false;
        }
        shared_ptr<Mesh> mesh = Mesh::create();
        if ( mesh->setGeometry( std::move( meshData._listVertex ), std::move( meshData._listIndex ) ) == false )
            return false; // 읽기가 범위를 봤다 — 여기서 지면 메시가 오류를 남겼다
        mesh->setSkin( std::move( meshData._listSkinVertex ), std::move( meshData._skinBinding ) );

        AnimationClip       clip;
        MeshVertexAnimation animation;
        const bool          bBaked =
            clip.loadCopyFromAsset( AssetID{ list.makeClipPath( clipName ) } ) && MeshVertexAnimationBaker::bake( *mesh, skeleton, clip, framesPerSecond, false, animation );
        if ( bBaked == false || animation.saveToFile( outputPath ) == false )
        {
            SW_LOG_ERROR_ID( "VertexAnimationCooker.VertexAnimationCookFailed", "Vertex animation cook failed for '%#' clip '%#'", list._meshPath.c_str(), clipName.c_str() );
            return false;
        }
        return true;
    }

    uint32 VertexAnimationCookList::cookList( const VertexAnimationCookList& list, const string& cookedDir, float32 framesPerSecond, uint32& outFailedCount )
    {
        uint32 writtenCount = 0;
        // 쿠킹 폴더가 비었으면 메시 파일 곁에 쓴다 — 상대 키인 메시는 읽기가 검색 순서로 고른 그 파일이다.
        const optional<AbsolutePath> meshPath = cookedDir.empty() ? engine::getSubsystem<AssetPaths>().toDiskPath( list._meshPath ) : nullopt;
        for ( const hashed_string& clipName : list._listClip )
        {
            const string           cookedPath = MeshVertexAnimation::makeCookedPath( list._meshPath, clipName );
            optional<AbsolutePath> outputPath;
            if ( cookedDir.empty() == false )
                outputPath = FileUtil::requireAbsolutePath( FileUtil::joinPath( cookedDir, cookedPath ) );
            else if ( meshPath.has_value() )
                outputPath = FileUtil::requireAbsolutePath( MeshVertexAnimation::makeCookedPath( meshPath->str(), clipName ) );
            if ( outputPath.has_value() == false )
            {
                SW_LOG_ERROR_ID( "VertexAnimationCooker.VATNoDiskFolder", "VAT '%#' has no disk folder beside its mesh to be written to", cookedPath.c_str() );
                ++outFailedCount;
                continue;
            }
            if ( cookClip( list, clipName, *outputPath, framesPerSecond ) == false )
            {
                ++outFailedCount;
                continue;
            }
            ++writtenCount;
        }
        return writtenCount;
    }

    CookJobDesc VertexAnimationCookList::makeCookJobDesc( float32 framesPerSecond )
    {
        CookJobDesc desc;
        desc._cookerName      = "VertexAnimation";
        desc._codeFingerprint = CookCodeFingerprint::find( "VertexAnimation" );
        desc._target          = "Any";
        desc._settingsHash    = BitUtil::floatToBits( framesPerSecond ); // 설정 해시는 프레임율의 비트 그대로다
        return desc;
    }

    uint32 VertexAnimationCookList::cookAll( const string& resourceRoot, const string& cookedDir, float32 framesPerSecond, const CookStore& store,
                                             uint32& outFailedCount )
    {
        vector<string> listFile;
        if ( AssetPaths::collectContentFiles( resourceRoot, ".xml", listFile ) == false )
        {
            SW_LOG_ERROR_ID( "VertexAnimationCooker.VertexAnimationCookCould3", "Vertex animation cook could not list '%#' - nothing is cooked", resourceRoot );
            ++outFailedCount;
            return 0;
        }
        VectorUtil::sort( listFile );
        const string      normalizedRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot ) );
        const string      activePackRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( engine::getSubsystem<GameConfig>()._packRoot.get() ).str() );
        const CookJobDesc desc           = makeCookJobDesc( framesPerSecond );
        CookStats         stats;
        for ( const string& filePath : listFile )
        {
            if ( StringUtil::endsWith( filePath, VertexAnimationCookList::kExtension, true ) == false )
                continue;
            const string normalizedFile = FileUtil::normalizeSeparators( filePath );
            const string relativePath   = normalizedFile.substr( MathUtil::min( normalizedRoot.size() + 1, normalizedFile.size() ) );
            if ( AssetCookPath::isInInactiveGamePack( relativePath, activePackRoot ) )
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
                const string     outputID = MeshVertexAnimation::makeCookedPath( list._meshPath, clipName );
                const CookResult result   = store.cookFile( desc, outputID, FileUtil::joinPath( cookedDir, outputID ), [&]()
                  {
                    VertexAnimationCookList      jobList;
                    const optional<AbsolutePath> outputPath = FileUtil::requireAbsolutePath( FileUtil::joinPath( cookedDir, outputID ) );
                    const bool                   bCooked    = outputPath.has_value() && jobList.loadFromAsset( AssetID{ relativePath } ) && cookClip( jobList, clipName, *outputPath, framesPerSecond );
                    return bCooked ? CookStep::Written : CookStep::Failed;
                } );
                outFailedCount += result == CookResult::Failed ? 1u : 0u;
                stats.add( result );
            }
        }
        SW_LOG_INFO_ID( "VertexAnimationCooker.VertexAnimationCookReused", "Vertex animation cook: %# reused from the DDC, %# cooked", stats._reusedCount, stats._cookedCount );
        return stats.getWrittenCount();
    }
} // namespace sw

namespace sw
{

    /** @brief VAT 쿠킹 목록 데이터 파일 종류입니다(스키마 테스트와 카탈로그 편집기가 이 줄로 파일을 읽는다). */
    SW_ASSET_TYPE( VertexAnimationCookList, VertexAnimationCookList::kAssetType.withPathRule( AssetPathRule{ {}, {}, VertexAnimationCookList::kExtension } ).withLoader( AssetTypeLoader::of<VertexAnimationCookList>() ) );
} // namespace sw
