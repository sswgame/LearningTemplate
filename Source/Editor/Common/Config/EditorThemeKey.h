/**
 * @file EditorThemeKey.h
 * @brief 테마 토큰의 키 형(`EditorThemeColorKey` · `EditorThemeMetricKey`)과 리터럴 매크로입니다. 값은 `Content/editor/theme/<이름>.tokens.json` 에만 있습니다.
 */
#pragma once
#include "Editor/Common/Config/EditorListedName.h"

namespace sw::editor
{
    /** @brief 코드가 쓸 수 있는 색 키(`_color`)입니다. 토큰을 더하면 여기에도 더한다(대조: `EditorThemeTokens::loadTheme`). 데이터가 이름으로 드는 색(`Component.<이름>`)은 없다. */
    struct EditorThemeColorKeyList
    {
        static constexpr const utf8* kArrName[] = {
            "Base.Text", "Base.TextMuted", "Base.Accent", "Base.WindowBg", "Base.PanelBg", "Base.HeaderBg", "Base.FrameBg", "Base.Border",
            "Base.ScrollbarBg", "Base.DockingEmptyBg", "Base.TableRowBgAlt", "Base.OverlayBg", "Base.Highlight", "Base.ToggleInactive",
            "Base.ToggleActive", "Signal.Success", "Signal.Warning", "Signal.Error", "Signal.Info", "Asset.AnimationClip", "Asset.AnimationGraph",
            "Asset.Audio", "Asset.Data", "Asset.DialogueGraph", "Asset.Fracture", "Asset.Heightfield", "Asset.LayerWeights", "Asset.Material",
            "Asset.Mesh", "Asset.Prefab", "Asset.Rig", "Asset.Sequence", "Asset.Shader", "Asset.Skeleton", "Asset.SpriteClip", "Asset.Texture",
            "Asset.TileMap", "Asset.UIDocument", "Viewport.SelectionOutline", "Viewport.AxisX", "Viewport.AxisY", "Viewport.AxisZ",
            "Viewport.ColliderWire", "Viewport.CameraWire", "Viewport.Label", "Graph.Sensor", "Graph.Operator", "Graph.Actuator",
            "Graph.TransitionPin", "Graph.FlowPin", "Viewport.Marker", "Viewport.Bone", "Viewport.Joint", "Viewport.BoneSelected",
            "Viewport.BoneSource", "Viewport.BoneReference", "Viewport.BonePose", "Viewport.HandleOutline", "Viewport.ModifierPreview",
            "Viewport.ModifierStage", "Viewport.ModifierAxis", "Viewport.ModifierHover", "Viewport.ModifierRange", "Viewport.LatticeLine",
            "Viewport.LatticePoint", "Viewport.LatticeSelected", "Viewport.ParkGate", "Viewport.ParkTrack", "Viewport.HousingPlot",
            "Viewport.HousingCell", "InputOverlay.Background", "InputOverlay.Border", "InputOverlay.Title", "InputOverlay.StickBase",
            "InputOverlay.StickRing", "InputOverlay.StickDot", "InputOverlay.TriggerTrack", "InputOverlay.TriggerFill", "InputOverlay.ButtonADown",
            "InputOverlay.ButtonAUp", "InputOverlay.ButtonBDown", "InputOverlay.ButtonBUp", "InputOverlay.ButtonXDown", "InputOverlay.ButtonXUp",
            "InputOverlay.ButtonYDown", "InputOverlay.ButtonYUp", "InputOverlay.ButtonLabel", "InputOverlay.ActiveAction",
            "AccentSwatch.ElectricBlue", "AccentSwatch.EmeraldCyan", "AccentSwatch.NeonViolet", "AccentSwatch.AmberGold", "AccentSwatch.Crimson" };
    };

    /** @brief 코드가 쓸 수 있는 수치 키(`_metric`)입니다. 규칙은 색 키와 같다. */
    struct EditorThemeMetricKeyList
    {
        static constexpr const utf8* kArrName[] = {
            "Radius.Window", "Radius.Frame", "Radius.Popup", "Radius.Tab", "Radius.Scrollbar", "Radius.Grab", "Spacing.WindowPaddingX",
            "Spacing.WindowPaddingY", "Spacing.FramePaddingX", "Spacing.FramePaddingY", "Spacing.ItemSpacingX", "Spacing.ItemSpacingY",
            "Spacing.ItemInnerSpacingX", "Spacing.ItemInnerSpacingY", "Spacing.Indent", "Spacing.ScrollbarSize", "Spacing.GrabMinSize",
            "Stroke.Window", "Stroke.Child", "Stroke.Popup", "Stroke.Frame", "Stroke.Tab", "Typography.FontSize", "Typography.IconScale",
            "Layout.Density", "Layout.PropertyLabelWidth", "Layout.FieldTinyEm", "Layout.FieldNumberEm", "Layout.FieldShortEm", "Layout.FieldStepEm",
            "Layout.FieldComboEm", "Layout.FieldModeEm", "Layout.FieldMediumEm", "Layout.FieldWideEm", "Layout.FieldPathEm", "Layout.TooltipWrapEm",
            "Layout.MenuTrailingIconEm", "Layout.InputMapLayerColumnEm", "Layout.InputMapActionColumnEm" };
    };

    using EditorThemeColorKey  = EditorListedName<EditorThemeColorKeyList>;
    using EditorThemeMetricKey = EditorListedName<EditorThemeMetricKeyList>;
} // namespace sw::editor

/**
 * @brief 테마 색 키 `Ns.Key` 입니다(`EditorThemeColorKey`). 등록 줄 · 값 목록처럼 색을 나중에 찾는 자리가 든다(찾기는 `EditorThemeUtil::findColor`).
 * @details 두 인자 모두 문자열 리터럴이어야 한다. 목록(`EditorThemeColorKeyList`)에 없는 키는 컴파일되지 않는다.
 */
#define SW_THEME_COLOR_KEY( NamespaceLiteral, KeyLiteral ) ::sw::editor::EditorThemeColorKey::makeAt<::sw::editor::EditorThemeColorKey::findIndex( NamespaceLiteral "." KeyLiteral )>()
/** @brief 테마 수치 키 `Ns.Key` 입니다(`EditorThemeMetricKey`). 규칙은 `SW_THEME_COLOR_KEY` 와 같다. */
#define SW_THEME_METRIC_KEY( NamespaceLiteral, KeyLiteral ) ::sw::editor::EditorThemeMetricKey::makeAt<::sw::editor::EditorThemeMetricKey::findIndex( NamespaceLiteral "." KeyLiteral )>()
