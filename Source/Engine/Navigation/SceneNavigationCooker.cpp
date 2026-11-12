#include "pch.h"

#include "Engine/Navigation/SceneNavigationCooker.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/AssetFormat.h"
#include "Engine/Asset/Cook/CookStore.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/CookCodeFingerprint.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Navigation/Component/NavMeshSurfaceComponent.h"
#include "Engine/Navigation/NavMeshAsset.h"
#include "Engine/Navigation/Scene/SceneNavigation.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"

namespace sw
{
    namespace
    {
        struct SceneNavigationCookerInternal
        {
            static constexpr string_view kSceneSuffix   = ".scene.xml";
            static constexpr string_view kSurfaceMarker = "NavMeshSurfaceComponent";
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool SceneNavigationCooker::cookScene( const SceneEngineServices& services, const AssetID& assetID, NavMeshAsset& outAsset )
    {
        const string_view sceneAssetPath = assetID.view();
        outAsset                         = NavMeshAsset{};
        SceneDocument document;
        if ( document.loadXML( assetID ) == false )
        {
            SW_LOG_ERROR_ID( "SceneNavigationCooker.NavmeshCookCouldNot", "Navmesh cook could not read scene '%#'", sceneAssetPath );
            return false;
        }
        Scene scene{ "NavMeshCook", services };
        if ( scene.instantiate( document ) == false || scene.getObjectManager() == nullptr )
        {
            SW_LOG_ERROR_ID( "SceneNavigationCooker.NavmeshCookCouldNot2", "Navmesh cook could not build scene '%#'", sceneAssetPath );
            return false;
        }
        GameObjectManager&    manager    = *scene.getObjectManager();
        SceneNavigation&      navigation = manager.getSubsystem<SceneNavigation>();
        vector<hashed_string> listAgentType;
        manager.forEachComponentOfType<NavMeshSurfaceComponent>( SceneWalkKey{}, [&listAgentType]( NavMeshSurfaceComponent* pSurface )
        {
            if ( pSurface->getAgentTypes().empty() )
                listAgentType.push_back( hashed_string{} );
            for ( const hashed_string& agentType : pSurface->getAgentTypes() )
            {
                listAgentType.push_back( agentType );
            }
        } );
        // 매니저 순회는 순서를 약속하지 않는다. 쿠킹 항목이 방문 순서에 따라 달라지지 않게 이름순으로 정렬하고, 같은 에이전트 타입은 한 번만 베이크한다.
        VectorUtil::sort( listAgentType, []( const hashed_string& lhs, const hashed_string& rhs )
        {
            return lhs.view() < rhs.view();
        } );
        listAgentType.erase( std::unique( listAgentType.begin(), listAgentType.end() ), listAgentType.end() );
        bool bAll = true;
        for ( const hashed_string& agentType : listAgentType )
        {
            NavMeshAssetEntry entry;
            if ( navigation.makeCookedEntry( agentType, entry ) == false )
            {
                SW_LOG_ERROR_ID( "SceneNavigationCooker.NavmeshCookCouldNot3", "Navmesh cook could not bake agent type '%#' of scene '%#'", agentType.c_str(), sceneAssetPath );
                bAll = false;
                continue;
            }
            outAsset.setEntry( std::move( entry ) );
        }
        return bAll;
    }

    uint32 SceneNavigationCooker::cookAll( const SceneEngineServices& services, const string& resourceRoot, const string& cookedDir, const CookStore& store, uint32& outFailedCount )
    {
        using Internal = SceneNavigationCookerInternal;
        if ( cookedDir.empty() )
        {
            ++outFailedCount;
            return 0;
        }
        vector<string> listFile;
        if ( AssetPaths::collectContentFiles( resourceRoot, ".xml", listFile ) == false )
        {
            SW_LOG_ERROR_ID( "SceneNavigationCooker.SceneNavigationCookCould", "Scene navigation cook could not list '%#' - nothing is cooked", resourceRoot );
            ++outFailedCount;
            return 0;
        }
        VectorUtil::sort( listFile );
        const string      normalizedRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot ) );
        const string      activePackRoot = FileUtil::trimTrailingSlashes( FileUtil::normalizePath( engine::getSubsystem<GameConfig>()._packRoot.get() ) );
        const CookJobDesc desc           = makeCookJobDesc();
        CookStats         stats;
        for ( const string& filePath : listFile )
        {
            if ( StringUtil::endsWith( filePath, Internal::kSceneSuffix, true ) == false )
                continue;
            const string normalizedFile = FileUtil::normalizeSeparators( filePath );
            const string relativePath   = normalizedFile.substr( MathUtil::min( normalizedRoot.size() + 1, normalizedFile.size() ) );
            if ( AssetCookPath::isInInactiveGamePack( relativePath, activePackRoot ) )
                continue;
            const string     outputID   = NavMeshAsset::makeCookedPath( relativePath );
            const string     cookedPath = FileUtil::joinPath( cookedDir, outputID );
            const CookResult result     = store.cookFile( desc, outputID, cookedPath, [&]()
                {
                // 표면이 없는 씬은 세우지 않는다 — 글에 컴포넌트 이름이 없으면 넘긴다. 작업 안에서 읽어 씬이 이 작업의 입력이 된다.
                string text;
                if ( AssetIO::readTextFile( filePath, text ) == false || text.find( Internal::kSurfaceMarker.data() ) == string::npos )
                    return CookStep::Skipped;
                NavMeshAsset asset;
                if ( cookScene( services, AssetID{ relativePath }, asset ) == false )
                    return CookStep::Failed;
                if ( asset.getEntries().empty() )
                    return CookStep::Skipped;
                const optional<AbsolutePath> cookedFile = FileUtil::requireAbsolutePath( cookedPath );
                if ( cookedFile.has_value() == false || asset.saveToFile( *cookedFile ) == false )
                {
                    SW_LOG_ERROR_ID( "SceneNavigationCooker.NavmeshCookCouldNot4", "Navmesh cook could not write '%#'", cookedPath.c_str() );
                    return CookStep::Failed;
                }
                SW_LOG_INFO_ID( "SceneNavigationCooker.CookedNavmeshAgentTypes", "Cooked navmesh '%#' (%# agent types)", cookedPath.c_str(), asset.getEntries().size() );
                return CookStep::Written;
            } );
            outFailedCount += result == CookResult::Failed ? 1u : 0u;
            stats.add( result );
        }
        SW_LOG_INFO_ID( "SceneNavigationCooker.NavmeshCookReusedDDC", "Navmesh cook: %# reused from the DDC, %# cooked", stats._reusedCount, stats._cookedCount );
        return stats.getWrittenCount();
    }

    CookJobDesc SceneNavigationCooker::makeCookJobDesc()
    {
        CookJobDesc desc;
        desc._cookerName      = "NavMesh";
        desc._codeFingerprint = CookCodeFingerprint::find( "NavMesh" );
        desc._target          = "Any";
        desc._settingsHash    = 0; // 베이크 설정은 리소스(내비 설정 표)라 읽기로 입력에 든다
        return desc;
    }
} // namespace sw
