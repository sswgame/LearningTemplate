/**
 * @file DiskPath.h
 * @brief 검증을 지난 절대 디스크 경로 가운데 **읽기만** 하는 형식입니다. 쓰기(`FileUtil::write*` · `remove*` · `save*`)는 파생 `AbsolutePath` 만 받는다.
 * @details 리소스 id 를 푼 경로(`AssetPaths::toDiskPath` · `findLooseFile`)가 이 형식이다 — 콘텐츠 파일을 읽기 전용 · 밖에서 바뀜 확인 없이 바로 쓰는 코드가 컴파일되지 않는다.
 *          쓰는 자리는 문(에디터 `AssetWriter`, 엔진 쪽은 `AssetWriteAccess` 의 친구) 하나를 지난다. 언리얼이 저장을 `UPackage::SavePackage` 로, 유니티가 `AssetDatabase` 로 모은 자리다.
 *          로그 · 파일을 쓰지 않는 값 형식이라 낮은 티어(`Core/Path`)에 둔다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"

namespace sw
{
    /**
     * @class DiskPath
     * @brief 검증을 지난 절대 경로 글입니다 — 읽기 · 있는지 묻기 · 나열이 받는다. 만드는 길은 `AbsolutePath`(글에서) 하나이고, 거꾸로 `AbsolutePath` 로 바뀌지 않는다.
     */
    class SW_API DiskPath
    {
    public:
        /** @brief 빈 경로입니다(아직 정하지 않은 값 칸). 그 값을 받은 I/O 는 실패한다. */
        DiskPath() = default;

        /** @brief `AbsolutePath::join` 과 같은 규칙입니다. */
        [[nodiscard]] DiskPath join( string_view relative ) const { return DiskPath{ makeJoinedText( relative ) }; }
        /** @brief `AbsolutePath::getParent` 와 같은 규칙입니다. */
        [[nodiscard]] DiskPath getParent() const { return DiskPath{ makeParentText() }; }
        /** @brief `AbsolutePath::appendSuffix` 와 같은 규칙입니다. */
        [[nodiscard]] DiskPath appendSuffix( string_view suffix ) const { return DiskPath{ makeTextWithSuffix( suffix ) }; }
        /** @brief `AbsolutePath::removeExtension` 과 같은 규칙입니다. */
        [[nodiscard]] DiskPath removeExtension() const { return DiskPath{ makeTextWithoutExtension() }; }

        /** @brief 경로 글입니다(구분자 `/`). */
        [[nodiscard]] const string& str() const { return _path; }
        [[nodiscard]] const utf8*   c_str() const { return _path.c_str(); }
        /** @brief `FileUtil::toDiskPath` 가 거절한 글(빈 글 · 상대 경로)에서 만든 빈 경로면 true 입니다. */
        [[nodiscard]] bool empty() const { return _path.empty(); }

        [[nodiscard]] bool operator==( const DiskPath& other ) const { return _path == other._path; }
        [[nodiscard]] bool operator!=( const DiskPath& other ) const { return _path != other._path; }
        /** @brief 글의 바이트 순입니다(목록 정렬). */
        [[nodiscard]] bool operator<( const DiskPath& other ) const { return _path < other._path; }

    protected:
        explicit DiskPath( string path )
            : _path{ std::move( path ) }
        {
        }

        string makeJoinedText( string_view relative ) const;
        string makeParentText() const;
        string makeTextWithSuffix( string_view suffix ) const;
        string makeTextWithoutExtension() const;

    private:
        friend struct FileUtil; // 읽기 경로 아래를 나열한 항목

        string _path;
    };
} // namespace sw
