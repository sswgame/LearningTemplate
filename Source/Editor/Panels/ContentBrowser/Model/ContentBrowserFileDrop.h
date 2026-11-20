/**
 * @file ContentBrowserFileDrop.h
 * @brief OS 에서 콘텐츠 브라우저로 놓은 파일을 임포트 요청으로 바꾸는 규칙입니다 — 경로 정규화 · 확장자 · 크기 · 개수를 검사해 받을 것과 거른 것(사유)으로 나눈다.
 * @details 경로는 믿지 않는 바깥 입력이다(창 시스템 · 다른 프로그램이 준다). 판정(`make`)은 디스크를 보지 않는 순수 함수라 단위 테스트가 가짜 파일 정보로 돈다 —
 *          디스크를 읽는 일(`collectDroppedFiles`)은 따로다. 받은 것은 기존 임포트 길(`EditorAssetFileCommands::importFiles` — 새 GUID · 같은 이름이면 새 이름)을 탄다.
 *          언리얼 콘텐츠 브라우저 · 유니티 Project 창의 파일 놓기와 같은 자리이고, 받는 확장자는 파일 대화상자 임포트와 같은 표다(`EditorAssetTypeRegistry::appendImportExtensions`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /** @brief 놓은 파일을 거른 까닭입니다. */
    enum class ContentBrowserFileDropRejection : uint8
    {
        NotAbsolute = 0,  ///< 절대 경로가 아니다(작업 폴더에 따라 다른 파일이 된다)
        ParentReference,  ///< `..` 성분이 있다(정규화 전 경로로 밖을 가리킨다)
        NotRegularFile,   ///< 없거나 폴더 · 장치다
        UnknownExtension, ///< 임포트할 수 있는 종류가 아니다
        TooLarge,         ///< 한 파일 크기 한도(`EditorToolDefaults::_fileDropMaxMegabytes`)를 넘는다
        TooMany,          ///< 한 번에 받는 개수 한도(`_fileDropMaxCount`)를 넘은 뒤의 파일이다
    };

    /** @brief 놓은 파일 하나의 디스크 정보입니다(`collectDroppedFiles` 가 채운다 — 테스트는 손으로 채운다). */
    struct ContentBrowserDroppedFile
    {
        string _path{};                ///< 구분자를 `/` 로 맞춘 경로
        uint64 _byteSize{ 0 };         ///< 일반 파일의 크기
        bool   _bRegularFile{ false }; ///< 있는 일반 파일이다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    /** @brief 받는 규칙입니다. 값은 설정(`EditorToolDefaults`)과 임포트 종류 표에서 온다 — 대신 쓰는 기본값은 없다. */
    struct ContentBrowserFileDropRules
    {
        vector<string> _listExtension{}; ///< 받는 확장자(`.png` — 대소문자 무시)
        uint64         _maxBytes{ 0 };   ///< 한 파일의 최대 바이트
        uint32         _maxCount{ 0 };   ///< 한 번에 받는 최대 파일 수
    };

    /** @brief 거른 파일 하나입니다. */
    struct ContentBrowserRejectedFile
    {
        string                          _path{};
        ContentBrowserFileDropRejection _reason{ ContentBrowserFileDropRejection::NotRegularFile };
    };

    /**
     * @struct ContentBrowserFileDropPlan
     * @brief 놓기 한 번의 판정 결과입니다 — 임포트할 경로와 거른 경로.
     */
    struct ContentBrowserFileDropPlan
    {
        vector<string>                     _listImport{};
        vector<ContentBrowserRejectedFile> _listRejected{};

        /**
         * @brief @p listFile 을 @p rules 로 판정해 @p outPlan 을 채웁니다(비우고 채운다). 디스크를 보지 않는다.
         * @details 검사 순서: 절대 경로 → `..` 성분 → 일반 파일 → 확장자 → 크기 → 개수(받은 수가 한도에 닿으면 뒤는 `TooMany`). 같은 경로가 두 번 오면 한 번만 받는다.
         */
        static void make( const vector<ContentBrowserDroppedFile>& listFile, const ContentBrowserFileDropRules& rules, ContentBrowserFileDropPlan& outPlan );
        /** @brief 경로들의 디스크 정보를 모읍니다(구분자를 `/` 로 맞추고 일반 파일인지 · 크기를 읽는다). 판정은 `make` 가 한다. */
        static void collectDroppedFiles( const vector<string>& listPath, vector<ContentBrowserDroppedFile>& outListFile );
        /** @brief 거른 까닭의 영어 한 줄입니다(로그 · 상태 줄). */
        static const utf8* describeRejection( ContentBrowserFileDropRejection reason );
    };
} // namespace sw::editor
