/**
 * @file CatalogInheritanceView.h
 * @brief 카탈로그 창이 고치는 원문의 항목 상속을 펼쳐 "이 칸을 지우면 무슨 값인가" 에 답합니다(ImGui 없음).
 * @details 창은 언제나 원문을 고치므로 저장은 적은 칸만 쓴다. 이 보기는 물려받은 값을 보여 주고, 부모와 같은 값을 적으면 그 칸을 지우게 한다(`CatalogDocument::setCell`).
 *          언리얼 Details 패널이 아키타입과 같은 칸을 표시하고 다른 칸만 저장하는 것과 같은 일이다. 합치기는 엔진의 `DataCatalogInheritance` 하나이며 여기서 다시 적지 않는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "Editor/Panels/Catalog/Model/CatalogChildSection.h"
#include "Editor/Panels/Catalog/Model/CatalogXMLText.h"

#include "Engine/Serialization/Inherit/DataCatalogInheritance.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw::editor
{
    /** @class CatalogInheritanceView @brief 파일 머리말을 참고하세요. */
    class CatalogInheritanceView
    {
    public:
        CatalogInheritanceView();

        /** @brief 부모 파일을 읽는 곳입니다. 없으면 보기가 꺼져 있습니다. 소유하지 않습니다. */
        void setSource( IDataInheritanceSource* pSource ) { _pSource = pSource; }
        /** @brief 원문이 바뀔 때마다 다시 펼칩니다. 규칙 표(`DataCatalogInheritance::kTableAssetID`)에 없는 루트이면 꺼집니다. */
        void rebuild( string_view text, const CatalogXMLText& parsed );

        /** @brief 규칙 표에 있는 카탈로그를 보고 있으면 true 입니다. */
        bool isActive() const { return _bActive == SW_TRUE; }
        /** @brief 펼치지 못했으면 이유입니다(자세한 오류는 로그). 고치는 중에 잠시 깨질 수 있어 편집을 막지 않습니다. */
        const string& getError() const { return _error; }
        /** @brief 항목 종류 @p kind 의 id 칸입니다. 규칙에 없는 종류이면 nullptr 입니다. */
        const string* findKeyAttribute( string_view kind ) const;
        /** @brief 행 @p row 가 칸 @p column 을 적지 않으면 물려받을 값입니다. 없으면 nullptr 이고, 포인터는 다음 `rebuild` 까지 유효합니다. */
        const utf8* findInherited( const CatalogXMLElement& row, string_view column ) const;
        /** @brief 행이 물려받는 칸 이름을 목록에 더합니다(겹치면 더하지 않습니다). 표의 열이 됩니다. */
        void collectInheritedColumns( const CatalogXMLElement& row, vector<string>& inoutListColumn ) const;
        /**
         * @brief 행의 자식 원소를 규칙대로 펼친 표들입니다. 먼저 출력 목록을 비웁니다. 펼치지 못했거나 규칙에 없는 행이면 비어 있습니다.
         * @details 규칙의 칸은 원소가 없어도 구역이 선다(더할 수 있어야 한다). 줄의 순서는 합친 결과의 순서이고, 합친 결과에서 빠진 `_op="Remove"` 원소는 그 구역 끝에 온다.
         */
        void collectChildSections( const CatalogXMLText& parsed, int32 rowIndex, vector<CatalogChildSection>& outListSection ) const;

        /** @brief 행의 부모 항목입니다(같은 파일 · 다른 파일 · 파일 상속의 같은 id). 없으면 무효입니다. */
        XMLNode findParentEntry( const CatalogXMLElement& row ) const;
        /** @brief 행을 합친 결과입니다. 펼치지 못했으면 무효입니다. */
        XMLNode findFlatEntry( const CatalogXMLElement& row ) const;
        /** @brief 행 아래 @p path 원소의 규칙입니다. 규칙에 없는 칸이면(통째로 바뀐다) nullptr 입니다. */
        const DataInheritanceSchema* findChildSchema( const CatalogXMLElement& row, const CatalogChildPath& path ) const;

        /** @brief 원문의 원소 @p ownerIndex 아래에서 @p step 원소의 번호입니다. 없으면 비어 있고, 담는 원소를 적지 않았으면(@p ownerIndex 가 비면) 그 아래도 없습니다. */
        static optional<int32> findWrittenChild( const CatalogXMLText& parsed, optional<int32> ownerIndex, const CatalogChildStep& step );
        /** @brief @p start 에서 @p path 를 따라 내려간 원소입니다. 없으면 무효입니다. */
        static XMLNode walkPath( const XMLNode& start, const CatalogChildPath& path );

    private:
        struct ParentFile
        {
            string      _assetID;
            XMLDocument _doc;
        };

        void addParentFile( string_view assetID );

    private:
        DataInheritanceSchema   _schema;
        XMLDocument             _flatDoc;        ///< 지금 원문을 펼친 문서
        vector<ParentFile>      _listParentFile; ///< 다른 파일의 부모를 펼친 문서
        string                  _rootBase;       ///< 루트의 `_base`
        string                  _error;
        IDataInheritanceSource* _pSource;
        uint8                   _bActive : 1;
    };
} // namespace sw::editor
