/**
 * @file CatalogFileSession.h
 * @brief 카탈로그 창에 열린 파일 하나의 상태입니다 — 문서, 저장 직전 글, 디스크 충돌, 검증 결과, 상태 줄(ImGui · 디스크 없음).
 * @details 창은 디스크를 읽고 쓰고 로더를 돌린 뒤 결과를 여기에 알린다. 그래서 "무엇이 언제 바뀌는가" 를 화면 없이 테스트한다.
 *          저장은 언리얼 · Godot 처럼 파일이 밖에서 바뀌었으면 말없이 덮어쓰지 않는다: 첫 저장은 알리고 멈추고, 같은 글에서 다시 저장하면 덮어쓴다(되돌리기 단추에 밖의 글이 남는다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Panels/Catalog/Model/CatalogDocument.h"

#include "Engine/Serialization/Inherit/DataInheritance.h"

// 카탈로그 창 왼쪽 목록입니다 — 파일을 종류 · 리소스 id 순으로 두고, 거르개에 맞는 파일을 종류별로 묶습니다(ImGui 없음).
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

namespace sw::editor
{
    /** @class CatalogFileSession @brief 파일 머리말을 참고하세요. */
    class CatalogFileSession
    {
    public:
        /** @brief 마지막 검증의 결과입니다. */
        enum class ValidationState : uint8
        {
            None = 0,
            Passed,
            Failed
        };

        CatalogFileSession();

        /**
         * @brief 파일을 엽니다. 깨진 원문도 글 상자로 고치게 열립니다(이유는 문서가 안다).
         * @param pInheritanceSource 항목 상속의 부모 파일을 읽는 곳입니다. 없으면 원문만 봅니다. 소유하지 않습니다.
         */
        void open( string_view assetID, string_view kindLabel, string_view text, DataInheritanceSource* pInheritanceSource = nullptr );

        bool                   hasFile() const { return _assetID.empty() == false; }
        const string&          getAssetID() const { return _assetID; }
        const string&          getKindLabel() const { return _kindLabel; }
        const CatalogDocument& getDocument() const { return _document; }
        CatalogDocument&       editDocument() { return _document; }
        const string&          getStatus() const { return _status; }
        void                   setStatus( string_view status ) { _status = string( status ); }
        void                   clearStatus() { _status.clear(); }
        ValidationState        getValidationState() const { return _validationState; }
        const vector<string>&  getWarnings() const { return _listWarning; }

        /** @brief 쓰기에 성공한 뒤 부릅니다. 저장 직전 디스크의 글 @p replacedText 를 "이전 파일" 로 남깁니다. */
        void onSaved( string replacedText );
        /** @brief 저장 직전 글이 지금 디스크와 달라 되돌릴 것이 있으면 true 입니다. */
        bool          canRestorePrevious() const { return _previousText != _document.getSavedText(); }
        const string& getPreviousText() const { return _previousText; }
        /** @brief "이전 파일" 을 디스크에 되쓴 뒤 부릅니다. 고치던 글은 Undo 로 돌아옵니다. */
        void onRestored();

        /** @brief 검증을 시작할 때 앞 결과를 비웁니다. */
        void clearValidation();
        /** @brief 로더를 돌린 결과입니다. 읽혔고 경고가 없으면 통과입니다. 고치던 글이 있으면 상태 줄이 "저장된 파일을 봤다" 고 알립니다. */
        void onValidated( bool bLoaded, vector<string>&& listWarning );

    private:
        CatalogDocument _document;
        vector<string>  _listWarning;
        string          _assetID;
        string          _kindLabel;
        string          _previousText;
        string          _status;
        ValidationState _validationState;
    };
} // namespace sw::editor
