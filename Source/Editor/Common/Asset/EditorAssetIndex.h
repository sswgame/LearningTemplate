/**
 * @file EditorAssetIndex.h
 * @brief 에디터의 에셋 색인입니다 — 리소스 트리의 파일 목록 · 종류 · GUID 와 참조 그래프(누가 이것을 쓰나 · 이것이 무엇을 쓰나)를 함께 들고, 바뀐 파일만 고칩니다. ImGui 에 의존하지 않습니다.
 * @details 언리얼 "AssetRegistry" 의 자리는 둘로 갈린다: 정체성(GUID ↔ 리소스 id)은 런타임 `AssetRegistry`, 목록 · 종류 · 참조 그래프는 이 색인이다(에디터만 읽는 값이
 *          Engine 과 팩 색인에 들지 않게). 언리얼 `IAssetRegistry`(`FDependsNode` 양방향 그래프, 디렉터리 감시 증분), 유니티 `AssetDatabase`(시각 · 해시 증분),
 *          Godot `EditorFileSystem` 과 같은 자리다.
 */
#pragma once
#include "Core/Concurrency/StopToken.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Job/EditorBackgroundJob.h"

#include "Engine/Content/AssetGUID.h"

namespace sw
{
    class AssetScanCache;
    class TaskManager;
} // namespace sw

namespace sw::editor
{
    /** @brief 참조 하나입니다(질의 결과). @p _referrerPath 의 텍스트 안에 @p _targetPath 가 적혀 있습니다 — 경로로든 GUID 로든. */
    struct EditorAssetReference
    {
        string _referrerPath;      ///< 리소스 id(`game/playground/maps/destructionshowcase.scene.xml`, 소문자)
        string _targetPath;        ///< 리소스 id(소문자). GUID 참조는 지금 경로로 푼 것
        uint32 _line{ 0 };         ///< 1 부터 시작하는 줄 번호
        bool   _bInlined{ false }; ///< `_base="…"` 칸이다 — 참조자가 로드할 때 대상을 펼쳐 넣으므로 대상이 바뀌면 다시 읽는다(`AssetReloadCascade`). 경로만 적은 칸은 아니다.
        bool   _bByGUID{ false };  ///< GUID 참조(`guid:<32자>`)로 적었다 — 대상을 옮겨도 글을 고칠 것이 없다(옮기기의 다시 쓰기가 건너뛴다).
    };

    /**
     * @brief 텍스트 에셋 하나가 적은 참조 하나입니다. 대상은 글에 적힌 그대로의 열쇠다 — 소문자 리소스 id 이거나 GUID 참조 `guid:<32자>`.
     * @details 대상이 지금 있는지는 적을 때가 아니라 물을 때 본다. 그래서 파일이 생기거나 사라져도 다른 파일을 다시 읽지 않는다(증분의 바탕).
     */
    struct EditorAssetLink
    {
        string _target;            ///< 소문자 리소스 id 또는 `guid:<32자>`
        uint32 _line{ 0 };         ///< 1 부터 시작하는 줄 번호
        bool   _bInlined{ false }; ///< `_base="…"` 칸이다(`EditorAssetReference::_bInlined`).
    };

    /** @brief 파일 하나에서 뽑은 것입니다 — 정체성과 나가는 참조. 훑기 캐시에 두는 값도 이것이다. */
    struct EditorAssetContent
    {
        optional<AssetGUID>     _guid;     ///< 텍스트 에셋의 머리 · 원본 곁 `.meta` 의 정체성(없을 수 있다)
        vector<EditorAssetLink> _listLink; ///< 나가는 참조(텍스트 에셋만)
    };

    /** @brief 파일 하나의 줄입니다. */
    struct EditorAssetEntry
    {
        string                  _displayPath; ///< 디스크 표기의 리소스 id(대소문자 그대로) — 목록 · 제목에 쓴다. 열쇠는 소문자 id 다
        FileStamp               _fileStamp{}; ///< 크기 · 쓰기 시각 — 같으면 파일을 열지 않는다
        vector<EditorAssetLink> _listLink;    ///< 나가는 참조(텍스트 에셋만)
    };

    /** @brief 색인의 상태 전체입니다. 워커가 전체 스캔으로 짓고 UI 스레드가 `EditorAssetIndex::assign` 으로 넣는다. */
    struct EditorAssetIndexData
    {
        unordered_map<string, EditorAssetEntry>  _mapEntry;              ///< 키: 소문자 리소스 id
        unordered_map<string, vector<string>>    _mapReferrer;           ///< 참조 대상 열쇠(`EditorAssetLink::_target`) → 그것을 적은 파일 id(겹치지 않는다) — 역방향 표
        unordered_map<string, AssetGUID>         _mapPathToGUID;         ///< 정체성이 있는 파일(머리 글 · `.meta` 를 가진 원본)
        unordered_map<AssetGUID, vector<string>> _mapGUIDToPath;         ///< 그 정체성을 적은 파일들(들어온 순서, 맨 앞이 주인) — 복사본이 같은 GUID 를 든 동안에도 원본 줄을 잃지 않는다
        int64                                    _scanMilliseconds{ 0 }; ///< 전체 스캔에 걸린 시간(로그용)
        uint32                                   _scannedFileCount{ 0 }; ///< 읽은 텍스트 에셋 수
    };

    /**
     * @class EditorAssetIndex
     * @brief 리소스 트리의 에셋 색인입니다. 전체 스캔은 워커(`scan`), 바뀐 파일의 적용(`applyChanges`)과 질의는 UI 스레드에서만 합니다.
     * @details 바뀐 파일 하나의 비용은 그 파일의 참조 수에 비례한다 — 전체 목록을 지우거나 정렬하지 않는다(이 색인 앞의 참조 역색인은 파일 하나에도 전체 `removeIf` + 정렬을 했다).
     *          참조는 텍스트에서 `engine/` · `common/` · `game/` · `editor/` · `scratch/` 로 시작하는 경로와 GUID 참조(`guid:<32자>`)다. GUID 참조는 물을 때 지금 경로로 푼다 —
     *          대상을 옮겨도 참조자는 그대로다(옮기기가 글을 고칠 필요가 없다).
     *          정체성은 텍스트 에셋의 머리(`AssetGUIDHeader`)와 원본 곁 `.meta` 에서 읽는다(런타임 `AssetRegistry` 의 줄은 파일 감시가 따로 고친다).
     */
    class SW_EDITOR_API EditorAssetIndex
    {
    public:
        EditorAssetIndex();

        /**
         * @brief @p resourceRoot(`Content/`)와 작업 도메인 폴더 @p scratchFolder(`AssetPaths::getScratchFolderPath`, 비면 빼고 없어도 뺀다) 아래를 모두 훑어 @p outData 를 짓습니다.
         *        워커에서 호출합니다. 텍스트 에셋은 @p pTaskManager 로 파일 단위 병렬로 읽고, nullptr 이면 차례로 읽습니다.
         * @details @p stopToken 이 올라가면 남은 파일을 읽지 않고 돌아온다 — 결과는 불완전하니 부른 쪽이 신호를 다시 읽고 버린다.
         *          @p pCache(판 `kScanPayloadVersion`)를 주면 크기 · 시각이 그대로인 파일은 열지 않는다. 작업 도메인(실행마다 새 사본)의 파일은 캐시에 두지 않는다.
         */
        static void scan( const AbsolutePath& resourceRoot, const AbsolutePath& scratchFolder, EditorAssetIndexData& outData, TaskManager* pTaskManager, const StopToken& stopToken = {}, AssetScanCache* pCache = nullptr );
        /** @brief 훑기 캐시에 두는 값(`EditorAssetContent`)의 판입니다. 참조 뽑기(`extractLinks` · 도메인 표) · 텍스트 에셋 판정 · 정체성 읽기 규칙을 바꾸면 올린다. */
        static constexpr uint32 kScanPayloadVersion = 0;
        /** @brief 스캔한 결과로 색인을 바꿉니다. 이후 `isReady` 가 true 입니다. */
        void assign( EditorAssetIndexData&& data );
        /**
         * @brief 바뀐 파일 · 폴더 @p listChangedID(리소스 id)만 고칩니다 — 있으면 다시 읽고(크기 · 시각이 같으면 열지 않는다), 없으면 그 줄(폴더면 그 아래 줄)을 뺍니다.
         * @details 옮기기 · 이름 바꾸기는 옛 id 와 새 id 둘을 넘긴다. GUID 참조는 정체성이 따라가므로 참조자를 다시 읽지 않는다.
         *          작업 도메인 id(`scratch/…`)는 @p resourceRoot 가 아니라 실행 뿌리 아래에서 읽는다(`AssetPaths::getScratchFolderPath`).
         */
        void applyChanges( const AbsolutePath& resourceRoot, const vector<string>& listChangedID );
        /** @brief 파일 하나의 글을 디스크 없이 넣습니다(테스트 · 저장 직후의 글). @p assetID 는 디스크 표기 그대로 받는다. @p text 는 텍스트 에셋일 때만 읽는다. */
        void applyText( string_view assetID, string_view text, const FileStamp& fileStamp = {} );

        /** @brief @p targetPath 를 적은 참조를 모읍니다(경로로 적은 것과 GUID 로 적은 것 모두). 먼저 출력 목록을 비우며, 참조자 · 줄 순서입니다. 없는 파일이면 비어 있습니다. */
        void findReferrers( string_view targetPath, vector<EditorAssetReference>& outListReference ) const;
        /** @brief @p referrerPath 가 적은 참조 중 지금 있는 대상을 모읍니다(GUID 참조는 지금 경로로 푼다). 먼저 출력 목록을 비우며, 줄 순서입니다. */
        void findDependencies( string_view referrerPath, vector<EditorAssetReference>& outListReference ) const;
        /** @brief @p targetPath 를 적은 서로 다른 파일 수입니다. */
        uint32 countReferrerFiles( string_view targetPath ) const;
        /**
         * @brief 아는 파일의 리소스 id 를 디스크 표기(`EditorAssetEntry::_displayPath`)로 모두 모읍니다(선택 팝업 · Open · Quick Open 의 목록). 먼저 출력 목록을 비우며, 정렬합니다.
         * @details 종류로 거르기는 부르는 쪽이 한다 — 종류 등록(`EditorAssetTypeRegistry`)은 이 색인보다 위 층이다. 디스크를 걷지 않는다.
         */
        void collectDisplayPaths( vector<string>& outListPath ) const;

        /** @brief 트리의 파일로 아는 리소스 id 이면 그 줄, 아니면 nullptr 입니다. */
        [[nodiscard]] const EditorAssetEntry* find( string_view assetID ) const;
        /** @brief 트리의 파일로 아는 리소스 id 이면 true 입니다. */
        bool containsFile( string_view assetID ) const { return find( assetID ) != nullptr; }
        /** @brief @p guid 를 든 파일의 리소스 id 입니다(복사). 없으면 없음입니다. */
        [[nodiscard]] optional<string> findPath( const AssetGUID& guid ) const;
        /** @brief 리소스 id @p assetID 의 GUID 입니다. 정체성이 없으면 없음입니다. */
        [[nodiscard]] optional<AssetGUID> findGUID( string_view assetID ) const;
        /** @brief 아는 파일 수입니다. */
        size_t getFileCount() const { return _data._mapEntry.size(); }
        /** @brief 한 번이라도 결과를 넣었으면 true 입니다. */
        bool isReady() const { return _bReady == SW_TRUE; }
        /** @brief 마지막 전체 스캔이 읽은 텍스트 에셋 수입니다. */
        uint32 getScannedFileCount() const { return _data._scannedFileCount; }

        /** @brief 참조를 훑는 텍스트 에셋이면 true 입니다. 확장자로 정한다(자동화 시나리오 · 리다이렉트 표는 참조자가 아니다). */
        static bool isTextAsset( string_view path );
        /** @brief 텍스트 하나에서 참조를 뽑아 @p outListLink 에 더합니다(비우지 않는다). 대상이 있는지는 보지 않는다. @p listMountedDomain 은 고정 도메인 표 밖의 붙인 디스크 도메인 이름이다. */
        static void extractLinks( string_view text, vector<EditorAssetLink>& outListLink, const vector<string>& listMountedDomain = {} );
        /**
         * @brief 파일의 글에서 줄의 내용을 뽑습니다. @p pText 는 텍스트 에셋의 글, @p pMetaText 는 원본 곁 `.meta` 의 글이고, 없으면 nullptr 입니다. 아무 스레드에서나 부른다.
         * @details 정체성은 텍스트 에셋이면 머리, 그 밖이면 곁의 `.meta` 에서 읽는다(`.meta` 는 목록의 파일이 아니다 — `EditorContentScan::isListedFile`).
         */
        static EditorAssetContent makeContent( const string* pText, const string* pMetaText, const vector<string>& listMountedDomain = {} );

    private:
        /** @brief 파일 @p assetID 의 줄과 그 줄이 만든 역방향 표 · GUID 줄을 뺍니다. */
        static void removeEntry( EditorAssetIndexData& inoutData, const string& assetID );
        /** @brief 파일 @p displayPath(디스크 표기)의 줄을 넣습니다(있던 줄은 먼저 뺀다). */
        static void insertEntry( EditorAssetIndexData& inoutData, string_view displayPath, const FileStamp& fileStamp, EditorAssetContent&& content );
        /** @brief 파일 @p displayPath 를 디스크에서 읽어 줄을 고칩니다. 크기 · 시각이 같으면(@p bForce 가 아니면) 열지 않는다. 없으면 줄을 뺀다. */
        static void refreshFromDisk( EditorAssetIndexData& inoutData, const AbsolutePath& resourceRoot, string_view displayPath, bool bForce );

    private:
        EditorAssetIndexData   _data;
        uint8                  _bReady   : 1;
        [[maybe_unused]] uint8 _reserved : 7;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorAssetIndexScanJob
     * @brief 에셋 색인의 전체 스캔(`EditorAssetIndex::scan`)을 워커에서 돌립니다.
     */
    class EditorAssetIndexScanJob final : public EditorBackgroundJob<AbsolutePath, EditorAssetIndexData>
    {
    public:
        /** @brief 워커에 @p resourceRoot 아래 스캔을 요청합니다. 이미 대기 중이면 세대를 올리고 오래된 결과는 버립니다. 작업 관리자는 부른 쪽(UI 스레드)이 찾아 넘깁니다. */
        void request( const AbsolutePath& resourceRoot, TaskManager* pTaskManager );

    private:
        static void runJob( const TaskArgs& args );
    };
} // namespace sw::editor
