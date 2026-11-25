#include "pch.h"

#include "DevTools/Cook/SceneHLODMeshCooker.h"

#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/Component/ComponentStableKey.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Visual/Component/HLODMeshComponent.h"

namespace sw
{
    namespace
    {
        struct SceneHLODMeshCookerInternal
        {
            static constexpr string_view kSceneBin          = ".scene.bin";
            static constexpr string_view kMeshSuffix        = ".hlod.mesh";
            static constexpr const utf8* kMergedTypeName    = "HLODMeshComponent";
            static constexpr const utf8* kInstancedTypeName = "InstancedMeshComponent";

            /** @brief 조각의 HLOD 대리 파일 id 들입니다. */
            static unordered_set<uint64> collectProxyFileIDs( const SceneDocument& doc )
            {
                unordered_set<uint64> uniqueProxyID;
                for ( const SceneDocument::StreamingCell& cell : doc._listStreamingCell )
                {
                    if ( cell._hlodFileID != 0 )
                        uniqueProxyID.insert( cell._hlodFileID );
                }
                return uniqueProxyID;
            }

            /** @brief 합친 대리 컴포넌트 하나의 부품을 합쳐 구간 하나로 더합니다. */
            [[nodiscard]] static bool appendSection( const HLODMeshComponent& component, const hashed_string& sectionName, string_view scenePath, MeshAssetData& inoutData )
            {
                const uint32 firstIndex = static_cast<uint32>( inoutData._listIndex.size() );
                for ( const HLODMeshPart& part : component.getParts() )
                {
                    const string&          meshID = part._meshID.path();
                    const shared_ptr<Mesh> mesh   = MeshAssetFormat::isMeshAssetPath( meshID ) ? MeshCache::acquire( meshID, MeshCPUAccess::Keep ) : MeshUtil::acquirePrimitive( meshID );
                    if ( mesh == nullptr || HLODMeshComponent::appendPart( part, *mesh, inoutData._listVertex, inoutData._listIndex ) == false )
                    {
                        SW_LOG_ERROR_ID( "SceneHLODMeshCooker.PartNotMerged", "Scene HLOD mesh cook: '%#' - part '%#' (section '%#') could not be merged", scenePath, meshID, part._sectionName.c_str() );
                        return false;
                    }
                }
                MeshSection section;
                section._name       = sectionName;
                section._firstIndex = firstIndex;
                section._indexCount = static_cast<uint32>( inoutData._listIndex.size() ) - firstIndex;
                inoutData._listSection.push_back( section );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string SceneHLODMeshCooker::makeMeshPath( string_view cookedScenePath )
    {
        using Internal = SceneHLODMeshCookerInternal;
        if ( StringUtil::endsWith( cookedScenePath, Internal::kSceneBin, true ) == false )
            return {};
        StringBuilder<constant::buffer::kMax512> builder;
        builder.appendFormat( "%#%#", cookedScenePath.substr( 0, cookedScenePath.size() - Internal::kSceneBin.size() ), Internal::kMeshSuffix );
        return string( builder.view() );
    }

    hashed_string SceneHLODMeshCooker::makeSectionName( uint64 proxyFileID, uint64 localID )
    {
        StringBuilder<constant::buffer::kMax128> builder;
        builder.appendFormat( "HLOD_%#_%#", proxyFileID, localID );
        return hashed_string( builder.view() );
    }

    optional<uint32> SceneHLODMeshCooker::replaceMergedProxies( SceneDocument& inoutDoc, string_view meshID )
    {
        using Internal                             = SceneHLODMeshCookerInternal;
        const unordered_set<uint64>  uniqueProxyID = Internal::collectProxyFileIDs( inoutDoc );
        uint32                       replacedCount = 0;
        vector<pair<size_t, string>> listState;
        for ( size_t nodeIndex = 0; nodeIndex < inoutDoc._listSceneObjectNode.size(); ++nodeIndex )
        {
            const SceneDocument::SceneObjectNode& node = inoutDoc._listSceneObjectNode[nodeIndex];
            if ( uniqueProxyID.count( node._fileID ) == 0 || node._embeddedXML.find( Internal::kMergedTypeName ) == string::npos )
                continue;
            XMLDocument state;
            if ( state.parse( node._embeddedXML ) == false )
            {
                SW_LOG_ERROR_ID( "SceneHLODMeshCooker.ProxyUnreadable", "Scene HLOD mesh cook: the state of proxy '%#' could not be read back", node._name );
                return nullopt;
            }
            const XMLNode listComponent = state.getRoot().findChild( "_listComponent" );
            for ( XMLNode component = listComponent.findChild( Internal::kMergedTypeName ); component.isValid(); component = listComponent.findChild( Internal::kMergedTypeName ) )
            {
                const optional<uint32> localID = component.readRequiredAttribute<uint32>( "_localID" );
                if ( localID.has_value() == false )
                    return nullopt; // 까닭은 읽기가 남겼다
                component.setName( Internal::kInstancedTypeName );
                component.setAttribute( "_meshID", meshID );
                component.setAttribute( "_sectionName", makeSectionName( node._fileID, *localID ).view() );
                const XMLNode listPart = component.findChild( "_listPart" );
                if ( listPart.isValid() )
                    (void)component.removeChild( listPart );
                ++replacedCount;
            }
            listState.push_back( { nodeIndex, state.getRoot().toString() } );
        }
        for ( pair<size_t, string>& replaced : listState )
        {
            inoutDoc._listSceneObjectNode[replaced.first]._embeddedXML = std::move( replaced.second );
        }
        return replacedCount;
    }

    bool SceneHLODMeshCooker::buildMesh( const SceneEngineSubsystems& services, const SceneDocument& doc, string_view scenePath, MeshAssetData& outData )
    {
        using Internal                            = SceneHLODMeshCookerInternal;
        outData                                   = MeshAssetData{};
        const unordered_set<uint64> uniqueProxyID = Internal::collectProxyFileIDs( doc );
        // 대리만 짓는다 — 원점의 씬 컴포넌트와 그 아래 컴포넌트뿐이라 다른 엔티티가 필요 없다.
        SceneDocument proxyDoc{};
        proxyDoc._name = doc._name;
        for ( const SceneDocument::SceneObjectNode& node : doc._listSceneObjectNode )
        {
            if ( uniqueProxyID.count( node._fileID ) != 0 && node._embeddedXML.find( Internal::kMergedTypeName ) != string::npos )
                proxyDoc._listSceneObjectNode.push_back( node );
        }
        if ( proxyDoc._listSceneObjectNode.empty() )
            return true;
        Scene scene{ "SceneHLODMeshCooker", services };
        scene.setBuildAllDataLayers();
        if ( scene.instantiate( proxyDoc ) == false )
        {
            SW_LOG_ERROR_ID( "SceneHLODMeshCooker.ProxiesNotBuilt", "Scene HLOD mesh cook: the HLOD proxies of '%#' could not be built", scenePath );
            return false;
        }
        unordered_map<uint64, GameObject*> mapObjectByFileID;
        scene.collectObjectsByFileID( mapObjectByFileID );
        for ( const SceneDocument::SceneObjectNode& node : proxyDoc._listSceneObjectNode )
        {
            const auto objectIt = mapObjectByFileID.find( node._fileID );
            if ( objectIt == mapObjectByFileID.end() )
            {
                SW_LOG_ERROR_ID( "SceneHLODMeshCooker.ProxyMissing", "Scene HLOD mesh cook: proxy '%#' of '%#' was not built", node._name, scenePath );
                return false;
            }
            for ( const Component* pComponent : objectIt->second->getComponents() )
            {
                if ( pComponent == nullptr || pComponent->getTypeInfo() != HLODMeshComponent::StaticType() )
                    continue;
                const hashed_string sectionName = makeSectionName( node._fileID, pComponent->getStoredLocalID() );
                if ( Internal::appendSection( *static_cast<const HLODMeshComponent*>( pComponent ), sectionName, scenePath, outData ) == false )
                    return false;
            }
        }
        return true;
    }
} // namespace sw
