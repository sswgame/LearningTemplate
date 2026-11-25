#include "pch.h"

#include "DevTools/Cook/VertexAnimationCooker.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "DevTools/Cook/CookStore.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Scene/MeshVertexAnimationBaker.h"
#include "Engine/Animation/Scene/VertexAnimationCookList.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Asset/AssetFormat.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/CookCodeFingerprint.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshVertexAnimation.h"
#include "Engine/Reflection/TypeInfo.h"

namespace sw
{
    bool VertexAnimationCooker::cookClip( const VertexAnimationCookList& list, const hashed_string& clipName, const AbsolutePath& outputPath, float32 framesPerSecond )
    {
        MeshAssetData meshData;
        Skeleton      skeleton;
        if ( MeshAssetFormat::loadFromAsset( list._meshPath.id(), meshData ) == false )
        {
            SW_LOG_ERROR_ID( "VertexAnimationCooker.VertexAnimationCookCould", "Vertex animation cook: could not read mesh '%#'", list._meshPath.c_str() );
            return false;
        }
        // 스켈레톤 · 클립은 쿠킹 작업의 입력으로 기록되도록 이 자리에서 읽는다(공유 캐시를 거치지 않는다 — `CookJob`).
        if ( skeleton.loadCopyFromAsset( list._skeletonPath.id() ) == false )
        {
            SW_LOG_ERROR_ID( "VertexAnimationCooker.VertexAnimationCookCould2", "Vertex animation cook: could not read skeleton '%#'", list._skeletonPath.c_str() );
            return false;
        }
        shared_ptr<Mesh> mesh = Mesh::create( MeshCPUAccess::Keep );
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

    CookJobDesc VertexAnimationCooker::makeCookJobDesc( float32 framesPerSecond )
    {
        CookJobDesc desc;
        desc._cookerName      = "VertexAnimation";
        desc._codeFingerprint = CookCodeFingerprint::find( "VertexAnimation" );
        desc._target          = "Any";
        desc._settingsHash    = BitUtil::floatToBits( framesPerSecond ); // 설정 해시는 프레임율의 비트 그대로다
        return desc;
    }

    uint32 VertexAnimationCooker::cookAll( const AbsolutePath& resourceRoot, const AbsolutePath& cookedDir, float32 framesPerSecond, const CookStore& store,
                                           uint32& outFailedCount )
    {
        vector<AbsolutePath> listFile;
        if ( AssetPaths::collectContentFiles( resourceRoot, ".xml", listFile ) == false )
        {
            SW_LOG_ERROR_ID( "VertexAnimationCooker.VertexAnimationCookCould3", "Vertex animation cook could not list '%#' - nothing is cooked", resourceRoot.str() );
            ++outFailedCount;
            return 0;
        }
        VectorUtil::sort( listFile );
        const string      normalizedRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot.str() ) );
        const string      activePackRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( engine::getSubsystem<GameConfig>()._packRoot.get() ).str() );
        const CookJobDesc desc           = makeCookJobDesc( framesPerSecond );
        CookStats         stats;
        for ( const AbsolutePath& filePath : listFile )
        {
            if ( StringUtil::endsWith( filePath.str(), VertexAnimationCookList::kExtension, true ) == false )
                continue;
            const string normalizedFile = FileUtil::normalizeSeparators( filePath.str() );
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
                const string       outputID   = MeshVertexAnimation::makeCookedPath( list._meshPath, clipName );
                const AbsolutePath outputPath = cookedDir.join( outputID );
                const CookResult   result     = store.cookFile( desc, outputID, outputPath, [&]()
                      {
                    VertexAnimationCookList jobList;
                    const bool              bCooked = jobList.loadFromAsset( AssetID{ relativePath } ) && cookClip( jobList, clipName, outputPath, framesPerSecond );
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
