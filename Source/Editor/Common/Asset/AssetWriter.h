/**
 * @file AssetWriter.h
 * @brief 에디터가 콘텐츠 · 설정 파일을 쓰는 문 하나입니다 — 읽기 전용(버전 관리가 잠그지 않은 파일)과 밖에서 바뀐 파일을 확인한 뒤에만 쓰는 경로를 연다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /** @brief 확인을 거치는 동작입니다 — 알림 제목이 고른다. */
    enum class AssetWriteKind : uint8
    {
        Save,   ///< 씬 · 문서 · 명령의 저장
        Move,   ///< 옮기기 · 이름 바꾸기(옮기는 파일과 고칠 참조 파일)
        Delete, ///< 휴지통 · 삭제
    };

    /**
     * @class AssetWriter
     * @brief 연(읽은) 파일의 디스크 내용을 기준으로 들고, 쓸 때 그 기준과 읽기 전용을 확인합니다. 문서 패널이 하나 쥐고(`EditorDocumentPanelBase`), 읽고 고쳐 쓰는 명령은 지역 값으로 쓴다.
     * @details 리소스 id 를 푼 경로(`DiskPath`)는 쓰는 함수(`FileUtil::write*` · `saveToFile`)에 들어가지 않고 이 문이 `AbsolutePath` 로 연다(`AssetWriteAccess`) — 확인을 빠뜨린 쓰기가 컴파일되지 않는다.
     *          예전에는 읽기 전용 확인 · 디스크 바뀜 검사(세 벌) · 폴더 만들기를 쓰는 자리마다 골라 불렀고, 창 밖 명령 열둘은 아무것도 보지 않았다.
     *          **읽기 전용.** git LFS lockable 은 잠근 사람 사본만 쓰기 가능으로 두므로 남이 잠근 파일은 이 사본에서 읽기 전용이다. 자동 체크아웃은 두지 않는다(`EditorSourceControl` 머리말).
     *          **밖에서 바뀜.** 기준과 다른 파일의 첫 저장은 거절하고 알리며, 그 뒤 같은 디스크 내용에 대한 저장은 고른 것으로 보고 덮는다. 기준이 없는 파일(새 자리)은 막지 않는다.
     *          언리얼은 저장 전 `PromptForCheckoutAndSave` 하나를, 유니티는 `AssetModificationProcessor.OnWillSaveAssets` 하나를 모든 저장이 지나고, Godot 은 디스크의 파일이 바뀌었는지 묻는다.
     */
    class SW_EDITOR_API AssetWriter
    {
    public:
        AssetWriter();

        /** @brief @p path(파일, 또는 폴더면 그 아래 모든 파일)에서 처음 만난 읽기 전용 파일의 경로입니다. 없거나 모두 쓸 수 있으면 빈 값입니다. */
        [[nodiscard]] static optional<string> findReadOnlyAtOrUnder( const DiskPath& path );
        /**
         * @brief @p path 에 읽기 전용 파일이 있으면 알림 · 경고 로그를 남기고 true 입니다(부르는 쪽은 아무것도 쓰지 않고 실패한다).
         * @details 쓰기 실패 로그만으로는 "먼저 잠가야 한다" 는 사실이 드러나지 않는다 — 그래서 실패 대신 이유를 알린다.
         */
        [[nodiscard]] static bool warnIfReadOnly( const DiskPath& path, AssetWriteKind kind );

        /** @brief @p path 의 지금 디스크 내용을 기준으로 적습니다(읽은 직후). 없는 파일의 기준은 지웁니다. */
        void capture( const DiskPath& path );
        /** @brief @p path 의 글을 읽고 그 내용을 기준으로 적습니다 — 읽고 고쳐 쓰는 명령의 앞 절반입니다. */
        [[nodiscard]] bool readText( const DiskPath& path, string& outText );
        /** @brief @p path 가 기준과 다른 내용이면 true 입니다(밖에서 바뀜). 기준이 없거나 파일이 없으면 false 입니다 — 알리지 않는다. */
        [[nodiscard]] bool isChangedOnDisk( const DiskPath& path ) const;

        /** @brief @p path 하나를 써도 되는지 봅니다(`validateSave( span )`). */
        [[nodiscard]] bool validateSave( const DiskPath& path );
        /**
         * @brief @p listPath 를 모두 써도 되는지 봅니다(여러 파일을 쓰는 저장은 첫 파일을 쓰기 전에 부른다). 안 되면 알림 · 경고 로그를 남기고 false 입니다.
         * @return 써도 되면 true 입니다. 밖에서 바뀌어 거절한 내용 그대로 다시 물으면 true 입니다.
         */
        [[nodiscard]] bool validateSave( span<const DiskPath> listPath );

        /** @brief 확인을 지나면 부모 폴더를 만들고 @p text 를 원자적으로 쓴 뒤 그 내용을 새 기준으로 적습니다. 지면 파일은 그대로이고 false 입니다. */
        [[nodiscard]] bool writeText( const DiskPath& path, string_view text );
        /** @brief `writeText` 의 바이트 판입니다. */
        [[nodiscard]] bool writeBytes( const DiskPath& path, span<const uint8> bytes );
        /** @brief 에셋 @p asset 을 `saveToFile( AbsolutePath )` 로 씁니다(`save`). */
        template <typename TAsset>
        [[nodiscard]] bool saveAsset( const DiskPath& path, const TAsset& asset )
        {
            return save( path, [&asset]( const AbsolutePath& writable )
            { return asset.saveToFile( writable ); } );
        }
        /**
         * @brief 파일 글이 @p expected 그대로일 때만 @p text 로 바꿉니다 — 파일을 쓰는 되돌리기 · 다시 하기 단계가 부른다.
         * @details 그 사이 다른 편집기 · 체크아웃이 고친 파일을 되돌리기가 말없이 옛 글로 덮던 자리다. 다르면 알리고 false 다(파일은 그대로).
         */
        [[nodiscard]] static bool replaceText( const DiskPath& path, string_view expected, string_view text );
        /** @brief `writeText` 와 같은 길로, 쓰기를 @p saveTo( AbsolutePath ) -> bool(에셋의 `saveToFile`)에 맡깁니다. 받은 경로를 들고 나가지 않는다. */
        template <typename Func>
        [[nodiscard]] bool save( const DiskPath& path, Func&& saveTo )
        {
            const optional<AbsolutePath> writable = beginWrite( path );
            if ( writable.has_value() == false || saveTo( *writable ) == false )
                return false;
            capture( path );
            return true;
        }

    private:
        struct Entry
        {
            string _path;
            uint64 _hash{ 0 };        ///< 읽은(쓴) 때의 내용
            uint64 _refusedHash{ 0 }; ///< 거절하며 알린 디스크 내용. 같은 내용이면 다음 저장은 덮어쓴다
        };

        const Entry* find( string_view path ) const;
        Entry*       find( string_view path );
        void         setBaseline( const DiskPath& path, uint64 hash );
        /** @brief 밖에서 바뀐 파일이면 알리고 false 입니다. */
        [[nodiscard]] bool validateUnchanged( const DiskPath& path );
        /** @brief 확인을 지나고 부모 폴더를 만든 쓰는 경로입니다. */
        [[nodiscard]] optional<AbsolutePath> beginWrite( const DiskPath& path );

    private:
        vector<Entry> _listEntry;
    };
} // namespace sw::editor
