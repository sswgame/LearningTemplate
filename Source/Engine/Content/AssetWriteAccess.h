/**
 * @file AssetWriteAccess.h
 * @brief 리소스 id 를 푼 읽기 경로(`DiskPath`)를 쓰는 경로(`AbsolutePath`)로 여는 허가입니다. 아래 친구만 열 수 있다 — 이 목록이 콘텐츠를 쓰는 문의 전부다.
 * @details 에디터 코드는 모두 `editor::AssetWriter` 를 지난다(읽기 전용 · 밖에서 바뀜 확인). 나머지는 에디터 아래 층이 스스로 쓰는 자리다: 활성 씬(에디터 명령이 확인한 뒤 부른다) ·
 *          골든 기록 · 머티리얼 그래프 생성물. 파일을 옮기고 지우는 명령(`EditorAssetFileCommands`)은 묶음 확인을 스스로 한다. 새 문을 더하기 전에 `AssetWriter` 로 쓸 수 있는지 먼저 본다.
 */
#pragma once
#include "Core/File/FileUtil.h"
#include "Core/Path/AbsolutePath.h"

namespace sw::editor
{
    class AssetWriter;
    struct EditorAssetFileCommands;
} // namespace sw::editor

namespace sw
{
    class AutomationEvaluator;
    class SceneManager;
    struct MaterialGraphGeneratedPaths;

    /**
     * @class AssetWriteAccess
     * @brief 콘텐츠를 쓰는 문의 목록입니다(파일 머리말).
     */
    class AssetWriteAccess
    {
    private:
        friend class editor::AssetWriter;
        friend struct editor::EditorAssetFileCommands;
        friend class AutomationEvaluator;
        friend class SceneManager;
        friend struct MaterialGraphGeneratedPaths;

        /** @brief @p path 에 쓸 경로입니다. */
        [[nodiscard]] static AbsolutePath toWritablePath( const DiskPath& path ) { return FileUtil::toDiskPath( path.str() ); }
    };
} // namespace sw
