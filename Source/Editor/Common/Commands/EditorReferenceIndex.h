/**
 * @file EditorReferenceIndex.h
 * @brief 리소스 트리의 텍스트 에셋이 적은 리소스 id 를 모아 "누가 이것을 쓰나" 와 "이것이 무엇을 쓰나" 를 답합니다(ImGui 없음).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /** @brief 참조 하나입니다. @p _referrerPath 의 글 안에 @p _targetPath 가 적혀 있습니다. */
    struct EditorAssetReference
    {
        string _referrerPath; ///< 리소스 id(`game/empty/maps/destructionshowcase.scene.xml`, 소문자)
        string _targetPath;   ///< 리소스 id(소문자)
        uint32 _line{ 0 };    ///< 1 부터 센 줄 번호
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 한 번 훑은 결과입니다. 워커가 만들고 UI 스레드가 색인에 넣습니다(`EditorReferenceIndex::assign`). */
    struct EditorReferenceIndexData
    {
        vector<EditorAssetReference> _listReference;    ///< 대상 경로 순으로 정렬한다(질의는 이분 탐색)
        vector<string>               _listKnownID;      ///< 트리의 모든 파일 리소스 id(소문자, 정렬)
        int64                        _scanMilliseconds; ///< 훑기에 걸린 시간(로그용)
        uint32                       _scannedFileCount; ///< 훑은 텍스트 에셋 수

        EditorReferenceIndexData();
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorReferenceIndex
     * @brief 리소스 트리의 텍스트 에셋(xml, json, material, hlsl)을 훑어 참조를 모은 역색인입니다. 유니티 Find References 처럼 글을 검색합니다.
     * @details 글 안에서 `engine/` · `common/` · `game/` · `editor/` 로 시작하는 경로 가운데 **실제로 있는 파일의 리소스 id** 만 참조로 셉니다.
     *          이름이 우연히 경로처럼 생겼어도 그 파일이 없으면 세지 않는다. 바이너리(dds, mesh, 셰이더 바이너리)는 훑지 않는다.
     *          훑기는 워커에서 `scan` 으로 하고, 결과를 UI 스레드가 `assign` 으로 넣는다. 질의와 `refreshFile` 은 UI 스레드에서만 부른다.
     */
    class SW_EDITOR_API EditorReferenceIndex
    {
    public:
        EditorReferenceIndex();

        /** @brief @p resourceRoot(`Resource/`) 아래를 모두 훑어 @p outData 를 채웁니다. 워커에서 부릅니다. */
        static void scan( string_view resourceRoot, EditorReferenceIndexData& outData );
        /** @brief 훑은 결과로 색인을 바꿉니다. 이 뒤로 `isReady` 가 true 입니다. */
        void assign( EditorReferenceIndexData&& data );
        /** @brief 파일 하나의 참조를 다시 훑습니다. 파일이 지워졌으면 그 파일이 적은 참조와 그 파일의 id 를 뺍니다. */
        void refreshFile( string_view resourceRoot, string_view resourcePath );

        /** @brief @p targetPath 를 적은 참조를 모읍니다(먼저 비운다). 리소스 id 의 대소문자는 가리지 않습니다. */
        void findReferrers( string_view targetPath, vector<EditorAssetReference>& outListReference ) const;
        /** @brief @p referrerPath 가 적은 참조를 모읍니다(먼저 비운다). "이 에셋이 쓰는 것" 입니다. */
        void findDependencies( string_view referrerPath, vector<EditorAssetReference>& outListReference ) const;
        /** @brief @p targetPath 를 적은 서로 다른 파일 수입니다. */
        uint32 countReferrerFiles( string_view targetPath ) const;

        /** @brief 한 번이라도 결과를 넣었으면 true 입니다. */
        bool isReady() const { return _bReady == SW_TRUE; }
        /** @brief 마지막으로 훑은 텍스트 에셋 수입니다. */
        uint32 getScannedFileCount() const { return _data._scannedFileCount; }

        /** @brief 참조를 훑는 텍스트 에셋이면 true 입니다(확장자로 정한다). */
        static bool isTextAsset( string_view path );
        /**
         * @brief 글 하나에서 리소스 id 를 뽑아 @p outListReference 에 더합니다(비우지 않는다).
         * @param listSortedKnownID 있는 파일의 리소스 id(소문자, 정렬). 여기 있는 것만 참조로 센다.
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
