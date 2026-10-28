/**
 * @file EditableAssetPath.h
 * @brief 사용자가 패널의 텍스트 칸에서 고치는 리소스 경로입니다 — 고치는 중인 글과 확정한 경로를 형식으로 나눈다.
 * @details `DataPath` 는 데이터 읽기만 값을 넣는 칸이라, 사용자가 글자마다 바꾸는 현재 값을 담을 수 없다. 그렇다고 `string` 하나로 두면
 *          글자 하나 칠 때마다 반쯤 쓴 경로가 열기 · 저장에 쓰이고(UI 미리보기가 글자마다 문서를 다시 만들고 실패를 쌓았다), 빈 문자열이
 *          "아직 없음" 과 "잘못 씀" 을 함께 뜻했다. 이 형식은 둘을 나눈다.
 *          - **확정 경로**(`get`)는 처음부터 값이 있다. 처음 값은 데이터(`DataPath`)에서만 받는다 — 코드 리터럴로 만들 수 없다.
 *          - **고치는 글**(`drawField` 의 필드)는 사용자가 필드를 떠나거나 Enter 를 누를 때 `commit` 으로 검사해 확정 경로가 된다.
 *            리소스 id 가 아니면(빈 글, OS 절대 경로) 거절하고 확정 경로는 그대로 둔다 — 빈 값 · 옛 값으로 조용히 바꾸지 않고 알린다.
 *          읽기 · 쓰기는 확정 경로만 쓴다. 사용자 설정의 디스크 경로(실행 파일 등)는 리소스 id 가 아니라 이 형식을 쓰지 않는다.
 *
 *          쓰는 법:
 *            EditableAssetPath _documentPath{ defaults._uiPreviewDocument };   // 처음 값은 데이터에서
 *            if ( _documentPath.drawField( "##doc", 320.0f ) ) reopen();           // 확정됐을 때만
 *            load( _documentPath.get() );
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class DataPath;
} // namespace sw

namespace sw::editor
{
    /**
     * @class EditableAssetPath
     * @brief 확정한 리소스 경로(도메인으로 시작하는 리소스 id)와 텍스트 칸에서 고치는 글을 함께 갖습니다.
     */
    class EditableAssetPath
    {
    public:
        /** @brief 데이터가 준 처음 경로로 만듭니다. 읽지 않은 칸이면 Debug 에서 어서션이 난다(`DataPath::get`). */
        explicit EditableAssetPath( const DataPath& initial );
        EditableAssetPath( const utf8* )   = delete;
        EditableAssetPath( const string& ) = delete;
        EditableAssetPath( string_view )   = delete;

        /**
         * @brief 고치는 글의 텍스트 칸을 그립니다. 사용자가 칸을 떠나거나 Enter 를 눌러 글이 확정 경로로 바뀌었을 때만 true 입니다.
         * @details 확정을 거절하면(`commit`) 글은 남겨 사용자가 고치게 하고 로그로 이유를 알린다. 칸을 고치지 않는 동안은 확정 경로를 보인다.
         * @param width 0 이면 ImGui 기본 폭, 음수면 남은 폭 전부입니다(`EditorWidgets::drawTextField` 와 같다).
         */
        [[nodiscard]] bool drawField( const utf8* pID, float32 width );
        /**
         * @brief 글을 검사해 확정 경로로 바꿉니다(다른 곳 — 콘텐츠 브라우저 포커스 — 에서 온 경로도 이것을 거친다).
         * @return 리소스 id 로 받아들여 확정 경로가 바뀌었으면 true 입니다. 같은 경로이거나 거절했으면 false 이고 확정 경로는 그대로다.
         */
        [[nodiscard]] bool commit( string_view text );

        /** @brief 확정 경로입니다. 늘 값이 있다. */
        const string& get() const { return _committed; }
        const utf8*   c_str() const { return _committed.c_str(); }
        /** @brief 확정 경로와 같은지 봅니다. */
        bool operator==( string_view other ) const { return string_view( _committed ) == other; }
        bool operator!=( string_view other ) const { return string_view( _committed ) != other; }

        /** @brief @p text 가 확정할 수 있는 리소스 id 인지 봅니다(앞뒤 공백을 뺀 글이 비지 않고 OS 절대 경로가 아니다). */
        [[nodiscard]] static bool isAssetID( string_view text );

    private:
        string _committed; ///< 읽기 · 쓰기가 쓰는 경로
        string _draft;     ///< 텍스트 칸이 고치는 글. 칸을 고치지 않는 동안은 확정 경로와 같다
    };
} // namespace sw::editor
