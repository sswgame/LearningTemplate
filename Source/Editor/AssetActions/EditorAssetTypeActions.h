/**
 * @file EditorAssetTypeActions.h
 * @brief 애셋 종류별 에디터 동작(썸네일 · 열기 · 뷰포트 드롭)의 인터페이스와 등록부입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

struct ImDrawList;

namespace sw
{
    struct float2;
    struct float3;

    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    enum class EditorAssetType : uint8;

    /**
     * @class IEditorAssetTypeActions
     * @brief 애셋 종류 하나가 에디터에서 **하는 일**입니다 — 언리얼 `FAssetTypeActions` 의 자리입니다.
     * @details 보여 주는 정보(이름 · 아이콘 · 색)는 종류 표(`EditorAssetTypeInfo`)에 있고, 여기에는 동작만 둔다. 종류의 코드 파일이
     *          `EditorAssetTypeActionsRegistrar` 로 등록하므로 콘텐츠 브라우저 · `EditorAssetCommands` 에는 종류별 분기가 없다.
     *          모든 동작은 선택이다. false 를 돌려주면 부르는 쪽이 일반 동작(일반 문서 썸네일 · 열기)으로 넘어간다.
     */
    class IEditorAssetTypeActions
    {
    public:
        virtual ~IEditorAssetTypeActions() = default;

        /** @brief 이 동작이 맡는 종류입니다. 종류마다 하나만 등록됩니다. */
        virtual EditorAssetType getKind() const = 0;
        /** @brief 콘텐츠 브라우저 카드 썸네일을 그립니다(배경 · 테두리는 브라우저가 그린다). 그리지 않으면 false 입니다. */
        virtual bool drawThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos ) const;
        /** @brief 전용 도구 패널이 없는 종류를 엽니다. 이 종류가 열기를 하지 않거나 열지 못했으면 false 입니다. */
        [[nodiscard]] virtual bool open( string_view relativePath ) const;
        /** @brief 뷰포트에 끌어 놓은 것을 처리합니다(스폰 · 로드). 처리하지 않으면 false 이고, 부르는 쪽이 열기로 넘어갑니다. */
        [[nodiscard]] virtual bool dropInViewport( GameObjectManager* pManager, const utf8* pPath, const float3& spawnPos ) const;
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorAssetTypeActionsRegistry
     * @brief 종류 → 동작 등록부입니다. 등록은 정적 초기화 때 `EditorAssetTypeActionsRegistrar` 가 합니다.
     * @details 등록부는 EditorModule 안에 있어 핫 리로드로 모듈이 바뀌면 등록자와 함께 새로 만들어진다.
     */
    class EditorAssetTypeActionsRegistry
    {
    public:
        /** @brief 동작을 등록합니다. 같은 종류가 이미 등록돼 있으면 오류를 남기고 앞의 것을 둡니다. */
        static void registerActions( const IEditorAssetTypeActions& actions );
        /** @brief 등록을 지웁니다. 그 종류에 다른 동작이 등록돼 있으면 건드리지 않습니다. */
        static void unregisterActions( const IEditorAssetTypeActions& actions );
        /** @brief 종류의 동작입니다. 등록되지 않았으면 nullptr 입니다. */
        static const IEditorAssetTypeActions* findActions( EditorAssetType kind );
        /** @brief 경로가 속한 종류(`EditorAssetTypeRegistry::findKind`)의 동작입니다. 없으면 nullptr 입니다. */
        static const IEditorAssetTypeActions* findActionsForPath( string_view path );
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class EditorAssetTypeActionsRegistrar
     * @brief 종류의 코드 파일에 정적 객체로 두면 동작을 만들어 등록하고, 모듈이 내려갈 때 지웁니다.
     * @details EditorModule 은 MODULE DLL 이라 소스의 목적 파일이 모두 링크된다 — 아무도 참조하지 않는 등록자도 버려지지 않는다.
     */
    template <typename TActions>
    class EditorAssetTypeActionsRegistrar
    {
    public:
        EditorAssetTypeActionsRegistrar()
            : _actions{}
        {
            EditorAssetTypeActionsRegistry::registerActions( _actions );
        }
        ~EditorAssetTypeActionsRegistrar() { EditorAssetTypeActionsRegistry::unregisterActions( _actions ); }

        EditorAssetTypeActionsRegistrar( const EditorAssetTypeActionsRegistrar& )            = delete;
        EditorAssetTypeActionsRegistrar& operator=( const EditorAssetTypeActionsRegistrar& ) = delete;

        const TActions& getActions() const { return _actions; }

    private:
        TActions _actions;
    };
} // namespace sw::editor
