/**
 * @file SceneObjectReferencePropertyDrawer.cpp
 * @brief `SceneObjectReference` 프로퍼티의 그리기 확장입니다 — 올라와 있는 씬들의 오브젝트에서 하나를 고르는 콤보(다른 씬 · 프리팹 파일의 오브젝트 참조).
 * @details 값은 (씬 경로, 파일 id) 라서 고르는 순간 대상 오브젝트에 파일 id 를 준다(`SceneObjectReferenceUtil::makeReference` — 다음 저장이 그 id 를 쓴다).
 *          저장한 적 없는 씬(경로 없음)의 오브젝트는 참조를 만들 수 없어 비활성 항목으로 보인다. 지금 값이 올라와 있지 않은 씬을 가리키면 정상 상태(하위 레벨 · 셀이 내려감)로, 씬은 있는데
 *          오브젝트가 없으면 결함으로 미리보기에 색을 달리해 알린다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/Inspector/EditorPropertyGrid.h"
#include "Editor/Panels/Inspector/IInspectorProperty.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Scene/SceneObjectReference.h"
#include "Engine/Serialization/Base/SerializerUtil.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 콤보 한 줄입니다. 콜백은 오브젝트 맵의 공유 잠금 안이라 이름과 번호만 모은다. */
        struct SceneObjectChoice
        {
            string _label;
            uint64 _objectID{ 0 };
        };

        class SceneObjectReferencePropertyDrawer final : public IInspectorProperty
        {
        public:
            bool draw( const MutableObjectAddress instance, const PropertyInfo& prop, EditorPropertyGrid& grid ) override
            {
                SceneObjectReference* pValue = prop.getValuePtr<SceneObjectReference>( instance.get() );
                if ( pValue == nullptr )
                    return false;
                const SceneManager* pSceneManager = editor::findSubsystem<SceneManager>();
                if ( pSceneManager == nullptr )
                {
                    ImGui::TextDisabled( "(no scene manager)" );
                    return true;
                }

                uint64                                resolvedID{ 0 };
                const SceneObjectResolveResult        state = SceneObjectReferenceUtil::resolve( *pSceneManager, *pValue, resolvedID );
                fixed_string<constant::kMaxBuffer512> preview;
                makePreview( *pSceneManager, *pValue, state, resolvedID, preview );
                const bool bWarn = state == SceneObjectResolveResult::ObjectMissing;
                if ( bWarn )
                    EditorThemeUtil::pushTextColor( EditorThemeUtil::getWarningColor() );
                const bool bOpen = ImGui::BeginCombo( "##sceneObjectReference", preview.c_str() );
                if ( bWarn )
                    EditorThemeUtil::popTextColor();
                if ( bOpen == false )
                    return true;

                SceneObjectReference picked{};
                bool                 bPicked = false;
                if ( ImGui::Selectable( "(none)", pValue->isSet() == false ) )
                    bPicked = true;
                EditorSelfTestMarks::note( "inspector.sceneref.none" ); // 시나리오가 누른다 — 콤보를 연 프레임에만 그려진다
                for ( const unique_ptr<Scene>& scene : pSceneManager->getLoadedScenes() )
                {
                    if ( scene == nullptr || scene->getObjectManager() == nullptr )
                        continue;
                    bPicked = drawSceneChoices( *scene, *pValue, picked ) || bPicked;
                }
                ImGui::EndCombo();
                if ( bPicked == false || picked == *pValue )
                    return true;
                // `picked` 는 값 자체라 값 주소 쪽 변환으로 쓴다(객체 주소 쪽은 오프셋을 한 번 더 더한다).
                grid.applyPropertyTextAsEdit( instance, prop,
                                              SerializerUtil::formatValueText( ValueAddress{ &picked }, prop._typeName, SerializeContext::getDefault( SerializePurpose::Authoring ) ),
                                              "Pick Scene Object" );
                return true;
            }

        private:
            /** @brief 씬 하나의 오브젝트를 이름 순으로 보이고, 고른 것이 있으면 true 입니다(@p outPicked 에 참조를 담는다). */
            bool drawSceneChoices( const Scene& scene, const SceneObjectReference& current, SceneObjectReference& outPicked )
            {
                const string& sourcePath = scene.getSourcePath();
                // 경로가 없는 씬(저장한 적 없음)은 참조를 만들 수 없다 — 이유를 보이고 건너뛴다.
                if ( sourcePath.empty() )
                {
                    ImGui::TextDisabled( "(unsaved scene: save it to reference its objects)" );
                    return false;
                }
                _listChoice.clear();
                scene.getObjectManager()->forEachGameObject( [this]( GameObject* pObject )
                { _listChoice.push_back( SceneObjectChoice{ string( pObject->getName().c_str() ), pObject->getObjectID() } ); } );
                std::sort( _listChoice.begin(), _listChoice.end(), []( const SceneObjectChoice& lhs, const SceneObjectChoice& rhs )
                { return lhs._label < rhs._label; } );

                bool bPicked = false;
                ImGui::SeparatorText( FileUtil::getFileNamePart( sourcePath ).c_str() );
                for ( const SceneObjectChoice& choice : _listChoice )
                {
                    SceneObjectReference candidate{};
                    const bool           bMakeable = SceneObjectReferenceUtil::makeReference( scene, choice._objectID, candidate );
                    if ( bMakeable == false )
                        continue;
                    ImGui::PushID( static_cast<int32>( choice._objectID ) );
                    if ( ImGui::Selectable( choice._label.c_str(), candidate == current ) )
                    {
                        outPicked = candidate;
                        bPicked   = true;
                    }
                    ImGui::PopID();
                    if ( EditorSelfTestMarks::isEnabled() )
                        EditorSelfTestMarks::note( ( "inspector.sceneref.choice." + choice._label ).c_str() );
                }
                return bPicked;
            }

            /** @brief 지금 값의 미리보기 글입니다: `<씬 파일> / <오브젝트>` · `(scene not loaded) …` · `(missing) …` · `(none)`. */
            static void makePreview( const SceneManager& sceneManager, const SceneObjectReference& value, SceneObjectResolveResult state, uint64 resolvedID,
                                     fixed_string<constant::kMaxBuffer512>& outPreview )
            {
                switch ( state )
                {
                    case SceneObjectResolveResult::Unset:
                    {
                        formatstring( outPreview.data(), outPreview.capacity(), "(none)" );
                        return;
                    }
                    case SceneObjectResolveResult::SceneNotLoaded:
                    {
                        formatstring( outPreview.data(), outPreview.capacity(), "(scene not loaded) %# #%#", value._scenePath, value._fileID );
                        return;
                    }
                    case SceneObjectResolveResult::ObjectMissing:
                    {
                        formatstring( outPreview.data(), outPreview.capacity(), "(missing) %# #%#", value._scenePath, value._fileID );
                        return;
                    }
                    case SceneObjectResolveResult::Resolved:
                    {
                        break;
                    }
                }
                const GameObject* pObject = findObject( sceneManager, value, resolvedID );
                formatstring( outPreview.data(), outPreview.capacity(), "%# / %#", FileUtil::getFileNamePart( value._scenePath ), pObject != nullptr ? pObject->getName().c_str() : "?" );
            }

            /** @brief 해석된 번호의 오브젝트를 그 참조의 씬에서 찾습니다. */
            static const GameObject* findObject( const SceneManager& sceneManager, const SceneObjectReference& value, uint64 objectID )
            {
                for ( const unique_ptr<Scene>& scene : sceneManager.getLoadedScenes() )
                {
                    if ( scene == nullptr || scene->getObjectManager() == nullptr )
                        continue;
                    if ( SceneObjectReferenceUtil::isSameScenePath( scene->getSourcePath(), value._scenePath ) )
                        return scene->getObjectManager()->findGameObjectByID( objectID );
                }
                return nullptr;
            }

            vector<SceneObjectChoice> _listChoice{};
        };
    } // namespace

    SW_EDITOR_PROPERTY_DRAWER( SceneObjectReference, "SceneObjectReference", SceneObjectReferencePropertyDrawer );
} // namespace sw::editor
