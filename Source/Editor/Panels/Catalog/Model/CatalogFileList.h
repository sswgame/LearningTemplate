/**
 * @file CatalogFileList.h
 * @brief 카탈로그 창 왼쪽 목록입니다 — 파일을 종류 · 리소스 id 순으로 두고, 거르개에 맞는 파일을 종류별로 묶습니다(ImGui 없음).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief 목록의 파일 하나입니다. */
    struct CatalogFileRow
    {
        string _assetID{};
        string _kindLabel{};

        /** @brief 종류 이름, 같으면 리소스 id 순입니다. */
        static bool isBefore( const CatalogFileRow& lhs, const CatalogFileRow& rhs );
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 거르개에 맞는 파일이 있는 종류 하나입니다. */
    struct CatalogFileGroup
    {
        string         _kindLabel{};
        vector<uint32> _listRow{}; ///< 맞는 파일의 번호(`CatalogFileList::getRows`)
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @class CatalogFileList @brief 파일 머리말을 참고하세요. */
    class CatalogFileList
    {
    public:
        CatalogFileList();

        /** @brief 목록을 통째로 바꿉니다. 종류 · 리소스 id 순으로 세웁니다. */
        void                          setRows( vector<CatalogFileRow>&& listRow );
        const vector<CatalogFileRow>& getRows() const { return _listRow; }
        /**
         * @brief 거르개(리소스 id · 종류 이름, 대소문자 무시)에 맞는 파일을 종류별로 모읍니다. 맞는 파일이 없는 종류는 넣지 않습니다. 먼저 출력 목록을 비웁니다.
         * @return 맞는 파일 수입니다.
         */
        uint32 collectGroups( string_view filter, vector<CatalogFileGroup>& outListGroup ) const;

    private:
        vector<CatalogFileRow> _listRow;
    };
} // namespace sw::editor
