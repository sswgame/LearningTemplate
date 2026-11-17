/**
 * @file InspectorImportSettings.h
 * @brief 인스펙터의 Import Settings 절 모델입니다 — 고른 원본(모델 · 텍스처)의 해석한 임포트 설정을 값마다 "폴더 규칙 · 서술 예외" 표시와 함께 내고,
 *        고친 값은 그 에셋 서술(`.meta` 의 `_listImportOverride`)에 차이로 써 그 원본만 다시 임포트합니다. ImGui 를 모릅니다(MVVM 계약).
 * @details 유니티 `.meta` 임포터 블록을 인스펙터가 고치는 것, 언리얼 `UAssetImportData` 를 Details 가 보이는 것과 같은 자리다(설계 M3). 기본값은 폴더 규칙이 주고
 *          서술은 차이만 든다(우선순위 프리셋 < 규칙 < 서술). 검증은 임포터의 해석(`resolveRule`)이 한다 — 해석이 지는 예외는 쓰지 않는다(폴백 없음).
 *          서술 쓰기는 되돌리기 한 단계다(서술 글 전 · 후, 데이터 단계 `restoreMetaText` — 되돌려도 다시 임포트한다). 씬은 dirty 가 아니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

namespace sw
{
    struct AssetImportOverride;
    struct TypeInfo;
} // namespace sw

namespace sw::editor
{
    /** @brief 임포트 설정 한 줄입니다 — 칸 이름(규칙 파일 · 서술과 같은 키), 해석한 값 글, 서술 예외인지. */
    struct InspectorImportRow
    {
        string _key;            ///< 규칙 파일 키("generateMips" · "animationCodec")
        string _valueText;      ///< 해석한 값(예외면 서술의 JSON 값 글, 아니면 규칙 값의 표시 글)
        uint8  _bOverride{ 0 }; ///< 서술의 예외가 정한 값이다
    };

    /**
     * @class InspectorImportSettings
     * @brief 고른 원본 하나의 임포트 설정입니다. 인스펙터 패널이 하나 들고 프레임마다 `refresh` 한다 — 에셋 · 서술 글이 그대로면 다시 읽지 않는다.
     */
    class SW_EDITOR_API InspectorImportSettings
    {
    public:
        /**
         * @brief 리소스 id @p assetID 가 서술을 가진 모델(`models_raw/`) · 텍스처(`textures_raw/`) 원본이면 설정을 모읍니다. 아니면 false 이고 비웁니다.
         * @details 같은 에셋이고 서술 글이 같으면 다시 해석하지 않는다(설정 파일은 에셋을 바꿀 때 다시 읽는다).
         */
        bool                              refresh( string_view assetID );
        const string&                     getAssetID() const { return _assetID; }
        const string&                     getRuleName() const { return _ruleName; } ///< 맞은 폴더 규칙 이름. 비면 기본값(맞는 규칙 없음)
        const string&                     getError() const { return _error; }       ///< 해석 · 쓰기 오류(영어 한 문장). 비면 없음
        const vector<InspectorImportRow>& getRows() const { return _listRow; }

        /**
         * @brief 칸 @p key 의 예외를 JSON 값 글 @p valueText(`false` · `"raw"` · `0.5`)로 씁니다. 해석이 지면 쓰지 않고 `getError` 에 이유를 두고 false 입니다.
         * @details 서술을 쓰고 되돌리기 한 단계를 남기고 그 원본을 다시 임포트한다. 값이 규칙과 같아도 예외로 남는다(지우기는 `clearOverride`).
         */
        [[nodiscard]] bool setOverride( string_view key, string_view valueText );
        /** @brief 칸 @p key 의 예외를 지워 폴더 규칙 값으로 돌아갑니다. 예외가 없으면 할 일이 없고 true 입니다. */
        [[nodiscard]] bool clearOverride( string_view key );

        /** @brief 되돌리기 · 다시 하기의 데이터 단계입니다 — 원본 @p assetID 의 서술을 글 @p metaText 로 되돌리고 그 원본을 다시 임포트합니다. */
        static void restoreMetaText( string assetID, string metaText );

    private:
        /** @brief 원본 종류입니다(규칙 파일이 다르다). */
        enum class SourceKind : uint8
        {
            None,
            Model,
            Texture,
        };

        /** @brief @p assetID 의 원본 종류입니다(원본 폴더 마디로 가른다). */
        static SourceKind findSourceKind( string_view assetID );
        /** @brief 지금 서술 글과 예외 목록으로 규칙을 해석해 행을 다시 짓습니다. */
        void rebuild( const vector<AssetImportOverride>& listOverride );
        /** @brief 규칙 값 @p pRule(타입 @p ruleType)의 칸마다 행을 더합니다. 예외 칸은 서술의 글을 보인다. */
        void appendRows( const TypeInfo& ruleType, const void* pRule, const vector<AssetImportOverride>& listOverride );
        /** @brief 예외 목록 @p listOverride 를 해석해 보고, 맞으면 서술을 그것으로 써 되돌리기에 남깁니다. */
        [[nodiscard]] bool writeOverrides( const vector<AssetImportOverride>& listOverride, string_view undoLabel );
        /** @brief 예외 목록 @p listOverride 의 해석 오류입니다. 없으면 빈 글입니다. */
        string findResolveError( const vector<AssetImportOverride>& listOverride ) const;

    private:
        string                     _assetID{};
        string                     _metaText{}; ///< 마지막으로 해석한 서술 글(같으면 다시 해석하지 않는다)
        string                     _ruleName{};
        string                     _error{};
        vector<InspectorImportRow> _listRow{};
        SourceKind                 _kind{ SourceKind::None };
    };
} // namespace sw::editor
