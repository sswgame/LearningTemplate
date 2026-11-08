/**
 * @file EditorFontSetup.h
 * @brief 에디터 ImGui 폰트(본문 · 한글 · 아이콘)를 구성합니다.
 *
 * @details 폰트 구성만 ImGui 를 필요로 하므로 UI 없는 도구와 따로 둡니다. 프로젝트 경로 해석이나 애셋 종류 판별 같은 UI 와
 *          무관한 유틸이 UI 라이브러리를 끌고 다니지 않아야 단위 테스트가 그 도구를 링크할 수 있습니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    struct EditorIconGlyphTable;
    struct EditorToolDefaults;

    /**
     * @class EditorFontSetup
     * @brief 에디터 ImGui 폰트 구성과 그 결과(고정폭 글꼴 · em 비)입니다. ImGui 컨텍스트의 주인(`ImGuiEditor`)이 멤버로 든다.
     * @details 결과는 ImGui 컨텍스트에 건다(`ImGuiIO::UserData` — 글꼴이 그 컨텍스트의 아틀라스에 있다). 예전 함수 정적(감춘 싱글턴)은 컨텍스트를 다시 세워도 앞 글꼴 주소가 남았다.
     *          ImGui 백엔드가 자기를 `BackendPlatformUserData` 에 거는 것과 같은 모양이다 — 프로세스 정적 포인터를 두지 않는다.
     */
    class EditorFontSetup
    {
    public:
        EditorFontSetup();
        /** @brief 지금 ImGui 컨텍스트에 이것이 걸려 있으면 뗍니다. */
        ~EditorFontSetup();
        EditorFontSetup( const EditorFontSetup& )            = delete;
        EditorFontSetup& operator=( const EditorFontSetup& ) = delete;

        /**
         * @brief @p toolDefaults 에 적힌 후보로 ImGui 본문 · 한글 · 아이콘 폰트를 구성합니다. 아이콘 글리프 범위는 @p iconTable(읽은 표)이 준다.
         * @details 에디터 범위가 서기 전(ImGui 컨텍스트 기동 단계)에 불리므로 도구 시드를 범위에서 찾지 않고 인자로 받는다.
         * @return 테마 글자 크기를 읽고 본문 · 한글 · 아이콘 글꼴을 모두 찾았으면 true. 하나라도 없으면 무엇이 없는지 오류로 남기고 false 다 —
         *         ImGui 기본 글꼴로 대신하지 않는다(글꼴 후보가 데이터이므로 고칠 곳은 editortooldefaults.json 이다).
         */
        [[nodiscard]] bool apply( const EditorToolDefaults& toolDefaults, const EditorIconGlyphTable& iconTable );
        /**
         * @brief 테마 글자 크기(em px — CSS 와 같은 뜻)를 본문 글꼴의 ImGui 크기로 바꿉니다.
         * @details ImGui 의 FreeType 로더는 "어센더 - 디센더 = 크기" 로 맞춘다(`FT_SIZE_REQUEST_TYPE_REAL_DIM`). 그 비가 글꼴마다 달라(Segoe UI 1.33,
         *          Consolas 1.0) 같은 14 가 em 10.5 px 와 14 px 로 그려졌다 — 토큰을 em 으로 두고 글꼴마다 그 비를 곱한다. `apply` 전이면 단언한다.
         */
        static float32 toBaseFontSize( float32 emSize );
        /**
         * @brief 고정폭 글꼴(한글 · 아이콘 병합)을 본문과 같은 em 크기로 겁니다 — 로그 · 콘솔 줄처럼 열이 맞아야 읽히는 글에만 쓴다. `popMonoFont` 와 짝이다.
         * @details 앱 화면 규칙: 글꼴은 하나, 둘째 글꼴은 한 가지 일만(언리얼 · VS Code 도 UI 는 산세리프, 출력 로그만 고정폭).
         */
        static void pushMonoFont();
        static void popMonoFont();

    private:
        /** @brief 지금 ImGui 컨텍스트에 건 구성입니다. `apply` 전이면 단언한다. */
        static const EditorFontSetup& getActive();

    private:
        uint32  _monoFontIndex; ///< 고정폭 글꼴의 아틀라스 번호(`ImFontAtlas::Fonts` — 글꼴은 아틀라스가 소유한다)
        float32 _baseEmRatio;   ///< 본문 글꼴의 (어센더 - 디센더) / em
        float32 _monoEmRatio;   ///< 고정폭 글꼴의 (어센더 - 디센더) / em
    };
} // namespace sw::editor
