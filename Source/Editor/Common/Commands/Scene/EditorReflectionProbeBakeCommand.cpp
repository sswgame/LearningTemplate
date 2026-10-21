/**
 * @file EditorReflectionProbeBakeCommand.cpp
 * @brief 에디터 커맨드 "Bake Reflection Probes"(`lighting.bakeReflectionProbes`) — Build 메뉴와 커맨드 팔레트에서 활성 씬의 반사 프로브를 GPU 로 굽습니다(IndirectLighting.md P4).
 * @details 선택한 오브젝트 중 반사 프로브가 있으면 그것만, 없으면 씬의 모든 반사 프로브를 굽는다(`lighting.bakeProbes` 와 같은 나눔). 결과는 씬 파일 옆
 *          `<씬 이름>.<오브젝트 이름(소문자)>.reflprobe` 에 쓰고 프로브의 `_probeDataPath` 에 적는다 — 씬을 저장해야 경로가 남으므로 씬을 고친 것으로 표시한다.
 *          굽기는 동기다 — 렌더 스레드를 비우고 이 스레드가 그래픽스 컨텍스트를 잡아 면 여섯을 그린다(`ReflectionProbeBaker::bakeProbes`). 끝난 뒤 알림 하나가 뜬다.
 */
#include "pch.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/Scene/EditorBakeOutputPath.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorRenderViews.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Automation/AutomationProbe.h"
#include "Engine/Graphics/Lighting/ReflectionProbeData.h"
#include "Engine/Object/Component/3D/ReflectionProbeComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Renderer/Capture/ReflectionProbeBaker.h"
#include "Engine/Renderer/RenderThreadSubsystem.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scene/Scene.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorReflectionProbeBake" );

    namespace
    {
        struct EditorReflectionProbeBakeCommandInternal
        {
            /** @brief 이 에디터 세션에서 `lighting.bakeReflectionProbes` 가 끝난 횟수입니다(탐침 `Editor.ReflectionProbeBakeCount`). */
            static uint32& getBakeRunCount()
            {
                static uint32 s_count{ 0 };
                return s_count;
            }

            /** @brief 굽을 프로브 — 선택한 오브젝트의 프로브, 선택에 프로브가 없으면 씬의 모든 프로브입니다. */
            static void collectProbes( GameObjectManager& manager, vector<ReflectionProbeComponent*>& outListProbe )
            {
                outListProbe.clear();
                if ( EditorContext::get() != nullptr )
                {
                    vector<GameObject*> listSelected;
                    editor::getMainSceneSelection().getSelectedObjects( listSelected );
                    for ( GameObject* pObject : listSelected )
                    {
                        ReflectionProbeComponent* pProbe = ( pObject != nullptr ) ? pObject->getComponent<ReflectionProbeComponent>() : nullptr;
                        if ( pProbe != nullptr )
                            outListProbe.push_back( pProbe );
                    }
                }
                if ( outListProbe.empty() == false )
                    return;
                for ( ReflectionProbeComponent* pProbe : manager.getComponentRegistry().getAll<ReflectionProbeComponent>() )
                {
                    if ( pProbe != nullptr )
                        outListProbe.push_back( pProbe );
                }
            }

            /** @brief 구운 데이터 하나를 파일로 쓰고 프로브에 넘깁니다. 실패하면 이유를 @p outError 에 적습니다. */
            static bool storeProbe( const Scene& scene, ReflectionProbeComponent& probe, ReflectionProbeData&& data, string& outError )
            {
                const string resourcePath = probe.getOwner() != nullptr ? makeSceneBakeResourcePath( scene, *probe.getOwner(), ReflectionProbeData::kExtension ) : string{};
                if ( resourcePath.empty() )
                {
                    outError = "the scene has never been saved - save it before baking reflection probes";
                    return false;
                }
                if ( data.saveToFile( ResourcePaths::getWritePath( resourcePath ) ) == false )
                {
                    utf8 arrMessage[constant::kMaxBuffer512];
                    formatstring( arrMessage, constant::kMaxBuffer512, "could not write %#", resourcePath.c_str() );
                    outError = arrMessage;
                    return false;
                }
                const uint32 resolution = data._resolution;
                if ( probe.adoptProbeData( std::move( data ), resourcePath ) == false )
                {
                    outError = "the baked reflection does not fit the probe";
                    return false;
                }
                SW_LOG_INFO( "Baked reflection probe '%#' (%#x%# octahedral)", resourcePath.c_str(), resolution, resolution );
                return true;
            }

            static void runBake()
            {
                Scene*             pScene   = editor::getActiveScene();
                GameObjectManager* pManager = editor::getActiveObjectManager();
                EditorContext*     pContext = EditorContext::get();
                const bool         bNoScene = pScene == nullptr || pManager == nullptr || pContext == nullptr;
                if ( bNoScene )
                    return;
                vector<ReflectionProbeComponent*> listProbe;
                collectProbes( *pManager, listProbe );
                EditorNotificationManager& notification = pContext->getSubsystem<EditorNotificationManager>();
                if ( listProbe.empty() )
                {
                    ++getBakeRunCount();
                    notification.push( "Bake Reflection Probes", "Nothing to bake - add a Reflection Probe", NotificationType::Warning );
                    return;
                }

                IRHIDevice*                 pDevice                = pContext->getSubsystem<EditorRenderViews>().getRHIDevice();
                RenderThreadSubsystem*      pRenderThreadSubsystem = editor::findSubsystem<RenderThreadSubsystem>();
                string                      lastError;
                vector<ReflectionProbeData> listData;
                const bool                  bBaked = pDevice != nullptr &&
                                    ReflectionProbeBaker::bakeProbes( *pDevice, pRenderThreadSubsystem != nullptr ? pRenderThreadSubsystem->findRenderThread() : nullptr, *pScene,
                                                                      listProbe, listData, &lastError );
                if ( pDevice == nullptr )
                    lastError = "no graphics device";
                uint32 storedCount{ 0 };
                for ( size_t probeIndex = 0; probeIndex < listProbe.size() && probeIndex < listData.size(); ++probeIndex )
                {
                    if ( listData[probeIndex]._listTexel.empty() )
                        continue;
                    string error;
                    if ( storeProbe( *pScene, *listProbe[probeIndex], std::move( listData[probeIndex] ), error ) )
                    {
                        ++storedCount;
                        continue;
                    }
                    SW_LOG_ERROR( "Reflection probe bake failed: %#", error.c_str() );
                    lastError = error;
                }
                if ( bBaked == false )
                    SW_LOG_ERROR( "Reflection probe bake failed: %#", lastError.c_str() );
                ++getBakeRunCount();
                if ( storedCount > 0 )
                    pContext->getSubsystem<EditorWorkspace>().markSceneDirty();
                const uint32 probeCount = static_cast<uint32>( listProbe.size() );
                utf8         arrMessage[constant::kMaxBuffer512];
                if ( storedCount < probeCount )
                {
                    formatstring( arrMessage, constant::kMaxBuffer512, "Baked %# of %# probe(s) - %#", storedCount, probeCount, lastError.c_str() );
                    notification.push( "Bake Reflection Probes", arrMessage, NotificationType::Error );
                }
                else
                {
                    formatstring( arrMessage, constant::kMaxBuffer512, "Baked %# probe(s) - save the scene to keep the paths", storedCount );
                    notification.push( "Bake Reflection Probes", arrMessage, NotificationType::Success );
                }
            }

            /** @brief 편집 상태에서만 켠다 — 플레이 중에 구우면 플레이가 움직인 오브젝트를 찍는다. */
            static bool isEnabled() { return editor::getActiveObjectManager() != nullptr; } // 편집 허용 답(플레이 중 · 베이크 중)은 레지스트리가 함께 묻는다(EditorCommandEdit::Scene)

            [[nodiscard]] static bool readBakeRunCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                outValue = static_cast<float64>( getBakeRunCount() );
                return true;
            }

            [[nodiscard]] static bool readBakedProbeCount( const GameObjectManager* /*pManager*/, float64& outValue )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                uint32             count{ 0 };
                if ( pManager != nullptr )
                {
                    for ( const ReflectionProbeComponent* pProbe : pManager->getComponentRegistry().getAll<ReflectionProbeComponent>() )
                    {
                        if ( pProbe != nullptr && pProbe->findProbeData() != nullptr )
                            ++count;
                    }
                }
                outValue = static_cast<float64>( count );
                return true;
            }
        };
    } // namespace

    SW_EDITOR_COMMAND( ReflectionProbeBake, "lighting.bakeReflectionProbes", 3320, "Bake Reflection Probes", editoricon::kLightGlobal, "Lighting",
                       "선택한(없으면 모든) 반사 프로브를 GPU 로 찍어 굽습니다(씬 옆 .reflprobe)", "Capture the selected (or every) reflection probe of the active scene on the GPU", {},
                       &EditorReflectionProbeBakeCommandInternal::runBake, &EditorReflectionProbeBakeCommandInternal::isEnabled, Scene, "MainMenu/Build" );

    SW_AUTOMATION_PROBE( editorReflectionProbeBakeCount, "Editor.ReflectionProbeBakeCount", "Times the lighting.bakeReflectionProbes command ran in this editor session",
                         &EditorReflectionProbeBakeCommandInternal::readBakeRunCount );
    SW_AUTOMATION_PROBE( editorBakedReflectionProbeCount, "Editor.BakedReflectionProbeCount", "Reflection probes of the active scene with usable baked data",
                         &EditorReflectionProbeBakeCommandInternal::readBakedProbeCount );
} // namespace sw::editor
