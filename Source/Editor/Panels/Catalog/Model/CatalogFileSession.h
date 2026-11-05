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

        /** @brief 파일을 엽니다. 깨진 원문도 글 상자로 고치게 열립니다(이유는 문서가 안다). */
        void open( string_view assetID, string_view kindLabel, string_view text );

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

        /**
         * @brief 저장 직전에 부릅니다. 지금 디스크의 글을 넘깁니다.
         * @return 써도 되면 true 입니다. 파일이 연 뒤 밖에서 바뀌었으면 첫 번에는 false 이고 상태 줄이 알립니다. 같은 글로 다시 부르면 true 입니다.
         */
        [[nodiscard]] bool checkDiskBeforeSave( string_view diskText );
        /** @brief 쓰기에 성공한 뒤 부릅니다. 저장 직전 디스크의 글을 "이전 파일" 로 남깁니다. */
        void onSaved();
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
        string          _conflictDiskText;
        string          _status;
        ValidationState _validationState;
        uint8           _bConflictPending : 1; ///< 밖에서 바뀐 글(`_conflictDiskText`)을 알렸다 — 같은 글이면 다음 저장은 덮어쓴다
    };
} // namespace sw::editor
