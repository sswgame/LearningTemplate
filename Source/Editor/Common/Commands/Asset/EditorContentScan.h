/**
 * @file EditorContentScan.h
 * @brief 에디터가 리소스 루트(`Content/`) 아래를 디스크에서 훑는 한 입구입니다 — 다른 프로세스의 시나리오 사본을 뺀 목록을 돌려줍니다.
 * @details 같은 체크아웃의 다른 App · 끝난 실행이 남긴 사본(`scratch/<이름>_p<pid>/…`)이 원본과 같은 파일 이름으로 보이면 Quick Open 이 사본을 집고,
 *          임포터가 남의 사본에 임포트하며, 공유 결과 정리가 남의 사본 파일을 지웠다. 에디터 코드는 리소스 트리를 `FileUtil::collectFiles` · `collectFolders` 로
 *          직접 훑지 않고 이것을 부른다(린트 common-shapes 의 editorContentScan). 자기 사본은 보인다 — 시나리오가 자기 사본을 연다.
 *          쿠킹 · 팩은 scratch 전체를 빼는 `AssetPaths::collectContentFiles` 를 쓴다(규칙이 다르다).
 */
#pragma once
#include "Core/Concurrency/StopToken.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

namespace sw::editor
{
    /**
     * @struct EditorContentScan
     * @brief 리소스 루트 @p contentRoot 아래 폴더를 훑습니다. 반환과 경로 규칙은 `FileUtil::collectFiles` · `collectFolders` 와 같고, 다른 프로세스의 사본만 빠집니다.
     */
    struct EditorContentScan
    {
        /** @brief @p folder(리소스 루트 아래 디스크 경로) 아래 파일입니다. 확장자 @p extension 이 비면 모두입니다. */
        [[nodiscard]] static bool collectFiles( string_view contentRoot, string_view folder, string_view extension, vector<string>& outListFilePath, bool bRecursive,
                                                const StopToken& stopToken = {} );
        /** @brief @p folder(리소스 루트 아래 디스크 경로) 아래 폴더입니다. */
        [[nodiscard]] static bool collectFolders( string_view contentRoot, string_view folder, vector<string>& outListFolder, bool bRecursive );
    };
} // namespace sw::editor
