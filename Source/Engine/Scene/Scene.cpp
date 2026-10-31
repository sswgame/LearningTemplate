#include "pch.h"

#include "Engine/Scene/Scene.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/unordered_set.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/TagID.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Asset/AssetManager.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Object/Prefab/PrefabInstanceRegistry.h"
#include "Engine/Object/Prefab/PrefabOverrides.h"
#include "Engine/Scene/SceneCooker.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"

namespace sw
{
    /**
     * @brief `-gv_defaultMaterial=<path>` 는 씬 기본 머티리얼을 EngineDefaultAssets 대신 이 경로로 정합니다.
     * @details 벤치와 시각 검증용입니다(예: engine/materials/benchtextured.material 로 텍스처 샘플링 경로를 봅니다).
     *          비어 있으면 EngineDefaultAssets._defaultMaterial 을 씁니다.
     */
    SW_GLOBAL_VARIABLE( sw::string, gv_defaultMaterial, "", "씬 기본 머티리얼 경로 덮어쓰기 (비면 EngineDefaultAssets)" );

    namespace
    {
        struct SceneInternal
        {
            /**
             * @brief 기본 머티리얼 경로를 반환합니다(-gv_defaultMaterial 이 있으면 그것, 없으면 엔진 데이터 설정, 그것도 없으면 빈 문자열).
             */
            static string resolveDefaultMaterialPath( const EngineDefaultAssets* pDefaultAssets )
            {
                // -gv_defaultMaterial 이 있으면 그것이 먼저다(선언은 이 파일 위쪽).
                if ( gv_defaultMaterial.empty() == false )
                    return gv_defaultMaterial;
                return pDefaultAssets != nullptr ? pDefaultAssets->_defaultMaterial.get() : string{};
            }

            /**
             * @brief MeshComponent 의 프리미티브 메시와 머티리얼(저장된 참조, 없으면 씬 기본)을 채우고, 잡은 머티리얼을 @p pRHIDevice 로 업로드합니다.
             * @param pAssetManager 머티리얼 캐시의 업로드 대기를 비울 곳입니다. nullptr 이면 업로드는 건너뜁니다.
             */
            static void bindSceneMeshDefaults( Scene* pScene, AssetManager* pAssetManager, IRHIDevice* pRHIDevice )
            {
                if ( pScene == nullptr )
                    return;
                GameObjectManager* pObjectManager = pScene->getObjectManager();
                if ( pObjectManager == nullptr )
                    return;

                Material* pDefaultMaterial = pScene->getMaterial();
                pObjectManager->forEachGameObject( [&]( GameObject* pObj )
                {
                    if ( pObj == nullptr )
                        return;
                    MeshComponent* pMeshComp = pObj->getComponent<MeshComponent>();
                    if ( pMeshComp == nullptr )
                        return;
                    pMeshComp->resolveRenderAssets();
                    // 인스턴스가 붙은 메시는 건너뛴다. 그 메시의 머티리얼은 인스턴스의 부모이고, GPUSceneBuilder 가 그렇게 고른다.
                    // 여기서 씬 기본을 넣으면 배치가 기본 머티리얼(그룹, 텍스처)과 인스턴스(원소 바이트, 퍼뮤테이션)로 섞인다.
                    const bool bNeedsDefaultMaterial = pMeshComp->getMaterial() == nullptr && pMeshComp->getRawMaterialInstance() == nullptr && pDefaultMaterial != nullptr;
                    if ( bNeedsDefaultMaterial )
                        pMeshComp->setMaterial( pDefaultMaterial );
                } );
                if ( pAssetManager != nullptr )
                    pAssetManager->getAssetCache<MaterialCache>().initializePending( pRHIDevice );
                pObjectManager->flushSceneTransforms();
            }

            /**
             * @brief 켜져 있는 첫 방향광(등록 순서)을 찾습니다. @p bRequireShadow 면 그림자를 드리우는 것만 봅니다.
             * @details 활성 판정은 여기서 한다. 레지스트리는 "무엇이 있나"만 안다(PrimitiveRegistry 와 같은 규약). `Component::isActive` 가
             *          소유 오브젝트의 계층 활성까지 본다.
             */
            static DirectionalLightComponent* findDirectionalLight( const GameObjectManager* pObjectManager, bool bRequireShadow )
            {
                if ( pObjectManager == nullptr )
                    return nullptr;
                for ( LightComponent* pLight : pObjectManager->getComponentRegistry().getAll<LightComponent>( shaderslot::kLightTypeDirectional ) )
                {
                    if ( pLight == nullptr || pLight->isActive() == false )
                        continue;
                    DirectionalLightComponent* pDirectional = static_cast<DirectionalLightComponent*>( pLight );
                    if ( bRequireShadow && pDirectional->castsShadow() == false )
                        continue;
                    return pDirectional;
                }
                return nullptr;
            }

            /**
             * @brief 프리팹의 원형 상태(`PrefabOverrides::makeBaseState`)입니다. 한 번의 로드나 저장 안에서 프리팹마다 한 번 만듭니다. 읽지 못하면 nullptr 입니다.
             * @details 프리팹 캐시(`PrefabCache`)가 아니라 부른 쪽의 테이블에 둔다. 원형은 현재 로드된 컴포넌트 타입으로 만드는 것이라 모듈 리로드를 넘겨
             *          보관하면 낡는다.
             */
            static const string* findPrefabBaseState( AssetManager& assetManager, unordered_map<string, string>& inoutMapBaseState, const string& prefabPath )
            {
                const auto cachedIt = inoutMapBaseState.find( prefabPath );
                if ( cachedIt != inoutMapBaseState.end() )
                    return cachedIt->second.empty() ? nullptr : &cachedIt->second;

                string                              baseState;
                const shared_ptr<const PrefabAsset> pPrefab = assetManager.getAssetCache<PrefabCache>().loadPrefab( prefabPath );
                if ( pPrefab != nullptr && PrefabOverrides::makeBaseState( *pPrefab, baseState ) == false )
                {
                    SW_LOG_WARNING( "Prefab '%#' could not be built as a base state", prefabPath );
                    baseState.clear();
                }
                const string& stored = inoutMapBaseState.emplace( prefabPath, std::move( baseState ) ).first->second;
                return stored.empty() ? nullptr : &stored;
            }

            /** @brief 로드에서 만든 프리팹 엔티티와 그 프리팹 경로 · 덮어쓴 것(문서의 것을 빌린다)입니다 — 엔티티를 모두 만든 뒤 중첩 인스턴스를 스폰한다. */
            struct PrefabEntity
            {
                GameObject*   _pObject;
                const string* _pPrefabPath;
                const string* _pOverrideXML; ///< 부품에 덮어쓴 것(`<Part>`)을 여기서 읽는다
            };

            /**
             * @brief 중첩 프리팹 부품(`PrefabInstanceRegistry`)을 저장 방법으로 나눕니다.
             * @details 바깥 인스턴스가 로드할 때 다시 스폰하는 부품은 @p outUniqueRespawned 에 담고 적지 않는다. 바깥 인스턴스는 씬의 프리팹 엔티티이거나
             *          그 자신이 부품이다(부품은 다시 스폰되거나 자기 프리팹의 엔티티로 적히므로 어느 쪽이든 자기 부품을 다시 스폰한다). 인스턴스 밖으로 옮긴 부품과
             *          다시 스폰할 인스턴스가 없는 부품(연결을 끊은 인스턴스의 부품)은 @p outMapDetachedPrefab 에 원본 경로와 함께 담고, 그 프리팹의 인스턴스로 적는다.
             */
            static void classifyNestedParts( const GameObjectManager& objects, const unordered_map<uint64, string>& mapPrefabSource,
                                             unordered_set<uint64>& outUniqueRespawned, unordered_map<uint64, string>& outMapDetachedPrefab )
            {
                outUniqueRespawned.clear();
                outMapDetachedPrefab.clear();
                const PrefabInstanceRegistry& registry = objects.getSubsystem<PrefabInstanceRegistry>();
                if ( registry.getNestedPartCount() == 0 )
                    return;
                vector<GameObject*> listObject;
                objects.getAllGameObjects( listObject );
                for ( const GameObject* pGo : listObject )
                {
                    const PrefabNestedPart* pPart = pGo != nullptr ? registry.findNestedPart( pGo->getObjectID() ) : nullptr;
                    if ( pPart == nullptr )
                        continue;
                    const bool bOwnerRespawns = mapPrefabSource.find( pPart->_ownerObjectID ) != mapPrefabSource.end() ||
                                                registry.findNestedPart( pPart->_ownerObjectID ) != nullptr;
                    if ( bOwnerRespawns && registry.isAttachedNestedPart( pGo ) )
                        outUniqueRespawned.insert( pGo->getObjectID() );
                    else if ( mapPrefabSource.find( pGo->getObjectID() ) == mapPrefabSource.end() )
                        outMapDetachedPrefab[pGo->getObjectID()] = pPart->_prefabPath;
                }
            }

            /**
             * @brief 씬에 적는 프리팹 인스턴스(@p mapInstancePrefab: 오브젝트 id → 프리팹 경로)마다 부품에 덮어쓴 것을 @p inoutMapPartOverride 에 담습니다.
             * @details 덮어쓴 것이 없는 인스턴스는 담지 않는다. 프리팹을 읽지 못한 인스턴스는 전체 상태로 적히므로(부품은 다시 스폰되지 않는다) 건너뛴다.
             */
            static void collectInstancePartOverrides( const GameObjectManager& objects, PrefabCache& prefabs, const unordered_map<uint64, string>& mapInstancePrefab,
                                                      unordered_map<uint64, vector<PrefabPartOverride>>& inoutMapPartOverride )
            {
                if ( objects.getSubsystem<PrefabInstanceRegistry>().getNestedPartCount() == 0 )
                    return;
                vector<PrefabPartOverride> listPartOverride;
                for ( const auto& [objectID, prefabPath] : mapInstancePrefab )
                {
                    const GameObject*                   pInstance = objects.findGameObjectByID( objectID );
                    const shared_ptr<const PrefabAsset> pPrefab   = pInstance != nullptr ? prefabs.loadPrefab( prefabPath ) : nullptr;
                    if ( pPrefab == nullptr )
                        continue;
                    if ( prefabs.collectPartOverrides( *pInstance, *pPrefab, listPartOverride ) == false )
                    {
                        SW_LOG_ERROR( "Nested part overrides of '%#' could not be computed - its parts are saved as the prefab has them", pInstance->getName().c_str() );
                        continue;
                    }
                    if ( listPartOverride.empty() == false )
                        inoutMapPartOverride[objectID] = std::move( listPartOverride );
                }
            }

            /**
             * @brief 엔티티 하나의 오브젝트를 만듭니다. 프리팹 엔티티는 **원형에 덮어쓴 것을 얹은 상태**로 한 번에 만든다(@p outPrefabInstanceState). 프리팹을 고치면 놓인 인스턴스에 퍼진다(언리얼과 유니티의
             *        프리팹 인스턴스). 전체 상태가 실린 엔티티는 그 상태가 기준이라 원형을 만들지 않는다(원형은 버려지고, 만드는 동안 컴포넌트가 한 벌 더 생긴다) — 프리팹이 있는지만 본다.
             * @return 만든 오브젝트입니다. 프리팹을 찾지 못하면 nullptr 이고, 버리지 않는다 — 버리면 다음 저장이 파일에서 지운다(덮어쓴 값과 프리팹 GUID 까지).
             *         문서 그대로 @p inoutListUnresolved 에 보관했다가 저장할 때 다시 써 넣는다(유니티의 "Missing Prefab" 과 같다). 스트리밍 셀의 엔티티는 보관하지 않는다.
             */
            static GameObject* createEntityObject( GameObjectManager& objects, AssetManager* pAssetManager, unordered_map<string, string>& inoutMapBaseState,
                                                   const SceneDocument::SceneObjectNode& entity, bool bStreamingCell, vector<SceneDocument::SceneObjectNode>& inoutListUnresolved,
                                                   string& outPrefabInstanceState )
            {
                if ( entity._prefab.empty() )
                    return objects.createGameObject( hashed_string( entity._name.c_str() ) );

                const bool bHasSavedState = entity._embeddedStateBytes.empty() == false || entity._embeddedXML.empty() == false;
                // 에셋 매니저가 없으면 프리팹을 찾을 곳이 없다 — 둘 다 건너뛰어 아래의 "Missing Prefab" 으로 보관한다.
                const bool bHasPrefabCache = pAssetManager != nullptr;
                bool       bStateMade      = false;
                if ( bHasPrefabCache && bHasSavedState )
                {
                    bStateMade = pAssetManager->getAssetCache<PrefabCache>().loadPrefab( entity._prefab ) != nullptr;
                }
                else if ( bHasPrefabCache )
                {
                    const string* pBaseState = findPrefabBaseState( *pAssetManager, inoutMapBaseState, entity._prefab );
                    bStateMade               = pBaseState != nullptr && PrefabOverrides::makeInstanceState( *pBaseState, entity._prefabOverrideXML, entity._name, outPrefabInstanceState );
                }
                GameObject* pGo = bStateMade ? objects.createGameObject( hashed_string( entity._name.c_str() ) ) : nullptr;
                if ( pGo == nullptr )
                {
                    SW_LOG_WARNING( "Prefab spawn failed for entity '%#' (%#, guid %#) - kept as-is and written back on save", entity._name, entity._prefab,
                                    entity._prefabGUID.empty() ? "none" : entity._prefabGUID.c_str() );
                    if ( bStreamingCell == false )
                        inoutListUnresolved.push_back( entity );
                }
                return pGo;
            }

            /**
             * @brief 엔티티의 상태를 오브젝트에 얹습니다. **쿠킹된 바이너리 상태가 있으면 그것이 기준이다.** 쿠커가 왕복 검증에 성공한 엔티티만 이쪽에 담고 XML 을 비우므로, 둘 다 차 있는 문서는 없다.
             *        상태가 없으면 빈 엔티티라 다른 엔티티가 파일 id 로 가리킬 수 있게 배치에 기록만 한다.
             */
            static void applyEntityState( GameObject* pGo, const SceneDocument::SceneObjectNode& entity, const string& prefabInstanceState, ObjectStateBatch& batch )
            {
                ObjectLoadContext context{ SerializePurpose::Authoring };
                context._pBatch  = &batch;
                context._savedID = entity._fileID;
                if ( entity._embeddedStateBytes.empty() == false )
                {
                    if ( ObjectStateSerializer::loadFromBinaryBuffer( pGo, entity._embeddedStateBytes.data(), entity._embeddedStateBytes.size(), context ) == 0 )
                        SW_LOG_WARNING( "Embedded binary state apply failed for '%#'", entity._name );
                }
                else if ( entity._embeddedXML.empty() == false )
                {
                    if ( ObjectStateSerializer::loadFromXMLString( pGo, entity._embeddedXML, context ) == false )
                        SW_LOG_WARNING( "Embedded state apply failed for '%#'", entity._name );
                }
                else if ( prefabInstanceState.empty() == false )
                {
                    // 원형은 다른 오브젝트로의 부착과 핸들을 싣지 않는다(`PrefabOverrides::makeBaseState`). 다른 엔티티에 붙은 인스턴스의 부착은
                    // 덮어쓴 값이라 파일 id 로 들어 있고, 배치가 모두 읽은 뒤 연결한다.
                    if ( ObjectStateSerializer::loadFromXMLString( pGo, prefabInstanceState, context ) == false )
                        SW_LOG_WARNING( "Prefab instance state apply failed for '%#' (%#)", entity._name, entity._prefab );
                }
                else
                {
                    batch.add( pGo, entity._fileID, hashed_string( entity._name.c_str() ), false );
                }
            }

            /**
             * @brief 프리팹의 중첩 인스턴스를 부품으로 다시 스폰합니다. 중첩 인스턴스는 씬 파일에 없다 — 원본에서 부품으로 다시 스폰한다(원본을 고치면 퍼진다).
             * @details 엔티티를 모두 만든 **뒤에** 스폰한다. 부품이 먼저 이름을 가져가면 같은 이름의 엔티티가 로드마다 번호 붙은 이름으로 바뀐다. 프리팹은 앞에서 이미 읽어 캐시에 있다.
             *          씬에서 부품을 고친 것은 엔티티의 덮어쓴 것 안 `<Part>` 로 실려 스폰하는 부품에 얹는다(유니티 nested override).
             */
            static void spawnNestedInstances( AssetManager& assetManager, const vector<PrefabEntity>& listPrefabEntity )
            {
                PrefabCache&               prefabs = assetManager.getAssetCache<PrefabCache>();
                vector<PrefabPartOverride> listPartOverride;
                for ( const PrefabEntity& prefabEntity : listPrefabEntity )
                {
                    const shared_ptr<const PrefabAsset> pPrefab = prefabs.loadPrefab( *prefabEntity._pPrefabPath );
                    if ( pPrefab == nullptr )
                        continue;
                    if ( PrefabOverrides::collectPartOverrides( *prefabEntity._pOverrideXML, listPartOverride ) == false )
                        SW_LOG_ERROR( "Nested part overrides of '%#' could not be read - its parts are spawned as the prefab has them", prefabEntity._pObject->getName().c_str() );
                    (void)prefabs.spawnNestedInstances( prefabEntity._pObject, *pPrefab, listPartOverride ); // 건너뛴 노드는 노드마다 경고했다
                }
            }

            /** @brief 문서로 쓸 때 오브젝트 순회 **밖에서** 미리 정해 두는 입력입니다(순회 안에서는 어느 매니저에도 오브젝트를 만들 수 없다 — `GameObjectManager::WalkScope`). */
            struct SaveInputs
            {
                ObjectSavedIDMap                                  _mapSavedID;            ///< 파일 id 를 먼저 모두 정한다. 자식의 부착이 부모의 파일 id 를 적으므로, 쓰는 동안 부모의 id 가 이미 있어야 한다.
                unordered_map<string, string>                     _mapPrefabBaseState;    ///< 프리팹 원형의 상태입니다(원형은 임시 매니저에 오브젝트를 만들어 만든다).
                unordered_set<uint64>                             _uniqueRespawnedPart;   ///< 다시 스폰될 중첩 프리팹 부품입니다 — 적지 않는다.
                unordered_map<uint64, string>                     _mapDetachedPartPrefab; ///< 다시 스폰될 곳을 잃은 부품이 자기 프리팹의 인스턴스로 적을 경로입니다.
                unordered_map<uint64, vector<PrefabPartOverride>> _mapPartOverride;       ///< 인스턴스마다 부품에 덮어쓴 것(`<Part>`)입니다.
            };

            /**
             * @brief @p inoutInputs 의 프리팹 관련 입력(부품 분류 · 원형 상태 · 부품 덮어쓰기)을 채웁니다. 만들지 못한 프리팹의 원형은 빈 문자열로 남아 전체 상태를 적게 됩니다.
             */
            static void prepareSavePrefabInputs( const GameObjectManager& objects, AssetManager* pAssetManager, const unordered_map<uint64, string>& mapPrefabSource, SaveInputs& inoutInputs )
            {
                // 중첩 프리팹 부품마다 저장 방법을 정한다. 다시 스폰될 부품은 적지 않고, 다시 스폰될 곳을 잃은 부품은 자기 프리팹의 인스턴스로 적는다.
                classifyNestedParts( objects, mapPrefabSource, inoutInputs._uniqueRespawnedPart, inoutInputs._mapDetachedPartPrefab );
                if ( pAssetManager == nullptr )
                    return;
                for ( const auto& [objectID, prefabPath] : mapPrefabSource )
                {
                    (void)objectID;
                    (void)findPrefabBaseState( *pAssetManager, inoutInputs._mapPrefabBaseState, prefabPath ); // 캐시를 미리 채운다 — 못 찾은 것은 쓰는 쪽이 다시 묻는다
                }
                for ( const auto& [objectID, prefabPath] : inoutInputs._mapDetachedPartPrefab )
                {
                    (void)objectID;
                    (void)findPrefabBaseState( *pAssetManager, inoutInputs._mapPrefabBaseState, prefabPath ); // 캐시를 미리 채운다 — 못 찾은 것은 쓰는 쪽이 다시 묻는다
                }
                // 인스턴스마다 부품에 덮어쓴 것도 순회 밖에서 뽑는다 — 기준 부품을 임시 매니저에 스폰한다.
                collectInstancePartOverrides( objects, pAssetManager->getAssetCache<PrefabCache>(), mapPrefabSource, inoutInputs._mapPartOverride );
                collectInstancePartOverrides( objects, pAssetManager->getAssetCache<PrefabCache>(), inoutInputs._mapDetachedPartPrefab, inoutInputs._mapPartOverride );
            }

            /**
             * @brief 오브젝트 하나를 문서의 엔티티로 씁니다. 프리팹 인스턴스는 원형과 다른 것만 적고, 프리팹을 읽지 못했으면 전체 상태를 적습니다(다음 로드가 그 상태로 만든다).
             * @return 문서에 넣을 엔티티가 만들어졌으면 true 입니다(적을 상태도 프리팹도 없으면 false).
             */
            [[nodiscard]] static bool writeEntityNode( GameObject* pGo, AssetManager* pAssetManager, const unordered_map<uint64, string>& mapPrefabSource, const SaveInputs& inputs,
                                                       const ObjectSaveOptions& saveOptions, SceneDocument::SceneObjectNode& outNode )
            {
                outNode._name        = pGo->getName().c_str();
                const auto savedIDIt = inputs._mapSavedID.find( pGo->getObjectID() );
                outNode._fileID      = ( savedIDIt != inputs._mapSavedID.end() ) ? savedIDIt->second : 0;

                const auto prefabIt = mapPrefabSource.find( pGo->getObjectID() );
                if ( prefabIt != mapPrefabSource.end() )
                    outNode._prefab = prefabIt->second;
                const auto detachedIt = inputs._mapDetachedPartPrefab.find( pGo->getObjectID() );
                if ( outNode._prefab.empty() && detachedIt != inputs._mapDetachedPartPrefab.end() )
                    outNode._prefab = detachedIt->second;

                if ( outNode._prefab.empty() == false && pAssetManager != nullptr )
                {
                    const UUID guid = pAssetManager->getAssetDatabase().ensureMeta( outNode._prefab );
                    if ( guid.isNull() == false )
                        outNode._prefabGUID = guid.toString();
                }
                const string state = ObjectStateSerializer::saveToXMLString( pGo, saveOptions );
                // 프리팹 인스턴스는 원형과 다른 것만 적는다. 프리팹을 읽지 못했으면 전체 상태를 적는다. 다음 로드가 그 상태로 만든다.
                const auto    baseIt     = outNode._prefab.empty() ? inputs._mapPrefabBaseState.end() : inputs._mapPrefabBaseState.find( outNode._prefab );
                const string* pBaseState = ( baseIt != inputs._mapPrefabBaseState.end() && baseIt->second.empty() == false ) ? &baseIt->second : nullptr;
                string        overridesError;
                const bool    bOverridesMade = pBaseState != nullptr && PrefabOverrides::computeOverrides( state, *pBaseState, outNode._prefabOverrideXML, overridesError );
                if ( bOverridesMade == false )
                {
                    if ( overridesError.empty() == false )
                        SW_LOG_WARNING( "Prefab instance '%#' is saved with its full state because its overrides could not be computed: %#", outNode._name, overridesError.c_str() );
                    outNode._prefabOverrideXML.clear();
                    outNode._embeddedXML = state;
                }
                const auto partIt = outNode._embeddedXML.empty() ? inputs._mapPartOverride.find( pGo->getObjectID() ) : inputs._mapPartOverride.end();
                if ( partIt != inputs._mapPartOverride.end() && PrefabOverrides::appendPartOverrides( outNode._prefabOverrideXML, partIt->second ) == false )
                    SW_LOG_ERROR( "Nested part overrides of '%#' could not be written - its parts are saved as the prefab has them", outNode._name );
                return outNode._embeddedXML.empty() == false || outNode._prefab.empty() == false;
            }
        };

        struct SceneDevCommandsInternal
        {
            static bool runTeleport( const vector<string>& listArgument, string& outReply )
            {
                float3     position{};
                const bool bInvalidMoveArguments = listArgument.size() != 4 || StringUtil::parseFloat( listArgument[1], position._x ) == false ||
                                                   StringUtil::parseFloat( listArgument[2], position._y ) == false || StringUtil::parseFloat( listArgument[3], position._z ) == false;
                if ( bInvalidMoveArguments )
                    return false;
                GameObject*     pObject   = engine::findActiveObject( hashed_string( listArgument[0] ) );
                SceneComponent* pRootNode = pObject != nullptr ? pObject->getComponent<SceneComponent>() : nullptr;
                if ( pRootNode == nullptr )
                {
                    outReply = "no object named '" + listArgument[0] + "' with a transform in the active scene";
                    return false;
                }
                pRootNode->teleportTo( position );
                outReply = "teleported " + listArgument[0];
                return true;
            }

            static bool runTagAdd( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 2 || listArgument[1].empty() )
                    return false;
                GameObject* pObject = engine::findActiveObject( hashed_string( listArgument[0] ) );
                if ( pObject == nullptr )
                {
                    outReply = "no object named '" + listArgument[0] + "' in the active scene";
                    return false;
                }
                pObject->addTag( TagID::request( listArgument[1] ) );
                outReply = "tagged " + listArgument[0] + " with " + listArgument[1];
                return true;
            }

            /**
             * @brief `world.cells` — 스트리밍 중이면 올라 있는 셀 수를, 아니면 활성 씬을 쿠커처럼 셀로 나눠 셀 수 · 늘 올라 있는 엔티티 수를 알립니다.
             * @details 나누지 못하면(셀을 넘는 참조) 쿠커와 같은 오류가 엔티티마다 로그에 남는다 — 쿠킹 전에 에디터에서 위반을 본다.
             */
            static bool runStreamingCells( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                SceneManager* pManager = engine::findSubsystem<SceneManager>();
                Scene*        pScene   = pManager != nullptr ? pManager->getActiveScene() : nullptr;
                if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
                {
                    outReply = "no active scene";
                    return false;
                }
                const SceneStreamingSubsystem& streaming = pScene->getObjectManager()->getSubsystem<SceneStreamingSubsystem>();
                if ( streaming.isStreaming() )
                {
                    outReply = formatToString( "streaming: %# of %# cells loaded", streaming.getLoadedCellCount(), streaming.getCellCount() );
                    return true;
                }
                if ( pScene->getSceneSettings()._bStreaming == false )
                {
                    outReply = "the active scene does not stream";
                    return true;
                }
                StreamingCellPreview preview{};
                if ( SceneCooker::previewStreamingCells( *pScene, preview ) == false )
                {
                    outReply = "the active scene could not be split into cells - see the log";
                    return false;
                }
                if ( preview._listViolation.empty() == false )
                {
                    outReply = "the cook would fail - " + to_string( static_cast<uint32>( preview._listViolation.size() ) ) +
                               " cross-cell references (see the log or the Streaming Cells window)";
                    return false;
                }
                outReply = to_string( static_cast<uint32>( preview._listCell.size() ) ) + " cells, " +
                           formatToString( "%# entities always loaded", static_cast<uint32>( preview._listAlwaysLoadedID.size() ) );
                return true;
            }

            static bool runSceneLeakCurve( const vector<string>& listArgument, string& outReply )
            {
                constexpr int32 kDefaultMeasuredRounds = 8;
                int32           measuredRounds         = kDefaultMeasuredRounds;
                const bool      bInvalidArguments      = listArgument.empty() || listArgument.size() > 2 || ( listArgument.size() == 2 && StringUtil::parseInt( listArgument[1], measuredRounds ) == false ) || measuredRounds < 1;
                if ( bInvalidArguments )
                    return false;
                SceneManager* pManager = &engine::getSubsystem<SceneManager>();
                if ( pManager->startLoadLeakProbe( listArgument[0], static_cast<uint32>( measuredRounds ) ) == false )
                {
                    outReply = "could not start (see the log)";
                    return false;
                }
                outReply = "scene leak probe started - the verdict goes to the log when the rounds finish";
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "Scene" );

    Scene::Scene( string_view name )
        : Scene( name, SceneEngineServices::makeFromBoundEngine() )
    {
    }

    Scene::Scene( string_view name, const SceneEngineServices& services )
        : _name{ name }
        , _sourcePath{}
        , _defaultMaterialPath{}
        , _sceneSettings{}
        , _services{ services }
        , _objectManager{ make_unique<GameObjectManager>( services ) }
        , _pMaterial{ nullptr }
        , _activeGameCamera{}
        , _gameCameraOverride{}
    {
    }

    Scene::~Scene()
    {
        shutdown();
    }

    /**
     * @brief 씬을 초기화합니다. 기본 머티리얼을 얻고 기본 카메라를 설정합니다.
     */
    bool Scene::initialize( IRHIDevice* pRHIDevice )
    {
        // 에셋 매니저가 없으면 기본 머티리얼을 잡을 곳도 없다 — 카메라와 메시 기본값만 채운다.
        const string materialPath = _services._pAssetManager != nullptr ? SceneInternal::resolveDefaultMaterialPath( _services._pDefaultAssets ) : string{};
        if ( materialPath.empty() == false )
        {
            if ( _pMaterial == nullptr || _defaultMaterialPath != materialPath )
            {
                releaseDefaultMaterial();
                _defaultMaterialPath = materialPath;
                _pMaterial           = _services._pAssetManager->getAssetCache<MaterialCache>().acquire( materialPath, pRHIDevice );
                if ( _pMaterial == nullptr )
                {
                    SW_LOG_ERROR( "Failed to acquire Material from %#", materialPath );
                    _defaultMaterialPath.clear();
                    return false;
                }
            }
        }
        // 카메라는 프레임마다 다시 고른다 — 여기서 못 고르면(오브젝트 관리자 없음) 렌더 경로가 "카메라 없음" 으로 알린다.
        pickActiveGameCamera();
        SceneInternal::bindSceneMeshDefaults( this, _services._pAssetManager, pRHIDevice );
        return true;
    }

    bool Scene::instantiate( const SceneDocument& doc, const StopToken& stopToken )
    {
        if ( _objectManager == nullptr )
            return false;
        if ( doc._sceneSettings.validate( _name ) == false )
            return false;
        _sceneSettings = doc._sceneSettings;
        // 서비스가 쿠킹본(내비메시 `<씬>.navmesh` 등)을 찾는 이름이다.
        _objectManager->notifySceneLoading( doc._sourcePath.empty() ? string_view{ _sourcePath } : string_view{ doc._sourcePath } );

        if ( instantiateNodes( doc._listSceneObjectNode, false, nullptr, stopToken ) == false )
            return false;
        // 셀로 나눈 쿠킹본이면 셀은 지금 짓지 않는다 — 스트리밍 원점이 다가올 때 서비스가 짓는다(`instantiateCell`).
        if ( doc._listStreamingCell.empty() )
            return true;
        return _objectManager->getSubsystem<SceneStreamingSubsystem>().beginWorld( *this, _sceneSettings, doc._listStreamingCell );
    }

    bool Scene::instantiateCell( const vector<SceneDocument::SceneObjectNode>& listNode, vector<uint64>& outListObjectID )
    {
        if ( _objectManager == nullptr )
            return false;
        return instantiateNodes( listNode, true, &outListObjectID );
    }

    void Scene::unloadCell( const vector<uint64>& listObjectID )
    {
        if ( _objectManager == nullptr )
            return;
        for ( const uint64 objectID : listObjectID )
        {
            _mapObjectIDToFileID.erase( objectID );
            _mapPrefabSource.erase( objectID );
            GameObject* pGo = _objectManager->findGameObjectByID( objectID );
            // 자식은 같은 셀의 엔티티이거나 이 엔티티의 중첩 프리팹 부품이다 — 늘 올라 있는 오브젝트는 셀의 오브젝트에 붙지 않는다(쿠커가 검사했다).
            if ( pGo != nullptr )
                _objectManager->destroyObject( pGo, true );
        }
    }

    bool Scene::instantiateNodes( const vector<SceneDocument::SceneObjectNode>& listNode, bool bStreamingCell, vector<uint64>* pOutListObjectID, const StopToken& stopToken )
    {
        // 오브젝트 **사이의** 부착은 모든 엔티티가 생긴 뒤라야 해석할 수 있다. 엔티티를 모두 하나의 배치로 읽고 끝에서 연결한다. 부착은 부모를
        // 그 엔티티의 **파일 id** 로 가리킨다(파일 안에서만 의미가 있는 값이라 배치 안에서만 해석한다). 이름으로 찾으면 이름이 겹친
        // 엔티티(병합)에서 자식이 앞의 것에 붙는다.
        ObjectStateBatch batch( ObjectIDSpace::Saved );
        // 프리팹마다 원형 상태를 한 번만 만든다(같은 프리팹을 여럿 놓은 씬).
        unordered_map<string, string>       mapPrefabBaseState;
        vector<SceneInternal::PrefabEntity> listPrefabEntity;

        // 셀은 이미 지은 늘 올라 있는 엔티티를 파일 id 로 가리킨다 — 그 오브젝트를 같은 배치에 기록해 둔다(상태는 읽지 않는다).
        if ( bStreamingCell )
        {
            for ( const auto& [objectID, fileID] : _mapObjectIDToFileID )
            {
                GameObject* pExisting = _objectManager->findGameObjectByID( objectID );
                if ( pExisting != nullptr && pExisting->isPendingDestroy() == false )
                    batch.add( pExisting, fileID, pExisting->getName(), false );
            }
        }

        for ( const SceneDocument::SceneObjectNode& entity : listNode )
        {
            // 종료 신호(`SceneManager::requestAsyncLoadStop`) — 중간에 멈추면 false 다. 이미 지은 오브젝트는 부른 쪽이 씬을 버리며 정리한다(비동기 로드 잡이 신호를 다시 읽고 씬을 소멸시킨다).
            if ( stopToken.isStopRequested() )
                return false;
            SW_LOG_TRACE( "Spawning entity '%#' prefab '%#'", entity._name, entity._prefab );
            string      prefabInstanceState; // 프리팹 원형에 덮어쓴 값을 얹어 만든 상태(프리팹 엔티티만)
            GameObject* pGo = SceneInternal::createEntityObject( *_objectManager, _services._pAssetManager, mapPrefabBaseState, entity, bStreamingCell, _listUnresolvedEntity, prefabInstanceState );
            if ( pGo == nullptr )
                continue;

            if ( pOutListObjectID != nullptr )
                pOutListObjectID->push_back( pGo->getObjectID() );
            if ( entity._prefab.empty() == false )
                _mapPrefabSource[pGo->getObjectID()] = entity._prefab;
            if ( entity._fileID != 0 )
                _mapObjectIDToFileID[pGo->getObjectID()] = entity._fileID;
            SceneInternal::applyEntityState( pGo, entity, prefabInstanceState, batch );
            if ( entity._prefab.empty() == false )
                listPrefabEntity.push_back( SceneInternal::PrefabEntity{ pGo, &entity._prefab, &entity._prefabOverrideXML } );
        }

        // 옮기지 못한 핸들 PROPERTY 가 있으면 로드 오류다(배치가 필드를 적어 오류를 남겼다). 비우고 이어 가면 값이 조용히 사라진다.
        if ( batch.finish() == false )
        {
            SW_LOG_ERROR( "Scene '%#' could not link its object handles - the load fails", _name.c_str() );
            return false;
        }

        if ( _services._pAssetManager != nullptr )
            SceneInternal::spawnNestedInstances( *_services._pAssetManager, listPrefabEntity );

        _objectManager->mergePendingAdds();
        _objectManager->flushSceneTransforms();
        return true;
    }

    bool Scene::serializeToDocument( SceneDocument& outDoc ) const
    {
        if ( _objectManager == nullptr )
            return false;
        // 셀을 스트리밍하는 씬(쿠킹본)은 내려가 있는 셀의 엔티티를 들고 있지 않다 — 쓰면 그 엔티티가 파일에서 사라진다.
        if ( _objectManager->getSubsystem<SceneStreamingSubsystem>().isStreaming() )
        {
            SW_LOG_ERROR( "Scene '%#' streams cells from a cooked scene - it cannot be written as a document", _name.c_str() );
            return false;
        }

        outDoc._name          = _name;
        outDoc._sourcePath    = _sourcePath;
        outDoc._sceneSettings = _sceneSettings;
        outDoc._listStreamingCell.clear();
        outDoc._listSceneObjectNode.clear();
        outDoc._bValid = true;

        SceneInternal::SaveInputs inputs;
        collectSavedIDMap( inputs._mapSavedID );
        ObjectSaveOptions saveOptions{ SerializePurpose::Authoring };
        saveOptions._pSavedIDMap          = &inputs._mapSavedID;
        AssetManager* const pAssetManager = _services._pAssetManager;
        SceneInternal::prepareSavePrefabInputs( *_objectManager, pAssetManager, _mapPrefabSource, inputs );

        // **자식 오브젝트도 자기 엔티티로 적는다.** 오브젝트 상태에는 자식 목록이 없고, 자식은 자기 씬 컴포넌트의 부착 필드(부모의 파일 id)로
        // 읽은 뒤 다시 붙는다(`instantiate` 의 배치이므로 순서는 상관없다). 부모가 있는 오브젝트를 건너뛰면 계층 아래의 오브젝트가
        // 저장할 때마다 파일에서 사라진다.
        _objectManager->forEachGameObject( [&]( GameObject* pGo )
        {
            if ( pGo == nullptr )
                return;
            CameraComponent* pCamera = pGo->getComponent<CameraComponent>();
            if ( pCamera != nullptr && pCamera->getRole() == CameraRole::Viewport )
                return;
            if ( inputs._uniqueRespawnedPart.find( pGo->getObjectID() ) != inputs._uniqueRespawnedPart.end() )
                return; // 바깥 프리팹 인스턴스가 로드할 때 다시 스폰한다
            SceneDocument::SceneObjectNode node{};
            if ( SceneInternal::writeEntityNode( pGo, pAssetManager, _mapPrefabSource, inputs, saveOptions, node ) )
                outDoc._listSceneObjectNode.push_back( std::move( node ) );
        } );
        // 프리팹을 찾지 못한 엔티티는 읽은 그대로 다시 쓴다(`instantiate` 설명). 파일 id 도 그대로다. 그 자식들이 그 id 로 가리킨다.
        for ( const SceneDocument::SceneObjectNode& unresolved : _listUnresolvedEntity )
        {
            outDoc._listSceneObjectNode.push_back( unresolved );
        }
        return true;
    }

    void Scene::collectSavedIDMap( ObjectSavedIDMap& outMap ) const
    {
        outMap.clear();
        if ( _objectManager == nullptr )
            return;

        uint64              nextFileID = computeNextFileID();
        vector<GameObject*> listObject;
        _objectManager->getAllGameObjects( listObject );
        outMap.reserve( listObject.size() );
        for ( const GameObject* pGo : listObject )
        {
            if ( pGo == nullptr )
                continue;
            const auto mapIt = _mapObjectIDToFileID.find( pGo->getObjectID() );
            if ( mapIt != _mapObjectIDToFileID.end() )
            {
                outMap.emplace( pGo->getObjectID(), mapIt->second );
                continue;
            }
            _mapObjectIDToFileID.emplace( pGo->getObjectID(), nextFileID );
            outMap.emplace( pGo->getObjectID(), nextFileID );
            ++nextFileID;
        }
    }

    uint64 Scene::getOrAssignFileID( uint64 objectID ) const
    {
        const GameObject* pObject = _objectManager != nullptr ? _objectManager->resolveGameObject( GameObjectHandle::make( objectID ) ) : nullptr;
        if ( pObject == nullptr )
            return 0;
        // 인스턴스가 다시 스폰하는 중첩 부품은 씬이 엔티티로 저장하지 않는다(`classifyNestedParts`) — 파일 id 를 줘도 다음 로드에 대상이 없다.
        // 프리팹 쪽에서 오브젝트가 씬 파일 밖으로 옮겨진 모양이라 참조를 만들 수 없게 막는다.
        if ( _objectManager->getSubsystem<PrefabInstanceRegistry>().isAttachedNestedPart( pObject ) )
            return 0;
        const auto mapIt = _mapObjectIDToFileID.find( objectID );
        if ( mapIt != _mapObjectIDToFileID.end() )
            return mapIt->second;
        const uint64 nextFileID = computeNextFileID();
        _mapObjectIDToFileID.emplace( objectID, nextFileID );
        return nextFileID;
    }

    uint64 Scene::findFileID( uint64 objectID ) const
    {
        const auto mapIt = _mapObjectIDToFileID.find( objectID );
        return mapIt != _mapObjectIDToFileID.end() ? mapIt->second : 0;
    }

    uint64 Scene::computeNextFileID() const
    {
        // 새 id 는 이 씬이 지금껏 쓴 어느 파일 id 보다 크다. 사라진 오브젝트나 해석하지 못한 엔티티의 id 를 다시 주면 그것을 가리키던 참조가
        // (파일을 넘는 참조 `SceneObjectReference` 를 포함해) 새 오브젝트에 붙는다.
        uint64 nextFileID = 1;
        for ( const auto& [objectID, fileID] : _mapObjectIDToFileID )
        {
            (void)objectID;
            nextFileID = MathUtil::max( nextFileID, fileID + 1 );
        }
        for ( const SceneDocument::SceneObjectNode& unresolved : _listUnresolvedEntity )
        {
            nextFileID = MathUtil::max( nextFileID, unresolved._fileID + 1 );
        }
        return nextFileID;
    }

    uint64 Scene::findObjectIDByFileID( uint64 fileID ) const
    {
        if ( fileID == 0 || _objectManager == nullptr )
            return 0;
        for ( const auto& [objectID, knownFileID] : _mapObjectIDToFileID )
        {
            if ( knownFileID == fileID && _objectManager->resolveGameObject( GameObjectHandle::make( objectID ) ) != nullptr )
                return objectID;
        }
        return 0;
    }

    /**
     * @brief 씬 리소스를 정리하고 머티리얼 참조를 해제합니다.
     */
    void Scene::shutdown()
    {
        releaseDefaultMaterial();
        // 셀을 버린다 — 내린 씬에 다음 갱신이 셀을 다시 짓지 않게.
        if ( _objectManager != nullptr )
            _objectManager->getSubsystem<SceneStreamingSubsystem>().endWorld();
        _mapPrefabSource.clear();
        _mapObjectIDToFileID.clear();
        _listUnresolvedEntity.clear();
        if ( _objectManager != nullptr )
        {
            for ( GameObject* pObj : _objectManager->getAllGameObjects() )
            {
                if ( pObj != nullptr )
                    _objectManager->destroyObject( pObj );
            }
        }
    }

    /**
     * @brief 씬 안의 게임 오브젝트와 컴포넌트를 매 프레임 갱신합니다.
     */
    void Scene::update( float32 deltaTime )
    {
        if ( _objectManager == nullptr )
            return;
        // 스트리밍 원점은 지난 프레임 끝의 카메라 자리다 — 셀은 프레임 끝(`DestroyPost`)에 오르내린다. 바깥 원점(서버의 접속자)을 쓰면 넣지 않는다.
        SceneStreamingSubsystem& streaming = _objectManager->getSubsystem<SceneStreamingSubsystem>();
        if ( streaming.isStreaming() && streaming.usesExternalSources() == false )
        {
            const CameraComponent* pCamera = getActiveGameCamera();
            if ( pCamera != nullptr )
                streaming.setCameraSource( pCamera->getWorldPosition() );
        }
        // 시야를 따라가는 서비스(오디오 리스너)는 씬을 모른다 — 이번 프레임의 게임 카메라를 매니저에 둔다.
        _objectManager->setFrameCamera( getActiveGameCamera() );
        _objectManager->update( deltaTime );
    }

    /**
     * @brief 씬에 게임 카메라가 있는지 보고, 없으면 기본 위치에 만듭니다.
     */
    bool Scene::pickActiveGameCamera()
    {
        if ( _objectManager == nullptr )
            return false;

        // 시퀀서 카메라 컷이 먼저다(언리얼의 Camera Cut 트랙이 뷰 타깃을 덮는 것과 같다). 다음은 직접 고른 카메라, 그다음은 레지스트리의 규칙이다. 모두 없으면
        // 지난번 카메라를 그대로 둔다. 하나뿐인 게임 카메라를 끈 경우다. 그때 새로 만들거나 기본값으로 되돌리면 꺼 둔 카메라를 프레임마다
        // 옮기게 된다. 아무것도 없을 때만 만든다.
        CameraComponent* pCamera = _objectManager->getCameraRegistry().getCutCamera();
        if ( CameraRegistry::isUsableCamera( pCamera ) == false )
            pCamera = resolveCamera( _gameCameraOverride );
        if ( CameraRegistry::isUsableCamera( pCamera ) == false )
            pCamera = _objectManager->getCameraRegistry().selectCamera( CameraRole::Game );
        if ( pCamera == nullptr )
            pCamera = resolveCamera( _activeGameCamera );
        if ( pCamera == nullptr || pCamera->isPendingDestroy() )
        {
            // 기본 GameCamera 다. 이미 있으면 **그대로** 쓴다. `findOrCreateNamed` 는 있는 것의 위치와 렌즈를 기본값으로 되돌린다.
            GameObject*      pNamed       = _objectManager->findGameObjectByName( hashed_string( "GameCamera" ) );
            CameraComponent* pNamedCamera = ( pNamed != nullptr ) ? pNamed->getComponent<CameraComponent>() : nullptr;
            if ( pNamedCamera != nullptr && pNamedCamera->isPendingDestroy() == false )
                pCamera = pNamedCamera;
            else
            {
                _objectManager->flushSceneTransforms();
                pCamera = CameraComponent::findOrCreateNamed( _objectManager.get(), hashed_string( "GameCamera" ), CameraRole::Game,
                                                              float3( 0.0f, 1.2f, 3.2f ), float3( 0.0f, 0.0f, 0.0f ) );
                _objectManager->flushSceneTransforms();
            }
        }

        storeCameraHandle( pCamera, _activeGameCamera );
        return pCamera != nullptr;
    }

    /**
     * @brief 지금 켜져 있는 방향광 하나를 반환합니다. 없으면 nullptr 입니다.
     * @details 레지스트리만 본다. 비용이 빛의 수에 비례하고 씬 크기와 무관하다(Engine 이 매 프레임 부르므로 씬을 훑으면 안 된다.
     *          `ComponentRegistry` 설명 참고).
     */
    DirectionalLightComponent* Scene::findActiveDirectionalLight() const
    {
        return SceneInternal::findDirectionalLight( _objectManager.get(), false );
    }

    DirectionalLightComponent* Scene::findShadowCastingDirectionalLight() const
    {
        return SceneInternal::findDirectionalLight( _objectManager.get(), true );
    }

    void Scene::setActiveGameCamera( CameraComponent* pCamera )
    {
        storeCameraHandle( pCamera, _gameCameraOverride );
        storeCameraHandle( pCamera, _activeGameCamera );
    }

    CameraComponent* Scene::getActiveGameCamera() const
    {
        return resolveCamera( _activeGameCamera );
    }

    void Scene::releaseDefaultMaterial()
    {
        // 경로가 있으면 `initialize` 가 이 에셋 매니저로 잡은 것이다.
        if ( _defaultMaterialPath.empty() == false && _services._pAssetManager != nullptr )
            _services._pAssetManager->getAssetCache<MaterialCache>().release( _defaultMaterialPath );
        _pMaterial = nullptr;
        _defaultMaterialPath.clear();
    }

    CameraComponent* Scene::resolveCamera( TComponentHandle<CameraComponent> handle ) const
    {
        if ( _objectManager == nullptr )
            return nullptr;
        return _objectManager->resolveComponent( handle );
    }

    void Scene::storeCameraHandle( CameraComponent* pCamera, TComponentHandle<CameraComponent>& outHandle )
    {
        outHandle = TComponentHandle<CameraComponent>{ pCamera };
    }

    void Scene::setEntityPrefabPath( uint64 objectID, string_view prefabPath )
    {
        if ( objectID == 0 )
            return;
        if ( prefabPath.empty() )
            _mapPrefabSource.erase( objectID );
        else
            _mapPrefabSource[objectID] = string{ prefabPath };
    }

    string_view Scene::getEntityPrefabPath( uint64 objectID ) const
    {
        const auto it = _mapPrefabSource.find( objectID );
        if ( it != _mapPrefabSource.end() )
            return it->second;
        return {};
    }

    SW_DEV_COMMAND( SceneLeakCurve, "scene.leakcurve", "scene.leakcurve <scene> [measuredRounds]",
                    "Load a scene again and again (1 warmup + N measured, default 8) and judge whether leftover memory grows (leak detection L3)",
                    &SceneDevCommandsInternal::runSceneLeakCurve );
    SW_DEV_COMMAND( Teleport, "teleport", "teleport <object> <x> <y> <z>", "Teleport a named object of the active scene", &SceneDevCommandsInternal::runTeleport );
    SW_DEV_COMMAND( StreamingCells, "world.cells", "world.cells", "Show loaded streaming cells, or split the active scene into cells as the cooker would",
                    &SceneDevCommandsInternal::runStreamingCells );
    SW_DEV_COMMAND( TagAdd, "tag.add", "tag.add <object> <tag>", "Add a gameplay tag to a named object of the active scene", &SceneDevCommandsInternal::runTagAdd );
} // namespace sw
