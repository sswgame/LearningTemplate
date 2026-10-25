/**
 * @file EditorReferenceIndex.h
 * @brief 리소스 트리의 텍스트 에셋에 적힌 리소스 id 를 모아 "누가 이것을 쓰나" 와 "이것이 무엇을 쓰나" 에 답합니다. ImGui 에 의존하지 않습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/EditorExports.h"

namespace sw
{
    class TaskManager;
} // namespace sw

namespace sw::editor
{
    /** @brief 참조 하나입니다. @p _referrerPath 의 텍스트 안에 @p _targetPath 가 적혀 있습니다. */
    struct EditorAssetReference
    {
        string _referrerPath; ///< 리소스 id(`game/empty/maps/destructionshowcase.scene.xml`, 소문자)
        string _targetPath;   ///< 리소스 id(소문자)
        uint32 _line{ 0 };    ///< 1 부터 시작하는 줄 번호
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 한 번 스캔한 결과입니다. 워커가 만들고 UI 스레드가 색인에 넣습니다(`EditorReferenceIndex::assign`). */
    struct EditorReferenceIndexData
    {
        vector<EditorAssetReference> _listReference;    ///< 대상 경로 순서로 정렬한다. 질의는 이분 탐색을 쓴다
        vector<string>               _listKnownID;      ///< 트리의 모든 파일 리소스 id(소문자, 정렬)
        int64                        _scanMilliseconds; ///< 스캔에 걸린 시간(로그용)
        uint32                       _scannedFileCount; ///< 스캔한 텍스트 에셋 수

        EditorReferenceIndexData();
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorReferenceIndex
     * @brief 리소스 트리의 텍스트 에셋(xml, json, material, hlsl)을 스캔해 참조를 모은 역색인입니다. 유니티의 Find References 처럼 텍스트를 검색합니다.
     * @details 텍스트 안에서 `engine/`, `common/`, `game/`, `editor/` 로 시작하는 경로 중 실제로 있는 파일의 리소스 id 만 참조로 센다.
     *          이름이 우연히 경로처럼 생겼어도 그 파일이 없으면 세지 않는다. 바이너리(dds, mesh, 셰이더 바이너리)는 스캔하지 않는다.
     *          스캔은 워커에서 `scan` 으로 하고, 결과를 UI 스레드가 `assign` 으로 넣는다. 질의와 `refreshFile` 은 UI 스레드에서만 호출한다.
     */
    class SW_EDITOR_API EditorReferenceIndex
    {
    public:
        EditorReferenceIndex();

        /**
         * @brief @p resourceRoot(`Resource/`) 아래를 모두 스캔해 @p outData 를 채웁니다. 워커에서 호출합니다. 텍스트 에셋은 @p pTaskManager 로 파일 단위 병렬로 읽고,
         *        nullptr 이면 현재 스레드가 차례로 읽습니다.
         * @details 두 경로의 결과는 같습니다(파일 순서로 합친 뒤 전순서 키로 정렬). 호출 스레드가 작업 관리자의 워커여도 막히지 않습니다.
         *          작업 관리자는 부른 쪽이 넘깁니다 — 워커는 에디터 범위를 찾지 않으므로(`findEditorScope`) 제출하는 UI 스레드가 찾아 잡 인자로 준다.
         */
        static void scan( string_view resourceRoot, EditorReferenceIndexData& outData, TaskManager* pTaskManager );
        /** @brief 스캔한 결과로 색인을 교체합니다. 이후 `isReady` 가 true 입니다. */
        void assign( EditorReferenceIndexData&& data );
        /** @brief 파일 하나의 참조를 다시 스캔합니다. 파일이 지워졌으면 그 파일이 적은 참조와 그 파일의 id 를 뺍니다. */
        void refreshFile( string_view resourceRoot, string_view resourcePath );

        /** @brief @p targetPath 를 적은 참조를 모읍니다. 먼저 출력 목록을 비우며, 리소스 id 의 대소문자는 구분하지 않습니다. */
        void findReferrers( string_view targetPath, vector<EditorAssetReference>& outListReference ) const;
        /** @brief @p referrerPath 가 적은 참조를 모읍니다. 먼저 출력 목록을 비우며, "이 에셋이 쓰는 것" 입니다. */
        void findDependencies( string_view referrerPath, vector<EditorAssetReference>& outListReference ) const;
        /** @brief @p targetPath 를 적은 서로 다른 파일 수를 반환합니다. */
        uint32 countReferrerFiles( string_view targetPath ) const;

        /** @brief 트리의 파일로 아는 리소스 id(소문자)이면 true 입니다. 파일 하나만 다시 읽어도 되는지(있던 파일의 내용 변경) 가르는 데 씁니다. */
        bool containsFile( string_view resourceID ) const;
        /** @brief 한 번이라도 결과를 넣었으면 true 입니다. */
        bool isReady() const { return _bReady == SW_TRUE; }
        /** @brief 마지막으로 스캔한 텍스트 에셋 수를 반환합니다. */
        uint32 getScannedFileCount() const { return _data._scannedFileCount; }

        /** @brief 참조를 스캔하는 텍스트 에셋이면 true 입니다. 확장자로 정합니다. */
        static bool isTextAsset( string_view path );
        /**
         * @brief 텍스트 하나에서 리소스 id 를 뽑아 @p outListReference 에 추가합니다. 출력 목록을 비우지 않습니다.
         * @param listSortedKnownID 있는 파일의 리소스 id 입니다. 소문자이며 정렬되어 있습니다. 여기 있는 것만 참조로 셉니다.
         */
        static void extractReferences( string_view referrerPath, string_view text, const vector<string>& listSortedKnownID,
                                       vector<EditorAssetReference>& outListReference );

    private:
        /** @brief 참조 목록을 대상 경로 순으로 정렬합니다. */
        static void sortByTarget( vector<EditorAssetReference>& inoutListReference );

    private:
        EditorReferenceIndexData _data;
        uint8                    _bReady   : 1;
        [[maybe_unused]] uint8   _reserved : 7;
    };
} // namespace sw::editor
