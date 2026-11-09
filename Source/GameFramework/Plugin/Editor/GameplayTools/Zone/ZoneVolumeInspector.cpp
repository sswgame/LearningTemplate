/**
 * @file ZoneVolumeInspector.cpp
 * @brief 지역 볼륨의 인스펙터 구역 — 프리셋(프로필 · 변형)에서 물려받은 모듈과 이 자리가 바꾼 모듈을 가르고, 바꾼 것마다 되돌리기를 둡니다.
 * @details 프리팹 덮어쓰기 화면과 같은 길이다: 줄마다 출처(물려받음 · 바꿈 · 통째로 바꿈 · 더함 · 뺌)를 흐린 글로 붙이고, 덮어쓴 모듈마다 되돌리기가 덮어쓰기 항목 하나를 지운다
 *          (되돌리기 기록 · 씬 dirty 는 `InspectorObjectEdit`). 매개변수는 합친 값을 칸으로 보인다 — 물려받은 값을 고치면 이 자리의 합치기 덮어쓰기가 되고, 이 자리가 적은 값은 표시와
 *          되돌리기(그 매개변수만 — 항목이 비면 항목째, `ZoneModuleDef::revertOverrideParam`)를 단다. 통째로 바꾸기 · 빼기 항목과 칸 밖의 것(조건 · 경우)은 반사 칸에서 고친다.
 *          언리얼 Details 의 "Reset to default" 화살표 ·
 *          유니티 Prefab Overrides 창(굵은 글 · 파란 막대 · Revert)과 같은 자리다(기억). 강조색은 쓰지 않는다(상태는 글 — 앱 화면 규칙).
 */
#include "pch.h"

#include "Core/String/formatString.h"

#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Panels/Inspector/IInspectorComponent.h"
#include "Editor/Panels/Inspector/Model/InspectorComponentManager.h"
#include "Editor/Panels/Inspector/Model/InspectorObjectEdit.h"

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
                { (void)ZoneModuleDef::revertOverrideModule( volume.getModuleOverrides(), name ); } );
            }

            /** @brief 그리는 동안 모은 매개변수 편집 하나입니다 — 목록은 그리기가 끝난 뒤 바꾼다. */
            struct PendingParamEdit
            {
                hashed_string _module{};
                hashed_string _param{};
                string        _value{};
                bool          _bRevert{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            };

            /** @brief 매개변수 덮어쓰기 · 되돌리기를 되돌리기 기록과 함께 적용합니다. */
            static void applyParamEdit( ZoneVolumeComponent& volume, const PendingParamEdit& edit )
            {
                const utf8* pUndo = edit._bRevert ? SW_LOCKEY( "EdUndo", "ZoneRevertParam" ) : SW_LOCKEY( "EdUndo", "ZoneOverrideParam" );
                InspectorObjectEdit::applyObjectEdit( volume.getOwner(), pUndo, [&volume, &edit]()
                {
                    if ( edit._bRevert )
                        (void)ZoneModuleDef::revertOverrideParam( volume.getModuleOverrides(), edit._module, edit._param );
                    else
                        (void)ZoneModuleDef::setOverrideParam( volume.getModuleOverrides(), edit._module, edit._param, edit._value );
                } );
            }

            /**
             * @brief 모듈 하나의 매개변수(합친 값)를 칸으로 그립니다. 이 자리가 적은 값은 표시(*)와 되돌리기, 물려받은 값은 흐린 이름표입니다.
             *        통째로 바꾸기 · 빼기 항목의 모듈은 칸을 읽기 전용 글로 둔다(그 항목은 반사 칸에서 고친다).
             */
            static void drawParams( const ZoneModuleDef& merged, const ZoneModuleDef* pOverride, bool bInPreset, PendingParamEdit& outEdit, bool& outbEdited )
            {
                const bool bEditable = pOverride == nullptr || pOverride->_op == ZoneOverrideOp::Merge;
                // 매개변수 하나의 되돌리기는 물려받을 값이 있을 때만(프리셋에 있는 모듈) — 이 자리가 더한 모듈은 모듈째 되돌린다.
                const utf8*   pRevertLabel = SW_LOCKEY( "EdButton", "ZoneRevert" );
                const float32 revertWidth  = ImGui::CalcTextSize( pRevertLabel ).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
                for ( const ZoneParamDef& param : merged._listParam )
                {
                    const bool bOwn        = pOverride != nullptr && pOverride->findParam( param._name ) != nullptr;
                    const bool bRevertable = bOwn && bInPreset;
                    ImGui::PushID( param._name.c_str() );
                    if ( bEditable == false )
                    {
                        ImGui::TextDisabled( "    %s = %s", param._name.c_str(), param._value.c_str() );
                        ImGui::PopID();
                        continue;
                    }
                    ImGui::TextUnformatted( bOwn ? "*" : " " ); // 이 자리가 적은 값 — 글 표시(강조색 없음, 앱 화면 규칙)
                    ImGui::SameLine();
                    fixed_string<constant::buffer::kMax128> label;
                    formatstring( label.data(), label.capacity(), "%###value", param._name.c_str() );
                    string value = param._value;
                    if ( EditorWidgets::drawTextField( label.c_str(), value, bRevertable ? -revertWidth : -1.0f ) && value != param._value )
                    {
                        outEdit    = PendingParamEdit{ merged._name, param._name, value, false };
                        outbEdited = true;
                    }
                    if ( bRevertable )
                    {
                        ImGui::SameLine();
                        if ( ImGui::SmallButton( pRevertLabel ) )
                        {
                            outEdit    = PendingParamEdit{ merged._name, param._name, {}, true };
                            outbEdited = true;
                        }
                        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTip", "ZoneRevertParam" ) );
                    }
                    ImGui::PopID();
                }
                if ( merged._condition.empty() == false )
                {
                    fixed_string<constant::buffer::kMax256> text;
                    formatstring( text.data(), text.capacity(), "%# %#", SW_LOCKEY( "EdLabel", "ZoneCondition" ), merged._condition );
                    EditorWidgets::drawEmptyHint( text.c_str() );
                }
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
                Util::PendingParamEdit       paramEdit{};
                bool                         bParamEdited = false;
                // 프리셋 순서대로(합친 결과의 순서), 이 자리가 더한 모듈은 끝에 — 뺀 모듈도 줄로 남겨 되돌릴 수 있게 한다.
                const auto drawRow = [&]( const hashed_string& name, const ZoneModuleDef* pMerged, bool bInPreset )
                {
                    const ZoneModuleDef*   pOverride = Util::findOverride( listOverride, name );
                    const ZoneModuleOrigin origin    = Util::classify( pOverride, bInPreset );
                    ImGui::PushID( name.c_str() );
                    // 이 자리가 바꾼 모듈은 펼친 채 연다 — 바꾼 것을 먼저 보인다(프리팹 덮어쓰기 화면의 "바뀐 것만" 과 같은 뜻).
                    const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ( pOverride != nullptr ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None );
                    const bool               bOpen = ImGui::TreeNodeEx( "##module", flags, "%s  %s", name.c_str(), pMerged != nullptr ? pMerged->_kind.c_str() : "" );
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
                            Util::drawParams( *pMerged, pOverride, bInPreset, paramEdit, bParamEdited );
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
                else if ( bParamEdited )
                    Util::applyParamEdit( *pVolume, paramEdit );
            }
        };
    } // namespace

    SW_EDITOR_INSPECTOR( ZoneVolumeComponent, ZoneVolumeInspector );
} // namespace sw::editor
