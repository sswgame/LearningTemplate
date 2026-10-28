/**
 * @file CatalogCurveModel.h
 * @brief 카탈로그 문서 안에서 곡선(`FloatCurve`: `<_listKey>` 아래 `<FloatCurveKey _time _value _interpolation/>`)과 색 그래디언트(`_red` `_green` `_blue` `_alpha` 네 곡선)를 찾고, 키를 더하는 ImGui 없는 도우미입니다.
 * @details 카탈로그 편집기의 표는 루트 바로 아래 원소의 속성만 칸으로 보여 주므로 VFX 효과 파일(`*.vfx.xml`)처럼 곡선이 깊이 중첩된 파일의 곡선은 표로 못 고쳤다.
 *          이 도우미는 한 원소 아래의 모든 곡선을 모아 "곡선 편집" 창(`CatalogTableEditor`)에 넘긴다. 키 값을 고치는 일은 기존 셀 편집(`CatalogDocument::setCell`)과 같은 길이고 원문의 주석 · 들여쓰기가 남는다.
 *          곡선인지는 **이름이 아니라 모양**으로 판정한다(`_listKey` 자식이 있는 원소) - VFX 의 `_sizeOverLife` 도, 앞으로 곡선을 쓰는 다른 데이터 종류도 같은 길을 쓴다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Panels/Catalog/Model/CatalogXMLText.h"

#include "Engine/Curve/FloatCurve.h"

namespace sw::editor
{
    class CatalogDocument;

    /** @brief 곡선 키 하나입니다. 값을 못 읽은 키는 `_bValid` 가 false 입니다(글 상자로 고치게 둔다). */
    struct CatalogCurveKey
    {
        int32   _elementIndex{ -1 };
        float32 _time{ 0.0f };
        float32 _value{ 0.0f };
        string  _interpolation{};
        bool    _bValid{ false };
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 곡선 하나입니다. */
    struct CatalogCurve
    {
        string                  _label{};   ///< 소유 원소 아래 경로(`VFXEmitterDef(Smoke)/_colorOverLife/_red`)
        string                  _channel{}; ///< 곡선 원소의 이름(`_red`)
        int32                   _elementIndex{ -1 };
        int32                   _parentIndex{ -1 };
        vector<CatalogCurveKey> _listKey{};
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 같은 부모 아래 네 채널이 다 있는 곡선 묶음입니다(`_curves` 의 인덱스). */
    struct CatalogGradient
    {
        string _label{};
        int32  _red{ -1 };
        int32  _green{ -1 };
        int32  _blue{ -1 };
        int32  _alpha{ -1 };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct CatalogCurveModel
     * @brief 곡선 찾기 · 표본 · 키 더하기.
     */
    struct CatalogCurveModel
    {
        /** @brief 곡선 키 원소의 이름입니다. */
        static constexpr const utf8* kKeyElementName = "FloatCurveKey";
        /** @brief 키 목록 원소의 이름입니다(곡선 원소의 자식). */
        static constexpr const utf8* kKeyListName = "_listKey";

        /** @brief @p ownerElement 아래(자기 포함 안 함)의 곡선을 문서 순서로 모읍니다. 먼저 출력 목록을 비웁니다. */
        static void collectCurves( const CatalogXMLText& parsed, int32 ownerElement, vector<CatalogCurve>& outListCurve );
        /** @brief 곡선 목록에서 네 채널이 다 있는 묶음을 모읍니다. 먼저 출력 목록을 비웁니다. */
        static void collectGradients( const vector<CatalogCurve>& listCurve, vector<CatalogGradient>& outListGradient );
        /** @brief 읽은 키로 엔진 곡선을 짓습니다. 읽지 못한 키는 건너뜁니다. 정렬과 자동 접선을 마친 곡선입니다. */
        static FloatCurve makeFloatCurve( const CatalogCurve& curve );
        /** @brief 곡선의 값을 @p sampleCount 개 표본으로 냅니다(시각 0 ~ @p endTime). 키가 없으면 false 입니다. */
        [[nodiscard]] static bool sample( const CatalogCurve& curve, float32 endTime, uint32 sampleCount, vector<float32>& outListValue );
        /** @brief 곡선의 마지막 키 시각입니다(1 보다 작으면 1). 표본 구간을 정한다. */
        static float32 computeEndTime( const CatalogCurve& curve );
        /**
         * @brief 곡선의 끝에 키를 더합니다. `_listKey` 가 없으면(`<_red/>`) 먼저 만듭니다. 새 키는 마지막 키 뒤 0.1 에 같은 값(키가 없으면 시각 0 값 1)이고 보간은 `Linear` 입니다.
         * @return 더했으면 true 입니다. 실패하면 아무것도 바뀌지 않는 것이 아니라(두 단계 중 첫 단계는 남을 수 있다) 문서의 `getError` 에 이유가 있습니다.
         */
        [[nodiscard]] static bool addKey( CatalogDocument& inoutDocument, int32 curveElementIndex );
    };
} // namespace sw::editor
