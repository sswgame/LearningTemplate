/**
 * @file EditorThemeConfig.h
 * @brief 에디터 테마의 실행 값입니다. 토큰 파일(`EditorThemeTokens`)을 해석한 값이고, 적용과 조회는 `EditorThemeUtil`(GUI)이 하며, 값은 에디터 서브시스템 `EditorTheme` 이 듭니다.
 * @details 색 · 크기의 기본값을 여기 두지 않는다. 원본은 토큰 파일 하나다(두 곳에 두면 한쪽만 고쳐진다). 테마를 읽기 전의 값은 의미가 없다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Editor/Common/Config/EditorComponentIconTable.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorThemeTokens.h"
#include "Editor/Common/EditorColor.h"
#include "Editor/Common/EditorSubsystem.h"

namespace sw
{
    class AssetPaths;
} // namespace sw

namespace sw::editor
{
    /**
     * @brief 해석한 테마 값입니다. 토큰 파일의 표 둘(`_color` · `_metric`)을 키(`Namespace.Key`) → 값으로 듭니다.
     * @details 키는 `hashed_string` 이다 — 코드의 `SW_THEMECOLOR` · `SW_THEMEMETRIC` 이 리터럴을 한 번 intern 해 두고 해시로 찾는다.
     */
    struct EditorThemeConfig
    {
        using ColorMap  = unordered_map<hashed_string, Color, hashed_string::HashFunc>;
        using MetricMap = unordered_map<hashed_string, float32, hashed_string::HashFunc>;

        string _name{};        ///< 토큰 벌 이름(`dark`)
        string _displayName{}; ///< 대화상자에 보이는 이름

        ColorMap  _mapColor{};
        MetricMap _mapMetric{};

        /** @brief @p key 의 색입니다. 표에 없으면 nullptr 입니다. */
        [[nodiscard]] const Color* findColor( const hashed_string& key ) const
        {
            const auto it = _mapColor.find( key );
            return it != _mapColor.end() ? &it->second : nullptr;
        }
        /** @brief @p key 의 수치입니다. 표에 없으면 nullptr 입니다. */
        [[nodiscard]] const float32* findMetric( const hashed_string& key ) const
        {
            const auto it = _mapMetric.find( key );
            return it != _mapMetric.end() ? &it->second : nullptr;
        }

        uint8 _bAccentOverridden{ SW_FALSE }; ///< 사용자가 강조색을 바꿨다(설정에 저장한다)
        uint8 _bRadiusOverridden{ SW_FALSE }; ///< 사용자가 둥글기를 바꿨다(설정에 저장한다)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorTheme
     * @brief 지금 적용된 테마 값을 가진 에디터 서브시스템입니다. 조회 · 교체는 `EditorThemeUtil` 이 `editor::findSubsystem<EditorTheme>()` 로 합니다.
     */
    class EditorTheme final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditorTheme";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        EditorThemeConfig&       getConfig() { return _config; }
        const EditorThemeConfig& getConfig() const { return _config; }
        /** @brief 아이콘 글꼴의 이름 → 코드포인트 표입니다(`EditorFontSetup` 이 글꼴을 세울 때 읽는다, 찾기는 `EditorIconUtil`). */
        EditorIconGlyphTable&       getIconTable() { return _iconTable; }
        const EditorIconGlyphTable& getIconTable() const { return _iconTable; }
        /** @brief 컴포넌트 아이콘 표입니다(아이콘 표와 함께 읽는다, 찾기는 `EditorComponentIcon`). */
        EditorComponentIconTable&       getComponentIconTable() { return _componentIconTable; }
        const EditorComponentIconTable& getComponentIconTable() const { return _componentIconTable; }
        /** @brief 폴더에서 읽어 둔 토큰 벌입니다(`EditorThemeUtil` 이 처음 쓸 때 채운다). */
        vector<EditorThemeConfig>& getAvailable() { return _listAvailable; }

        /** @brief DPI 배율입니다(`EditorThemeUtil::setDPIScale`). 테마는 96 DPI 기준 크기로 기록하고, 적용할 때 이 값을 곱한다. 1 이 기본입니다. */
        float32 getDPIScale() const { return _dpiScale; }
        void    setDPIScale( float32 dpiScale ) { _dpiScale = dpiScale; }

        /** @brief 토큰 파일이 바뀌었다고 알립니다(핫 리로드 — 에셋 테이블의 처리기가 부른다). 다음 UI 프레임이 다시 읽는다. */
        void markTokensChanged() { _bTokensChanged = SW_TRUE; }
        /**
         * @brief 핫 리로드가 바뀐 토큰 파일을 알릴 때 부릅니다(에셋 종류 등록의 처리기). 활성 에디터의 테마에 표시를 남기고, 다음 UI 프레임이 모두 다시 읽고 적용합니다.
         * @return 늘 true 입니다(처리했으니 엔진 캐시 리로드는 하지 않는다).
         */
        [[nodiscard]] static bool importChangedTokenFile( const AssetPaths& assetPaths, string_view relativePath ); ///< 임포터 함수 형식(`AssetSourceImporterFunc`)에 맞춘다 — 경로 상태는 쓰지 않는다
        /** @brief 바뀐 토큰이 있었으면 true 를 돌려주고 표시를 지웁니다. */
        bool consumeTokensChanged()
        {
            const bool bChanged = _bTokensChanged == SW_TRUE;
            _bTokensChanged     = SW_FALSE;
            return bChanged;
        }

        /** @brief 표에 없는 키를 처음 물었으면 true 입니다(오류는 키마다 한 번 남긴다 — 매 프레임 같은 줄로 로그를 덮지 않게). */
        [[nodiscard]] bool markMissingKey( const hashed_string& key )
        {
            for ( const hashed_string& reported : _listMissingKey )
            {
                if ( reported == key )
                    return false;
            }
            _listMissingKey.push_back( key );
            return true;
        }

    private:
        vector<hashed_string>     _listMissingKey{};
        EditorIconGlyphTable      _iconTable{};
        EditorComponentIconTable  _componentIconTable{};
        EditorThemeConfig         _config{};
        vector<EditorThemeConfig> _listAvailable{};
        float32                   _dpiScale{ 1.0f };
        uint8                     _bTokensChanged{ SW_FALSE };
    };
} // namespace sw::editor
