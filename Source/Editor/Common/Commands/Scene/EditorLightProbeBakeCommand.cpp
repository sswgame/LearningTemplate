/**
 * @file EditorLightProbeBakeCommand.cpp
 * @brief 에디터 커맨드 "Bake Light Probes"(`lighting.bakeProbes`) — Build 메뉴와 커맨드 팔레트에서 활성 씬의 라이트 프로브 볼륨을 굽습니다(IndirectLighting.md P6).
 * @details 선택한 오브젝트 중 볼륨이 있으면 그것만, 없으면 씬의 모든 볼륨을 굽는다(언리얼 Build Lighting 의 선택 굽기와 같은 나눔).
 *          결과는 씬 파일 옆 `<씬 이름>.<오브젝트 이름(소문자)>.probes` 에 쓰고 볼륨의 `_probeDataPath` 에 적는다 — 씬을 저장해야 경로가 남으므로 씬을 고친 것으로 표시한다.
 *          굽기는 동기다. 진행은 볼륨마다 로그 한 줄과 끝난 뒤 알림 하나로 보인다(볼륨 하나가 수 초를 넘으면 비동기 작업으로 옮긴다 — 계획 문서 P6 의 남은 것).
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/Scene/EditorBakeOutputPath.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Graphics/Lighting/LightProbeGrid.h"
#include "Engine/Object/Component/3D/LightProbeVolumeComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Renderer/Light/LightProbeBaker.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorLightProbeBake" );

    namespace
    {
        struct EditorLightProbeBakeCommandInternal
        {
            /** @brief 볼륨 하나를 굽고 파일로 쓴 뒤 볼륨에 넘깁니다. 실패하면 이유를 @p outError 에 적습니다. */
            static bool bakeVolume( const Scene& scene, LightProbeVolumeComponent& volume, string& outError )
            {
                LightProbeBakeInput input{};
                if ( LightProbeBaker::collectInput( scene, volume, input, &outError ) == false )
                    return false;
                LightProbeGrid grid{};
                if ( LightProbeBaker::bakeGrid( volume.computeLayout(), input, grid, &outError ) == false )
                    return false;
                const string resourcePath = volume.getOwner() != nullptr ? makeSceneBakeResourcePath( scene, *volume.getOwner(), LightProbeGrid::kExtension ) : string{};
                if ( resourcePath.empty() )
                {
                    outError = "the scene has never been saved - save it before baking light probes";
                    return false;
                }
                if ( grid.saveToFile( ResourcePaths::getWritePath( resourcePath ) ) == false )
                {
                    utf8 arrMessage[constant::kMaxBuffer512];
                    formatstring( arrMessage, constant::kMaxBuffer512, "could not write %#", resourcePath.c_str() );
                    outError = arrMessage;
                    return false;
                }
                if ( volume.adoptProbeData( std::move( grid ), resourcePath ) == false )
                {
                    outError = "the baked grid does not fit the volume";
                    return false;
                }
                SW_LOG_INFO( "Baked light probes '%#' (%# probes)", resourcePath.c_str(), volume.computeLayout().getProbeCount() );
                return true;
            }

            /** @brief 굽을 볼륨 — 선택한 오브젝트의 볼륨, 선택에 볼륨이 없으면 씬의 모든 볼륨입니다. */
            static void collectVolumes( GameObjectManager& manager, vector<LightProbeVolumeComponent*>& outListVolume )
            {
                outListVolume.clear();
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr )
                {
                    vector<GameObject*> listSelected;
                    editor::getMainSceneSelection().getSelectedObjects( listSelected );
                    for ( GameObject* pObject : listSelected )
                    {
                        LightProbeVolumeComponent* pVolume = ( pObject != nullptr ) ? pObject->getComponent<LightProbeVolumeComponent>() : nullptr;
                        if ( pVolume != nullptr )
                            outListVolume.push_back( pVolume );
                    }
                }
                if ( outListVolume.empty() == false )
                    return;
                for ( LightProbeVolumeComponent* pVolume : manager.getComponentRegistry().getAll<LightProbeVolumeComponent>() )
                {
                    if ( pVolume != nullptr )
                        outListVolume.push_back( pVolume );
                }
            }

            static void runBake()
            {
                Scene*             pScene   = editor::getActiveScene();
                GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( pScene == nullptr || pManager == nullptr )
                    return;
                vector<LightProbeVolumeComponent*> listVolume;
                collectVolumes( *pManager, listVolume );
                uint32 bakedCount{ 0 };
                string lastError;
                for ( LightProbeVolumeComponent* pVolume : listVolume )
                {
                    string error;
                    if ( bakeVolume( *pScene, *pVolume, error ) )
                    {
                        ++bakedCount;
                        continue;
                    }
                    SW_LOG_ERROR( "Light probe bake failed: %#", error.c_str() );
                    lastError = error;
                }
                ++getBakeRunCount();
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return;
                if ( bakedCount > 0 )
                    pContext->getSubsystem<EditorWorkspace>().markSceneDirty();
                EditorNotificationManager& notification = pContext->getSubsystem<EditorNotificationManager>();
                const uint32               volumeCount  = static_cast<uint32>( listVolume.size() );
                utf8                       arrMessage[constant::kMaxBuffer512];
                if ( volumeCount == 0 )
                {
                    notification.push( "Bake Light Probes", "Nothing to bake - add a Light Probe Volume", NotificationType::Warning );
                }
                else if ( bakedCount < volumeCount )
                {
                    formatstring( arrMessage, constant::kMaxBuffer512, "Baked %# of %# volume(s) - %#", bakedCount, volumeCount, lastError.c_str() );
                    notification.push( "Bake Light Probes", arrMessage, NotificationType::Error );
                }
                else
                {
                    formatstring( arrMessage, constant::kMaxBuffer512, "Baked %# volume(s) - save the scene to keep the paths", bakedCount );
                    notification.push( "Bake Light Probes", arrMessage, NotificationType::Success );
                }
            }

            /** @brief 편집 상태에서만 켠다 — 플레이 중에 구우면 플레이가 움직인 오브젝트의 빛을 굽는다. */
            static bool isEnabled() { return EditorPlaySession::areSceneEditsAllowed() && editor::getActiveObjectManager() != nullptr; }

            [[nodiscard]] static bool readBakeRunCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getBakeRunCount() );
                return true;
            }

            [[nodiscard]] static bool readBakedVolumeCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                uint32             count{ 0 };
                if ( pManager != nullptr )
                {
                    for ( const LightProbeVolumeComponent* pVolume : pManager->getComponentRegistry().getAll<LightProbeVolumeComponent>() )
                    {
                        if ( pVolume != nullptr && pVolume->findProbeData() != nullptr )
                            ++count;
                    }
                }
                outValue = static_cast<float64>( count );
                return true;
            }
        };
    } // namespace

    SW_EDITOR_COMMAND( LightProbeBake, "lighting.bakeProbes", 3310, "Bake Light Probes", editoricon::kLightGlobal, "Lighting",
                       "선택한(없으면 모든) 라이트 프로브 볼륨의 간접광을 굽습니다(씬 옆 .probes)", "Bake the selected (or every) light probe volume of the active scene", {},
                       &EditorLightProbeBakeCommandInternal::runBake, &EditorLightProbeBakeCommandInternal::isEnabled, "MainMenu/Build" );

    SW_AUTOMATION_PROBE( editorLightProbeBakeCount, "Editor.LightProbeBakeCount", "Times the lighting.bakeProbes command ran in this editor session",
                         &EditorLightProbeBakeCommandInternal::readBakeRunCount );
    SW_AUTOMATION_PROBE( editorBakedLightProbeVolumeCount, "Editor.BakedLightProbeVolumeCount", "Light probe volumes of the active scene with usable baked data",
                         &EditorLightProbeBakeCommandInternal::readBakedVolumeCount );
} // namespace sw::editor
