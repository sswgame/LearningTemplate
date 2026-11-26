/**
 * @file VisualWatches.cpp
 * @brief 보이는 계통(메시 LOD · 빛 · VFX 풀)의 자동화 감시 값과 개발 명령입니다.
 * @details 기능 .cpp 끝에 있던 등록 블록을 그대로 옮겼다 — 배포 엔진에 컴파일되지 않게 개발 도구가 짓는다. 본문은 기능 형식의 공개 조회만 읽는다.
 */
#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/OrientationUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Graphics/Lighting/LensFlareAsset.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Shader/Binding/GPULight.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Object/GameObject/Subsystem/SceneSubsystemRegistry.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Visual/Component/LightComponent.h"
#include "Engine/Visual/Component/MeshComponent.h"
#include "Engine/Visual/Component/MeshSectionComponent.h"
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
