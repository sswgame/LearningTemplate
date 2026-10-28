/**
 * @file UIPreviewLogic.h
 * @brief UI 미리보기 패널(`UIPreviewPanel`)의 판단입니다 — 해상도 견본 → 뷰포트, 이미지 맞춤 · 점 옮기기, 위젯 트리 줄. ImGui 를 모른다(EditorTest 가 테스트한다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/UI/WidgetTree/WidgetTypes.h"

namespace sw
{
    struct PropertyInfo;
    struct TypeInfo;
    struct UIResolutionPreset;
    struct UIScaleSettings;
    struct WidgetLayoutSlot;

    class GridPanel;
    class PanelWidget;
    class TypeRegistry;
    class UIScreen;
    class UISystem;
    class Widget;
    class WidgetTree;
} // namespace sw

namespace sw::editor
{
    /** @brief 캔버스 자식의 슬롯 손잡이 하나입니다(디자이너). 가운데는 옮기기, 나머지는 그 변만 옮깁니다. */
    enum class UIPreviewSlotHandle : uint8
    {
        Move = 0,
        Left,
        Top,
        Right,
        Bottom
    };

    /**
     * @brief 캔버스 슬롯의 앵커 프리셋입니다(UMG 디자이너의 앵커 드롭다운). 아홉 점 앵커와 가로 · 세로 · 전체 늘이기입니다.
     * @details 가로 늘이기는 세로 가운데, 세로 늘이기는 가로 가운데에 앵커를 둔다.
     */
    enum class UIPreviewAnchorPreset : uint8
    {
        TopLeft = 0,
        TopCenter,
        TopRight,
        CenterLeft,
        Center,
        CenterRight,
        BottomLeft,
        BottomCenter,
        BottomRight,
        StretchHorizontal,
        StretchVertical,
        StretchAll,
        Count
    };

    /** @brief 앵커 프리셋 하나의 앵커 값입니다(부모 크기 비율, 0~1). */
    struct UIPreviewAnchorValues
    {
        float2 _anchorMin{};
        float2 _anchorMax{};
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @brief 고른 위젯의 부모 패널이 읽는 슬롯 필드 그룹입니다(디자이너의 Slot 필드가 이것만 보여 준다 — UMG 디자이너가 부모 패널의 슬롯 타입만 보여 주는 것과 같다).
     * @details 슬롯은 모든 패널의 필드를 한 구조체(`WidgetLayoutSlot`)에 모았으므로, 패널 종류마다 슬롯 타입을 두지 않고 "어느 그룹을 보이는지" 만 고른다.
     */
    struct UIPreviewSlotFields
    {
        bool _bPaddingAlignment{ false }; ///< 여백 · 가로/세로 정렬(캔버스 밖의 모든 패널)
        bool _bBoxSize{ false };          ///< 크기 규칙 · 채우기 비율(상자 패널)
        bool _bGrid{ false };             ///< 행 · 열 · 넓이(격자 패널)
        bool _bCanvas{ false };           ///< 앵커 · 오프셋 · 자동 크기 · 커지는 쪽 · z 순서(캔버스 패널)
        bool _bSizeOverride{ false };     ///< 크기 덮어쓰기 · 최소 · 최대(부모가 있으면 모두)
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 위젯 트리 목록의 줄 하나입니다(문서 순서). */
    struct UIPreviewWidgetRow
    {
        string   _label{}; ///< "타입 #이름"(이름이 없으면 타입만)
        WidgetID _widget{ kInvalidWidgetID };
        uint32   _depth{ 0 }; ///< 루트 = 0
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct UIPreviewLogic
     * @brief 미리보기 패널이 부르는 순수 함수들입니다(유니티 UI Builder · UMG 디자이너의 미리보기 해상도 · 위젯 계층 창에 해당합니다).
     */
    struct UIPreviewLogic
    {
        /**
         * @brief 견본 @p resolution 을 게임과 같은 배율 규칙(@p settings)으로 UI 뷰포트로 만듭니다 — 사용자 배율 @p userScale 을 곱하고 각 변을 물리 크기의
         *        @p safeZoneRatio 만큼 안쪽으로(안전 영역 보기). 견본은 엔진 표(`UIResolutionPresetTable`)의 한 줄입니다.
         */
        static UIViewport makeViewport( const UIScaleSettings& settings, const UIResolutionPreset& resolution, float32 userScale, float32 safeZoneRatio );
        /** @brief 크기 @p imageSize 의 이미지를 @p available 안에 비율을 지켜 맞춘 크기입니다(늘리지 않는다 — 1 배가 상한). */
        static float2 fitImage( const float2& imageSize, const float2& available );
        /** @brief 그린 이미지 위의 점 @p imagePoint(이미지 왼쪽 위 원점, 그린 크기 @p drawSize)를 UI 단위 점으로 옮깁니다. */
        static float2 mapImageToUI( const float2& imagePoint, const float2& drawSize, const UIViewport& viewport );
        /** @brief UI 단위 사각형 @p rect 를 그린 이미지 위 사각형(이미지 왼쪽 위 원점)으로 옮깁니다. */
        static UIRect mapUIToImage( const UIRect& rect, const float2& drawSize, const UIViewport& viewport );
        /** @brief 트리의 위젯을 문서 순서로 한 줄씩(깊이 · "타입 #이름") 모읍니다. */
        static void collectWidgetRows( const WidgetTree& tree, vector<UIPreviewWidgetRow>& outListRow );
        /** @brief UI 점 @p point 아래의 맨 위(문서 순서로 마지막) 보이는 위젯입니다. 없으면 무효입니다. */
        static WidgetID findWidgetAt( const WidgetTree& tree, const float2& point );
        /** @brief 미리보기 렌더 텍스처 경로입니다 — 크기마다 다르다(렌더 텍스처 크기는 처음 만들 때 정해진다). */
        static string makeTargetPath( const UIViewport& viewport );

        // --- 언어 고르기 ---------------------------------------------------------------
        /** @brief 미리보기 언어를 "게임 언어 그대로" 로 두는 값입니다(빈 코드). */
        static constexpr const utf8* kGameLanguage = "";
        /**
         * @brief 언어 콤보의 코드 목록입니다. 첫 줄은 게임 언어 그대로(`kGameLanguage`), 이어서 @p listAvailable 의 실제 문화권을 사전순으로, 끝에 의사 문화권(`qps_ploc`,
         *        `qps_plocm`)을 둡니다. 의사 문화권은 @p listAvailable 에 없어도 넣고 중복은 한 번만 넣습니다(Dev 빌드에서만 `getAvailableLanguages` 가 내놓는다).
         */
        static void makeLanguageChoices( const vector<string>& listAvailable, vector<string>& outListCode );
        /** @brief @p code(정규 표기)가 의사 문화권(`qps_ploc`, `qps_plocm`)이면 true 입니다. 화면에서 번역이 아니라 원문 변환 글을 보인다는 표시에 씁니다. */
        static bool isPseudoLanguage( string_view code );

        // --- 위젯 고르기 · 속성 고치기 ---------------------------------------------------------------
        /** @brief 위젯 번호 @p id 의 문서 순서 번호입니다. 없으면 `kNoOrdinal` 입니다. 문서를 다시 읽으면 위젯 번호가 바뀌므로 고른 위젯을 번호로 이어 갑니다. */
        static constexpr uint32 kNoOrdinal = 0xFFFFFFFFu;
        static uint32           findWidgetOrdinal( const WidgetTree& tree, WidgetID id );
        /** @brief 문서 순서 번호 @p ordinal 의 위젯 번호입니다. 범위 밖이면 무효입니다. */
        static WidgetID findWidgetByOrdinal( const WidgetTree& tree, uint32 ordinal );
        /** @brief 위젯의 프로퍼티 @p name (기반 타입 것 포함)입니다. 없으면 nullptr 입니다. */
        static const PropertyInfo* findWidgetProperty( const Widget& widget, string_view name );
        /** @brief 프로퍼티의 지금 값을 문서에 쓰는 텍스트(XML 속성 값과 같은 형태)로 돌려줍니다. */
        static string formatWidgetProperty( const Widget& widget, const PropertyInfo& prop );
        /**
         * @brief 텍스트 @p text 를 프로퍼티 값으로 쓰고 위젯에 알립니다(`Widget::onBoundPropertyChanged` — 세터와 같은 무효화). 읽지 못하면 false 이고 값은 그대로입니다.
         * @details 되돌리기와 다시 하기가 이 함수로 값을 되돌린다. 그리드의 편집도 같은 알림을 거치므로 화면은 어느 쪽이든 같은 경로로 다시 그려진다.
         */
        [[nodiscard]] static bool applyWidgetProperty( Widget& widget, const PropertyInfo& prop, string_view text );
        /**
         * @brief 미리보기 화면 @p screen 을 문서 텍스트로 씁니다(`UIDocumentWriter`) — 저장할 파일의 내용입니다.
         * @details 스타일 시트 목록은 화면이 아니라 문서 @p documentPath 의 것이다. 화면의 목록은 조각 문서의 시트까지 모은 것이라 그대로 쓰면 조각의 시트가 이 문서로 복사된다.
         *          애니메이션은 화면의 재생기가 가진 문서 애니메이션을 그대로 쓴다.
         * @return 문서나 루트가 없으면 false 이고 @p outError 에 까닭을 적습니다.
         */
        [[nodiscard]] static bool makeDocumentText( UISystem& ui, const UIScreen& screen, string_view documentPath, string& outText, string& outError );
        // --- 디자이너: 팔레트 · 끌어 놓기 · 슬롯 손잡이 ---------------------------------------------------------------
        /** @brief 문서 순서 번호 @p ordinal 의 위젯이 패널이면 그 패널입니다. 패널이 아니거나 범위 밖이면 nullptr 입니다. */
        static PanelWidget* findPanelByOrdinal( WidgetTree& tree, uint32 ordinal );
        /**
         * @brief 팔레트 목록입니다 — 만들 수 있는(추상이 아닌) 위젯 타입을 이름순으로 담습니다(`Widget` 의 모든 파생 타입, 게임 모듈의 위젯 포함).
         * @details 위젯 타입이 늘면 목록도 는다. 타입 레지스트리가 아는 것이 곧 팔레트라 따로 테이블을 두지 않는다(유니티 UI Builder 의 Library, UMG 의 Palette 와 같다).
         */
        static void collectPaletteTypes( const TypeRegistry& registry, vector<const TypeInfo*>& outListType );
        /**
         * @brief 놓은 위치 @p pTarget 이 받을 패널을 고릅니다 — 패널이면 그 패널(자식 끝에), 아니면 그 위젯의 부모(그 위젯 바로 뒤에)입니다. 둘 다 없으면 루트가 패널일 때 루트입니다.
         * @param outIndex 새 위젯이 들어갈 자식 번호입니다.
         */
        static PanelWidget* findDropContainer( WidgetTree& tree, Widget* pTarget, uint32& outIndex );
        /** @brief @p type 의 위젯을 만들어 문서 순서 번호 @p parentOrdinal 인 패널의 @p index 번째에 끼웁니다. 못 끼우면 nullptr 입니다(패널이 아니거나 타입을 만들 수 없다). */
        static Widget* insertWidget( WidgetTree& tree, const TypeInfo& type, uint32 parentOrdinal, uint32 index );
        /** @brief 문서 순서 번호 @p parentOrdinal 인 패널의 @p index 번째 자식을 분리해 소유권을 돌려줍니다(지우거나 되돌릴 때 다시 끼운다). 없으면 빈 포인터입니다. */
        static unique_ptr<Widget> detachWidget( WidgetTree& tree, uint32 parentOrdinal, uint32 index );
        /** @brief 분리한 위젯을 같은 위치에 다시 끼웁니다(지우기 되돌리기). 못 끼우면 nullptr 입니다. */
        static Widget* attachWidget( WidgetTree& tree, unique_ptr<Widget> widget, uint32 parentOrdinal, uint32 index );
        /** @brief 위젯의 슬롯을 손잡이로 고칠 수 있으면 true 입니다 — 부모가 캔버스 패널(앵커와 오프셋)일 때입니다. */
        static bool canEditSlotByHandle( const Widget& widget );
        /** @brief 위젯 @p widget 의 부모 패널 종류로 Slot 필드에 보일 필드 그룹을 고릅니다. 부모가 없으면(루트) 모두 false 입니다. */
        static UIPreviewSlotFields findSlotFields( const Widget& widget );
        /**
         * @brief 손잡이 @p handle 을 UI 단위 @p deltaUI 만큼 끈 결과를 슬롯에 적용합니다. 가운데는 두 오프셋을 함께, 변은 그 변의 오프셋만 옮깁니다(자동 크기이면 오른쪽 · 아래 오프셋은 쓰이지 않아 옮기지 않는다).
         * @return 슬롯이 바뀌었으면 true 입니다.
         */
        [[nodiscard]] static bool applySlotDrag( WidgetLayoutSlot& inoutSlot, UIPreviewSlotHandle handle, const float2& deltaUI );
        /** @brief 앵커 프리셋 @p preset 의 앵커 값입니다. 범위 밖이면 좌상(0, 0)입니다. */
        static UIPreviewAnchorValues getAnchorPreset( UIPreviewAnchorPreset preset );
        /** @brief 앵커 프리셋 @p preset 의 자동화 표식 이름입니다("topleft" 등). */
        static const utf8* getAnchorPresetMark( UIPreviewAnchorPreset preset );
        /**
         * @brief 앵커를 프리셋 @p preset 으로 바꾸고, 위젯이 화면에서 제자리에 머물도록 오프셋을 다시 채웁니다(UMG 의 기본 동작 — 앵커만 옮기고 위치는 지킨다).
         * @param localRect 위젯의 지금 사각형(부모 왼쪽 위 원점, UI 단위)입니다. 자동 크기도 실제 크기가 들어오므로 오른쪽 · 아래 오프셋까지 맞게 채워진다.
         *        슬롯의 안쪽 여백(`_padding`)은 이 사각형 바깥에 있다 — 슬롯 사각형은 여백을 도로 더한 것이다.
         * @param parentSize 부모 캔버스의 크기(UI 단위)입니다.
         * @return 슬롯이 바뀌었으면 true 입니다.
         */
        [[nodiscard]] static bool applyAnchorPreset( WidgetLayoutSlot& inoutSlot, UIPreviewAnchorPreset preset, const UIRect& localRect, const float2& parentSize );

        // --- 디자이너: 상자 순서 · 격자 셀 손잡이 ---------------------------------------------------------------
        /**
         * @brief 상자 패널에서 끄는 점 @p point 가 들어갈 위치입니다 — 자식 가운데(@p listChildCenter, 상자 축 좌표, 자식 순서)가 @p point 보다 작은 자식 수입니다.
         * @details 끄는 자식 자신도 목록에 있다. 분리한 뒤의 번호는 `findReorderIndex` 가 고친다. 마지막 자식의 가운데가 첫 자식보다 작으면(RTL 거울 등) 거꾸로 놓인 것으로
         *          보고 가운데가 @p point 보다 **큰** 자식 수를 센다.
         */
        static uint32 findBoxInsertIndex( const vector<float32>& listChildCenter, float32 point );
        /** @brief 자식 @p fromIndex 를 분리하고 끼울 번호입니다 — 사이 위치 @p insertIndex(떼기 전 번호)가 자신 뒤면 하나 당깁니다. */
        static uint32 findReorderIndex( uint32 fromIndex, uint32 insertIndex );
        /**
         * @brief 격자에서 점 @p point 가 속한 줄(행이나 열) 번호입니다. @p listTrackStart 는 줄마다 시작 좌표(오름차순), @p trackEnd 는 마지막 줄의 끝입니다.
         * @details 첫 줄 앞이면 0, @p trackEnd 를 넘으면 줄 수(새 줄)입니다. 줄이 없으면 0 입니다.
         */
        static uint32 findGridTrack( const vector<float32>& listTrackStart, float32 trackEnd, float32 point );
        /**
         * @brief 줄 크기 @p listTrackSize(트랙 순서)와 간격 @p spacing 으로 줄마다 시작 좌표(첫 줄 0)와 마지막 줄의 끝을 채웁니다.
         * @details 크기는 배치가 정한 값(`GridPanel::getArrangedColumnSizes` · `getArrangedRowSizes`)이다 — 자식 사각형으로 추정하지 않으므로 걸친 칸이 줄을 흐리지 않고
         *          자식 없는 줄도 제 폭을 가져 끌어 놓을 수 있다.
         */
        static void makeGridTrackStarts( const vector<float32>& listTrackSize, float32 spacing, vector<float32>& outListTrackStart, float32& outTrackEnd );
        /**
         * @brief 화면 점 @p screenPoint(UI 단위)가 놓인 격자 @p grid 의 칸입니다. 격자 로컬로 되돌리고 RTL 거울이면 x 를 뒤집어 배치의 줄과 비교합니다.
         * @return 렌더 변환이 퇴화해 로컬로 되돌릴 수 없으면 false 입니다.
         */
        [[nodiscard]] static bool findGridCell( const GridPanel& grid, const float2& screenPoint, uint32& outRow, uint32& outColumn );
        /** @brief 시간 막대 줄입니다 — "0.25 / 1.00 s". */
        static string makeTimeLabel( float32 time, float32 duration );
    };
} // namespace sw::editor
