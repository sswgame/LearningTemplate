/**
 * @file BodyDataEditModel.h
 * @brief 몸 데이터 파일 하나(`*.bodyshape.xml` 채널 매핑 · `bodyprofiles.xml` 지글 · 주름 · 체액 경로 · `itemvisuals.xml` 구간 별칭)를 원소 열쇠 경로로 고치는 편집 모델입니다(되돌리기 · 저장).
 * @details 편집 대상은 파일 글이다 — 원소를 열쇠(`이름[속성=값]`)의 경로로 짚어 속성을 바꾸고, 없는 원소는 그 열쇠로 만든다(파생 프로필의 지글 묶음을 처음 고치면 그 프로필에
 *          덮어쓰기 원소가 생긴다 — 상속 표가 열쇠로 합친다). 글 전체가 되돌리기 한 단계의 값이다(`EditorDocumentState<string>`) — 몸 데이터 파일은 작다.
 *          주석을 남긴 채 읽어(`XMLDocument::parseKeepingComments`) 저장해도 파일의 설명이 사라지지 않는다. 저장하면 외형 데이터 감시가 다시 읽어 미리보기 몸에 걸린다.
 *          언리얼은 데이터 에셋의 디테일 패널 편집이 트랜잭션 버퍼에 남는 자리다 — 여기서는 데이터가 XML 이라 글이 정본이다. ImGui 와 씬을 모릅니다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Workspace/Undo/EditorDocumentState.h"

#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw::editor
{
    /** @brief 원소 경로의 한 칸입니다 — 이름과(있으면) 가르는 속성 · 값. 속성이 비면 그 이름의 첫 자식입니다. */
    struct BodyDataElementKey
    {
        string _element{};
        string _keyAttribute{};
        string _keyValue{};
    };

    /** @brief 한 번에 바꾸는 속성 하나입니다(`setAttributes`). */
    struct BodyDataAttribute
    {
        const utf8* _pName{ nullptr };
        string      _value{};
    };

    /** @class BodyDataEditModel @brief 파일 머리말 참고. */
    class BodyDataEditModel
    {
    public:
        BodyDataEditModel();

        /** @brief 글 @p text 를 문서로 엽니다(기록을 비우고 "저장된 상태" 로 맞춘다). 읽지 못하면 false 이고 이유는 `getError` 입니다. @p diskPath 는 저장할 곳(비면 저장하지 않는다). */
        [[nodiscard]] bool openText( string_view text, string_view diskPath );
        /** @brief 디스크 파일 @p diskPath 를 엽니다. 같은 파일이 이미 열려 있고 고치지 않았으면 다시 읽지 않습니다. */
        [[nodiscard]] bool openFile( string_view diskPath );
        /** @brief 지금 글을 연 파일에 씁니다. 성공하면 "저장된 상태" 로 맞춥니다. */
        [[nodiscard]] bool save();
        void               close();

        bool          isOpen() const { return _doc.getRoot().isValid(); }
        bool          isDirty() const { return _state.isDirty( _text ); }
        const string& getText() const { return _text; }
        const string& getDiskPath() const { return _diskPath; }
        const string& getError() const { return _error; }
        /** @brief 편집 · 되돌리기마다 오르는 번호입니다(화면이 다시 읽을지 가른다). */
        uint32 getRevision() const { return _revision; }

        /** @brief 경로의 원소가 있으면 true 입니다. */
        bool hasElement( vector_reference<const BodyDataElementKey> path ) const;
        /** @brief 경로 원소의 속성 글입니다. 원소 · 속성이 없으면 빈 글입니다. */
        string readAttribute( vector_reference<const BodyDataElementKey> path, const utf8* pAttribute ) const;
        /**
         * @brief 경로 원소의 속성을 @p value 로 바꿉니다(없는 원소는 열쇠 속성을 단 채 만든다). 이미 같은 값이면 false 이고 기록하지 않습니다.
         * @param bContinuous true 면 같은 원소 · 속성의 연속 편집(끌기)을 되돌리기 한 단계로 합칩니다(`endContinuousEdit` 로 끊는다).
         */
        bool setAttribute( vector_reference<const BodyDataElementKey> path, const utf8* pAttribute, string_view value, bool bContinuous );
        /** @brief 경로 원소의 속성 여럿을 한 되돌리기 단계로 바꿉니다(없는 원소는 만든다 — 새 축 · 새 덮어쓰기). 바뀐 것이 없으면 false 입니다. */
        bool setAttributes( vector_reference<const BodyDataElementKey> path, vector_reference<const BodyDataAttribute> listAttribute );
        /** @brief 경로 원소를 만듭니다(열쇠 속성만 단다). 이미 있으면 false 입니다. 되돌리기 한 단계입니다. */
        bool ensureElement( vector_reference<const BodyDataElementKey> path );
        void endContinuousEdit() { _state.endCoalescing(); }

        [[nodiscard]] bool undo();
        [[nodiscard]] bool redo();
        bool               canUndo() const { return _state.canUndo(); }
        bool               canRedo() const { return _state.canRedo(); }

    private:
        /** @brief 경로 원소입니다. @p bCreate 면 없는 칸을 만들며 걷는다(문서를 고친다 — 부르는 쪽이 기록한다). */
        XMLNode findElement( vector_reference<const BodyDataElementKey> path, bool bCreate ) const;
        /** @brief 편집 전 글 @p before 를 기록하고 문서를 글로 굳힙니다. */
        void commit( const string& before, string_view coalesceKey );
        /** @brief 글 @p text 로 문서를 다시 읽습니다(되돌리기 · 다시 하기). */
        [[nodiscard]] bool reparse( const string& text );
        static string      makeCoalesceKey( vector_reference<const BodyDataElementKey> path, const utf8* pAttribute );

    private:
        XMLDocument                 _doc;
        string                      _text;
        string                      _diskPath;
        string                      _error;
        EditorDocumentState<string> _state;
        uint32                      _revision;
    };
} // namespace sw::editor
