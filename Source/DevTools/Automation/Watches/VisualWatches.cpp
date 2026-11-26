/**
 * @file VisualWatches.cpp
 * @brief 보이는 계통(메시 LOD · 빛 · VFX 풀)의 자동화 감시 값과 개발 명령입니다.
 * @details 기능 .cpp 끝에 있던 등록 블록을 그대로 옮겼다 — 배포 엔진에 컴파일되지 않게 개발 도구가 짓는다. 본문은 기능 형식의 공개 조회만 읽는다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/BuildInfo.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/OrientationUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Environment/EnvironmentUtil.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/Lighting/LensFlareAsset.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Graphics/Shader/Binding/GPULight.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Texture/TextureCache.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Object/GameObject/Subsystem/ISceneSubsystem.h"
#include "Engine/Object/GameObject/Subsystem/SceneSubsystemRegistry.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Scene/ScenePhysics.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"
#include "Engine/VFX/VFXComponent.h"
#include "Engine/VFX/VFXEffectDef.h"
#include "Engine/VFX/VFXEffectPool.h"
#include "Engine/Visual/Component/LightComponent.h"
#include "Engine/Visual/Component/MeshComponent.h"
#include "Engine/Visual/Component/MeshSectionComponent.h"
#include "Engine/Visual/MeshInstanceBatch.h"
#include "Engine/Visual/MeshLODSettings.h"
#include "Engine/Visual/MeshLODSystem.h"

// Engine/Visual/MeshLODSystem.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    namespace
    {
        /** @brief LOD 판정 확인 — 보기 모드 LODLevel 의 색을 숫자(지금 단계 · 화면 크기 · 임계값 · 고른 쪽)로 읽는다. */
        struct MeshLODDiagnosticsInternal
        {
            /** @brief @p object 의 메시 컴포넌트마다 판정 한 줄을 @p outText 에 잇습니다. 메시 컴포넌트가 없으면 false 입니다. */
            static bool describeObject( const GameObject& object, string& outText )
            {
                const GameObjectManager* const pManager = object.getManager();
                if ( pManager == nullptr )
                    return false;
                const MeshLODSystem& system = pManager->getSubsystem<MeshLODSystem>();
                bool                 bFound = false;
                for ( const Component* pComponent : object.getComponents() )
                {
                    const MeshComponent* pMesh = castTo<MeshComponent>( pComponent );
                    if ( pMesh == nullptr )
                        continue;
                    if ( bFound )
                        outText += "; ";
                    outText += system.explainLOD( *pMesh );
                    bFound = true;
                }
                return bFound;
            }

            /** @brief `lod.explain <오브젝트>` — 활성 씬에서 그 이름의 오브젝트를 찾아 메시마다 판정을 답한다. */
            static bool runExplain( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                GameObjectManager* const                  pManager = engine::findActiveObjectManager();
                const GameObject*                         pObject  = pManager != nullptr ? pManager->findGameObjectByName( hashed_string( listArgument[0] ) ) : nullptr;
                StringBuilder<constant::buffer::kMax1024> reply;
                if ( pObject == nullptr )
                {
                    reply.appendFormat( "lod.explain: no object '%#' in the active scene", listArgument[0] );
                    outReply = string( reply.view() );
                    return false;
                }
                string text;
                if ( describeObject( *pObject, text ) == false )
                {
                    reply.appendFormat( "lod.explain: '%#' has no mesh component", listArgument[0] );
                    outReply = string( reply.view() );
                    return false;
                }
                reply.appendFormat( "%#: %#", listArgument[0], text );
                outReply = string( reply.view() );
                SW_LOG_INFO_ID( "MeshLOD.Explain", "%#", outReply.c_str() );
                return true;
            }

            /** @brief 감시 값 `Mesh.LOD` — 대상 오브젝트의 메시마다 판정 한 줄(`MeshLODSystem::explainLOD`)입니다. */
            [[nodiscard]] static bool readLOD( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject* pTarget = query.findTargetObject( outValue );
                if ( pTarget == nullptr )
                    return false;
                string text;
                if ( describeObject( *pTarget, text ) == false )
                {
                    outValue.setText( query.describeMissing( "mesh component" ) );
                    return false;
                }
                outValue.setText( text );
                return true;
            }

            /** @brief 감시 값 `Mesh.LODIndex` — 대상 오브젝트 첫 메시 컴포넌트의 지금 단계입니다. */
            [[nodiscard]] static bool readLODIndex( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const MeshComponent* pMesh = query.findTargetComponent<MeshComponent>( "mesh component", outValue );
                if ( pMesh == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pMesh->getLODIndex() ) );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( MeshLODExplain, "lod.explain", "lod.explain <object>",
                    "Explain the LOD pick of an object's meshes: current level, screen size, thresholds and who picks (cpu, gpu, forced, section)", &MeshLODDiagnosticsInternal::runExplain );
    SW_AUTOMATION_QUERY_WATCH( meshLOD, "Mesh.LOD", "LOD pick of the target's meshes: level, screen size, thresholds and who picks", &MeshLODDiagnosticsInternal::readLOD, true, false );
    SW_AUTOMATION_QUERY_WATCH( meshLODIndex, "Mesh.LODIndex", "Current LOD level of the target's first mesh component", &MeshLODDiagnosticsInternal::readLODIndex, true, false );
} // namespace sw

// Engine/Visual/Component/LightComponent.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    // 게임 코드의 빛 종류(`LightKind`)와 셰이더의 종류 값(`shaderslot::kLightType*`)은 한 값이다 — 레지스트리 칸 번호를 둘이 함께 쓴다.
    static_assert( static_cast<uint32>( LightKind::Directional ) == shaderslot::kLightTypeDirectional && static_cast<uint32>( LightKind::Point ) == shaderslot::kLightTypePoint &&
                       static_cast<uint32>( LightKind::Spot ) == shaderslot::kLightTypeSpot && static_cast<uint32>( LightKind::Point2D ) == shaderslot::kLightTypePoint2D &&
                       static_cast<uint32>( LightKind::Global2D ) == shaderslot::kLightTypeGlobal2D && static_cast<uint32>( LightKind::Freeform2D ) == shaderslot::kLightTypeFreeform2D,
                   "LightKind 는 셰이더의 빛 종류 값과 같아야 한다" );

    namespace
    {
        struct LightWatchesInternal
        {
            /** @brief 감시 값 `Light.Intensity` 입니다. 대상 빛의 지금 세기(적은 세기 × 날씨 배율)입니다. */
            [[nodiscard]] static bool readIntensity( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const LightComponent* pLight = query.findTargetComponent<LightComponent>( "Light", outValue );
                if ( pLight == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pLight->getIntensity() ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( lightIntensity, "Light.Intensity", "Current intensity of the target's light (authored intensity x weather scale)", &LightWatchesInternal::readIntensity, true, false );
} // namespace sw

// Engine/VFX/VFXEffectPool.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    namespace
    {
        struct VFXEffectPoolWatchesInternal
        {
            /** @brief 감시 값 `VFX.PoolPlayed` 입니다. 효과 `key`(경로)를 이 씬에서 튼 수(누적)입니다. */
            [[nodiscard]] static bool readPlayed( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                if ( query._pManager == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( query._pManager->getSubsystem<VFXEffectPool>().getPlayCount( hashed_string( query._key ) ) ) );
                return true;
            }

            /** @brief 감시 값 `VFX.PoolSize` 입니다. 효과 `key`(경로)의 풀이 만든 오브젝트 수입니다. */
            [[nodiscard]] static bool readPoolSize( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                if ( query._pManager == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( query._pManager->getSubsystem<VFXEffectPool>().getPoolSize( hashed_string( query._key ) ) ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( vfxPoolPlayed, "VFX.PoolPlayed", "Times the pooled one-shot effect at path key played in this scene (cumulative)", &VFXEffectPoolWatchesInternal::readPlayed, false,
                               true );
    SW_AUTOMATION_QUERY_WATCH( vfxPoolSize, "VFX.PoolSize", "Objects the pool of the one-shot effect at path key has made", &VFXEffectPoolWatchesInternal::readPoolSize, false, true );
} // namespace sw

// Engine/VFX/VFXComponent.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    namespace
    {
        struct VFXComponentWatchesInternal
        {
            /** @brief 감시 값 `VFX.ParticlesInBox` 입니다. 대상 효과의 CPU 입자 가운데 월드 상자 key("최소 x y z 최대 x y z") 안에 있는 수입니다. */
            [[nodiscard]] static bool readParticlesInBox( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const VFXComponent* pEffect = query.findTargetComponent<VFXComponent>( "VFXComponent", outValue );
                if ( pEffect == nullptr )
                    return false;
                float32                   arrBound[6]{};
                const vector<string_view> listToken = StringUtil::split( query._key, " " );
                bool                      bParsed   = listToken.size() == SW_COUNT_OF( arrBound );
                for ( size_t index = 0; bParsed && index < listToken.size(); ++index )
                    bParsed = StringUtil::parseFloat( listToken[index], arrBound[index] );
                if ( bParsed == false )
                {
                    outValue.setText( "key must be 'minX minY minZ maxX maxY maxZ'" );
                    return false;
                }
                uint32 count = 0;
                for ( uint32 emitterIndex = 0; emitterIndex < pEffect->getEmitterCount(); ++emitterIndex )
                {
                    const float4x4                               world         = pEffect->getEmitterWorldMatrix( emitterIndex );
                    const shared_ptr<const vector<GPUParticle>>& pListParticle = pEffect->getEmitterParticles( emitterIndex );
                    if ( pListParticle == nullptr )
                        continue;
                    for ( const GPUParticle& particle : *pListParticle )
                    {
                        const float3 position = float3::transform( float3{ particle._positionSize._x, particle._positionSize._y, particle._positionSize._z }, world );
                        const bool   bAbove   = position._x >= arrBound[0] && position._y >= arrBound[1] && position._z >= arrBound[2];
                        const bool   bBelow   = position._x <= arrBound[3] && position._y <= arrBound[4] && position._z <= arrBound[5];
                        count += bAbove && bBelow ? 1u : 0u;
                    }
                }
                outValue.setNumber( static_cast<float64>( count ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( vfxParticlesInBox, "VFX.ParticlesInBox", "CPU particles of the target's effect inside the world box key 'minX minY minZ maxX maxY maxZ'",
                               &VFXComponentWatchesInternal::readParticlesInBox, true, true );
} // namespace sw

// Engine/Visual/TextureStreamingSystem.cpp 에서 옮긴 감시 읽기(아래 등록이 부른다).
namespace sw
{
    namespace
    {
        /** @brief 감시 값 `Texture.StreamingQueueCount` 입니다. 위쪽 밉을 기다리거나 읽는 중이거나 업로드 중인 텍스처 요청 수(텍스처 로드 큐 — `TextureCache::captureStreamingQueue`)입니다. */
        [[nodiscard]] bool readTextureStreamingQueueCount( const GameObjectManager* pManager, float64& outValue )
        {
            AssetManager* pAssets = pManager != nullptr ? pManager->findAssetManager() : nullptr;
            if ( pAssets == nullptr )
                return false;
            const TextureStreamingQueueSnapshot snapshot = pAssets->getAssetCache<TextureCache>().captureStreamingQueue();
            outValue                                     = static_cast<float64>( snapshot._listWaiting.size() + snapshot._readingCount + snapshot._uploadingCount );
            return true;
        }
    } // namespace
} // namespace sw

// Engine/Visual/TextureStreamingSystem.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    SW_AUTOMATION_WATCH( textureStreamingQueueCount, "Texture.StreamingQueueCount",
                         "Texture mip requests waiting, reading or uploading (the texture load queue, ordered by streaming priority then screen size; world.streamqueue lists them)",
                         &readTextureStreamingQueueCount );
} // namespace sw
