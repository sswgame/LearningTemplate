/**
 * @file EditorDiskBaseline.h
 * @brief 연 파일의 디스크 내용을 적어 두었다가, 저장이 그 사이 밖에서 바뀐 파일을 말없이 덮지 않게 하는 기준입니다(씬 · 문서 패널 공용).
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /**
     * @class EditorDiskBaseline
     * @brief 읽은(쓴) 때의 파일 내용 해시입니다. 저장 직전에 지금 디스크와 견준다.
     * @details 밖에서 바뀐 파일(다른 도구 · git 체크아웃 · 다른 편집기)을 열린 문서가 저장하면 그 변경이 사라졌다. 언리얼은 다시 읽을지 묻고, 유니티는 다시 읽으며, Godot 은
     *          "디스크의 파일이 바뀌었다" 를 묻는다 — 여기서는 첫 저장을 거절하고 알리며, 그 뒤 같은 디스크 내용에 대한 저장은 고른 것으로 보고 덮어쓴다(카탈로그 창과 같은 규칙).
     *          기준이 없는 파일(아직 읽지 않은 새 자리 · 지워진 파일)은 막지 않는다.
     */
    class SW_EDITOR_API EditorDiskBaseline
    {
    public:
        EditorDiskBaseline();

        /** @brief @p listPath 의 지금 디스크 내용을 기준으로 적습니다(읽은 직후 · 쓴 직후). 없는 파일의 기준은 지웁니다. */
        void capture( span<const AbsolutePath> listPath );
        /**
         * @brief @p listPath 에 쓰기 직전에 묻습니다. 기준과 다른 파일이 있으면 알림 · 경고 로그를 남기고 false 입니다(아무것도 쓰지 않는다).
         * @return 써도 되면 true 입니다. 거절한 내용 그대로 다시 물으면 true 입니다.
         */
        [[nodiscard]] bool validateOverwrite( span<const AbsolutePath> listPath );

    private:
        struct Entry
        {
            string _path;
            uint64 _hash{ 0 };        ///< 읽은(쓴) 때의 내용
            uint64 _refusedHash{ 0 }; ///< 거절하며 알린 디스크 내용. 같은 내용이면 다음 저장은 덮어쓴다
        };

        Entry* find( string_view path );

    private:
        vector<Entry> _listEntry;
    };
} // namespace sw::editor
