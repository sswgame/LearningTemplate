/**
 * @file ZoneVolumeInspector.cpp
 * @brief 지역 볼륨의 인스펙터 구역 — 프리셋(프로필 · 변형)에서 물려받은 모듈과 이 자리가 바꾼 모듈을 가르고, 바꾼 것마다 되돌리기를 둡니다.
 * @details 프리팹 덮어쓰기 화면과 같은 길이다: 줄마다 출처(물려받음 · 바꿈 · 통째로 바꿈 · 더함 · 뺌)를 흐린 글로 붙이고, 이 자리가 적은 매개변수는 본문 글 · 물려받은 것은 흐린 글이며, 덮어쓴 줄마다
 *          되돌리기가 덮어쓰기 항목 하나를 지운다(되돌리기 기록 · 씬 dirty 는 `InspectorObjectEdit`). 매개변수는 합친 값을 보이고, 이 자리가 적은 값에 표시를 단다.
 *          덮어쓰기 자체(`_listModuleOverride`)는 반사 칸으로 그대로 고친다 — 이 구역은 합친 결과를 읽어 보이는 쪽이다. 언리얼 Details 의 "Reset to default" 화살표 ·
 *          유니티 Prefab Overrides 창(굵은 글 · 파란 막대 · Revert)과 같은 자리다(기억). 강조색은 쓰지 않는다(상태는 글 — 앱 화면 규칙).
 */
#include "pch.h"

#include "Core/Container/formatString.h"

#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/InspectorObjectEdit.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentManager.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Runtime/Gameplay/Zone/ZoneVolumeComponent.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 모듈 줄 하나의 출처입니다. */
        enum class ZoneModuleOrigin : uint8
        {
            Inherited = 0, ///< 프리셋 그대로
            Changed,       ///< 이 자리가 칸 몇 개를 바꿨다
            Replaced,      ///< 이 자리가 통째로 바꿨다
            Added,         ///< 이 자리가 더했다
            Removed        ///< 이 자리가 뺐다
        };

        struct ZoneVolumeInspectorUtil
        {
            static const ZoneModuleDef* findOverride( const vector<ZoneModuleDef>& listOverride, const hashed_string& name )
            {
                for ( const ZoneModuleDef& over : listOverride )
                {
                    if ( over._name == name )
                        return &over;
                }
                return nullptr;
            }

            static ZoneModuleOrigin classify( const ZoneModuleDef* pOverride, bool bInPreset )
            {
                if ( pOverride == nullptr )
                    return ZoneModuleOrigin::Inherited;
                if ( bInPreset == false )
                    return ZoneModuleOrigin::Added;
                switch ( pOverride->_op )
                {
                    case ZoneOverrideOp::Merge:
                        return ZoneModuleOrigin::Changed;
                    case ZoneOverrideOp::Replace:
                        return ZoneModuleOrigin::Replaced;
                    case ZoneOverrideOp::Remove:
                        return ZoneModuleOrigin::Removed;
                }
                return ZoneModuleOrigin::Changed;
            }

            static const utf8* getOriginLabel( ZoneModuleOrigin origin )
            {
                switch ( origin )
                {
                    case ZoneModuleOrigin::Inherited:
                        return SW_LOCKEY( "EdLabel", "ZoneInherited" );
                    case ZoneModuleOrigin::Changed:
                        return SW_LOCKEY( "EdLabel", "ZoneChanged" );
                    case ZoneModuleOrigin::Replaced:
                        return SW_LOCKEY( "EdLabel", "ZoneReplaced" );
                    case ZoneModuleOrigin::Added:
                        return SW_LOCKEY( "EdLabel", "ZoneAdded" );
                    case ZoneModuleOrigin::Removed:
                        return SW_LOCKEY( "EdLabel", "ZoneRemoved" );
                }
                return "";
            }

            /** @brief 덮어쓰기 항목 @p name 을 지웁니다(되돌리기 기록과 함께). */
            static void revert( ZoneVolumeComponent& volume, const hashed_string& name )
            {
                InspectorObjectEdit::applyObjectEdit( volume.getOwner(), SW_LOCKEY( "EdUndo", "ZoneRevertModule" ), [&volume, &name]()
                {
                    vector<ZoneModuleDef>& listOverride = volume.getModuleOverrides();
                    for ( size_t index = 0; index < listOverride.size(); ++index )
                    {
                        if ( listOverride[index]._name == name )
                        {
                            listOverride.erase( listOverride.begin() + static_cast<ptrdiff_t>( index ) );
                            return;
                        }
                    }
                } );
            }

            /** @brief 모듈 하나의 매개변수(합친 값)를 그립니다. 이 자리가 적은 값은 본문 글과 표시, 물려받은 값은 흐린 글입니다. */
            static void drawParams( const ZoneModuleDef& merged, const ZoneModuleDef* pOverride )
            {
                for ( const ZoneParamDef& param : merged._listParam )
                {
                    const bool bOwn = pOverride != nullptr && pOverride->findParam( param._name ) != nullptr;
                    if ( bOwn )
                        ImGui::BulletText( "%s = %s", param._name.c_str(), param._value.c_str() );
                    else
                        ImGui::TextDisabled( "    %s = %s", param._name.c_str(), param._value.c_str() ); // 물려받은 값은 흐린 글(테마의 비활성 글 토큰)
                }
                if ( merged._condition.empty() == false )
                    EditorWidgets::drawEmptyHint( formatToString( "%# %#", SW_LOCKEY( "EdLabel", "ZoneCondition" ), merged._condition ).c_str() );
            }
        };

        /** @brief 지역 볼륨의 물려받은 · 바꾼 모듈 구역입니다(파일 머리말 참고). */
        class ZoneVolumeInspector final : public IInspectorComponent
        {
        public:
            void drawSection( Component* pComponent ) override
            {
                using Util                   = ZoneVolumeInspectorUtil;
                ZoneVolumeComponent* pVolume = static_cast<ZoneVolumeComponent*>( pComponent );
                ImGui::SeparatorText( SW_LOCKEY( "EdSection", "ZoneModules" ) );
                const shared_ptr<const ZoneProfile> pPreset = pVolume->getProfileRef().acquire();
                if ( pPreset == nullptr )
                {
                    EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorTip", "ZoneNoProfile" ) );
                    return;
                }
                ZoneProfile merged;
                if ( pVolume->composeProfile( merged ) == false )
                    EditorWidgets::drawEmptyHint( SW_LOCKEY( "EditorTip", "ZoneOverridesInvalid" ) );

                const vector<ZoneModuleDef>& listOverride = pVolume->getModuleOverrides();
                hashed_string                revertName{};
                // 프리셋 순서대로(합친 결과의 순서), 이 자리가 더한 모듈은 끝에 — 뺀 모듈도 줄로 남겨 되돌릴 수 있게 한다.
                const auto drawRow = [&]( const hashed_string& name, const ZoneModuleDef* pMerged, bool bInPreset )
                {
                    const ZoneModuleDef*   pOverride = Util::findOverride( listOverride, name );
                    const ZoneModuleOrigin origin    = Util::classify( pOverride, bInPreset );
                    ImGui::PushID( name.c_str() );
                    const bool bOpen = ImGui::TreeNodeEx( "##module", ImGuiTreeNodeFlags_SpanAvailWidth, "%s  %s", name.c_str(), pMerged != nullptr ? pMerged->_kind.c_str() : "" );
                    ImGui::SameLine();
                    ImGui::TextDisabled( "%s", Util::getOriginLabel( origin ) );
                    if ( pOverride != nullptr )
                    {
                        ImGui::SameLine();
                        if ( ImGui::SmallButton( SW_LOCKEY( "EdButton", "ZoneRevert" ) ) )
                            revertName = name;
                        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTip", "ZoneRevert" ) );
                    }
                    if ( bOpen )
                    {
                        if ( pMerged != nullptr )
                            Util::drawParams( *pMerged, pOverride );
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                };
                for ( const ZoneModuleDef& presetModule : pPreset->_listModule )
                    drawRow( presetModule._name, merged.findModule( presetModule._name ), true );
                for ( const ZoneModuleDef& over : listOverride )
                {
                    if ( pPreset->findModule( over._name ) == nullptr )
                        drawRow( over._name, merged.findModule( over._name ), false );
                }
                // 지우기는 그리기가 끝난 뒤 — 그리는 동안 목록을 바꾸지 않는다.
                if ( revertName.empty() == false )
                    Util::revert( *pVolume, revertName );
            }
        };
    } // namespace

    SW_EDITOR_INSPECTOR( ZoneVolumeComponent, ZoneVolumeInspector );
} // namespace sw::editor
