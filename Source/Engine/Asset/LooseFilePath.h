/**
 * @file LooseFilePath.h
 * @brief 리소스 id 를 디스크의 낱개 파일로 푼 결과입니다. 팩을 보지 않는 경로라는 것을 형식이 말합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"

namespace sw
{
    class AssetPaths;

    /**
     * @class LooseFilePath
     * @brief `AssetPaths::findLooseFile` · `findLooseDomainFolder` 가 돌려주는 디스크 경로입니다. 만드는 쪽은 `AssetPaths` 하나입니다.
     * @details 리소스 id 를 디스크 경로로 푼 문자열이 평범한 `string` 이면 `FileUtil` 의 읽기 · 훑기로 그대로 흘러가, 팩으로 쿠킹한 빌드에서
     *          조용히 "없음" 이 됩니다. 그래서 이 형식은 `string` · `string_view` 로 저절로 바뀌지 않고, 디스크 경로가 필요한 쪽(팩 인식 계층 자신,
     *          쿠커, 에디터, 도구)이 `getDiskPath()` 로 **보이게** 꺼냅니다. 런타임에 리소스를 읽는 코드는 `AssetIO` 를 씁니다.
     */
    class LooseFilePath
    {
    public:
        LooseFilePath() = default;

        /** @brief 디스크에 그 파일 · 폴더가 없어 풀지 못했으면 true 입니다. */
        bool isEmpty() const { return _diskPath.empty(); }

        /** @brief 정규화한 절대 디스크 경로입니다. 팩을 보지 않습니다 — 런타임 리소스 읽기에 쓰지 않습니다. */
        const string& getDiskPath() const& { return _diskPath; }
        /** @brief 임시 값에서 꺼낼 때는 값으로 옮겨 줍니다(임시 값의 참조를 들고 있지 않게). */
        string getDiskPath() && { return std::move( _diskPath ); }

    private:
        friend class AssetPaths;

        explicit LooseFilePath( string diskPath )
            : _diskPath{ std::move( diskPath ) }
        {
        }

        string _diskPath{};
    };
} // namespace sw
