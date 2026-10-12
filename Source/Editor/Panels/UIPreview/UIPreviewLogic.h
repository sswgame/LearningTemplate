/**
 * @file UIPreviewLogic.h
 * @brief UI 미리보기 패널(`UIPreviewPanel`)의 판단입니다 — 해상도 견본 → 뷰포트, 이미지 맞춤 · 점 옮기기, 위젯 트리 줄. ImGui 를 모른다(EditorTest 가 테스트한다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/UI/Base/WidgetTypes.h"

namespace sw
{
    struct PropertyInfo;
    struct UIScaleSettings;

    class UIScreen;
    class UISystem;
    class Widget;
    class WidgetTree;
} // namespace sw

namespace sw::editor
{
    /** @brief 해상도 견본 하나입니다(물리 픽셀). */
    struct UIPreviewResolution
    {
        const utf8* _pName{ "" };
        uint32      _width{ 0 };
        uint32      _height{ 0 };
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
     * @brief 미리보기 패널이 부르는 순수 함수들입니다(유니티 UI Builder · UMG 디자이너의 미리보기 해상도 · 위젯 계층 칸 자리).
     */
    struct UIPreviewLogic
    {
        /** @brief 해상도 견본 수와 @p index 의 견본입니다 — 1280×720 · 1920×1080 · 2560×1440 · 3840×2160 · 21:9 · 세로. */
        static uint32                     getResolutionCount();
        static const UIPreviewResolution& getResolution( uint32 index );
        /**
         * @brief 견본 @p index 를 게임과 같은 배율 규칙(@p settings)으로 UI 뷰포트로 만듭니다 — 사용자 배율 @p userScale 을 곱하고 각 변을 물리 크기의
         *        @p safeZoneRatio 만큼 안쪽으로(안전 영역 보기). 범위 밖 견본은 첫 견본입니다.
         */
        static UIViewport makeViewport( const UIScaleSettings& settings, uint32 index, float32 userScale, float32 safeZoneRatio );
        /** @brief 크기 @p imageSize 의 그림을 @p available 안에 비율을 지켜 맞춘 크기입니다(늘리지 않는다 — 1 배가 상한). */
        static float2 fitImage( const float2& imageSize, const float2& available );
        /** @brief 그린 그림 위의 점 @p imagePoint(그림 왼쪽 위 원점, 그린 크기 @p drawSize)를 UI 단위 점으로 옮깁니다. */
        static float2 mapImageToUI( const float2& imagePoint, const float2& drawSize, const UIViewport& viewport );
        /** @brief UI 단위 사각형 @p rect 를 그린 그림 위 사각형(그림 왼쪽 위 원점)으로 옮깁니다. */
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
        /** @brief 시간 막대 줄입니다 — "0.25 / 1.00 s". */
        static string makeTimeLabel( float32 time, float32 duration );
    };
} // namespace sw::editor
