/**
 * @file EditorSelectionSync.h
 * @brief 선택 동기화 채널 — 선택이 바뀐 문맥(메인 씬 · 미리보기 씬 · 콘텐츠 브라우저)마다 "지금 고른 것이 무슨 애셋인가" 를 한 칸씩 남기고, 패널은 따를 문맥을 골라 읽습니다.
 * @details **왜 전역 선택 하나가 아닌가.** 선택은 문맥마다 따로다(`EditorSelection` — 메인 하나, 미리보기 창마다 하나). 하나로 합치면 미리보기 창에서 고른 부품이
 *          메인 씬 선택 · 인스펙터 · 콘텐츠 브라우저를 덮는다. 이 채널은 선택을 옮기지 않는다 — 바뀐 선택이 어느 문맥의 무슨 애셋인지(놓은 프리팹 · 메시 · 스프라이트 텍스처)만
 *          문맥의 칸에 적고 번호(`_sequence`)를 올린다. 받는 쪽이 따를 문맥을 마스크로 고른다: 콘텐츠 브라우저는 메인 씬만 따라 그 애셋을 표시하고(미리보기 선택은 보지 않는다),
 *          인스펙터는 지금처럼 겨누는 선택(`editor::getFocusedSelection`)을 본다. 언리얼의 "Sync to Content Browser"(메인 레벨 선택 → 애셋) · 유니티의 Ping 과 같은 뜻이고,
 *          둘 다 프리팹/애셋 편집 창의 선택은 레벨 선택과 따로 둔다.
 *          쓰는 곳은 둘이다 — 모든 오브젝트 선택 변경이 모이는 `EditorSelection::notifyChanged` 가 자기 문맥(메인 · 미리보기)으로, 콘텐츠 브라우저의 고르기(`ContentBrowserModel::selectAsset`)가 자기 문맥으로 알린다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/EditorSubsystem.h"

namespace sw
{
    class GameObject;
} // namespace sw

namespace sw::editor
{
    class EditorSelection;

    /** @brief 선택이 바뀐 문맥입니다. 받는 쪽은 비트 마스크(`makeSelectionContextBit`)로 따를 문맥을 고른다. */
    enum class EditorSelectionContext : uint8
    {
        MainScene = 0,  ///< 메인 씬(편집 중인 씬)의 오브젝트 선택 — 씬 뷰 · 계층
        PreviewScene,   ///< 미리보기 창(프리팹 창 · 애셋 미리보기)의 오브젝트 선택 — 창마다 자기 씬
        ContentBrowser, ///< 콘텐츠 브라우저에서 고른 애셋(작업 공간 포커스 애셋과 같은 값)
        Count
    };

    /** @brief 문맥 하나의 비트입니다. */
    constexpr uint8 makeSelectionContextBit( EditorSelectionContext context )
    {
        return static_cast<uint8>( 1u << static_cast<uint32>( context ) );
    }

    /** @brief 문맥 하나의 칸 — 마지막으로 고른 것의 애셋과 그 오브젝트, 바뀐 번호입니다. */
    struct EditorSelectionSyncEntry
    {
        string                 _assetPath{};                                  ///< 고른 것의 애셋 리소스 id(애셋이 없는 오브젝트 · 빈 선택이면 비어 있다)
        uint64                 _objectID{ 0 };                                ///< 고른 오브젝트 id(애셋 선택 · 빈 선택이면 0)
        uint64                 _sequence{ 0 };                                ///< 이 칸이 마지막으로 바뀐 채널 번호(0 이면 아직 없다)
        EditorSelectionContext _context{ EditorSelectionContext::MainScene }; ///< 칸의 문맥
    };

    /**
     * @class EditorSelectionSync
     * @brief 문맥마다 한 칸인 선택 동기화 채널입니다(편집기 범위 서브시스템). 게임 스레드에서만 쓴다. 파일 머리말 참고.
     */
    class SW_EDITOR_API EditorSelectionSync final : public EditorSubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "EditorSelectionSync";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        /** @brief 선택 @p selection 이 바뀌었다. 미리보기에 묶인 선택이면 미리보기 문맥, 아니면 메인 씬 문맥의 칸을 고칩니다(고른 첫 오브젝트의 애셋). */
        void onSelectionChanged( const EditorSelection& selection );
        /** @brief 문맥 @p context 의 칸을 @p assetPath · @p objectID 로 고칩니다. 같은 값이면 번호를 올리지 않는다 — 받는 쪽이 "바뀌었나" 를 번호로 본다. */
        void publish( EditorSelectionContext context, string_view assetPath, uint64 objectID );
        /** @brief 문맥 @p context 의 칸입니다. */
        const EditorSelectionSyncEntry& getEntry( EditorSelectionContext context ) const;
        /** @brief @p acceptedMask 의 문맥 가운데 가장 늦게 바뀐, 애셋이 있는 칸입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] const EditorSelectionSyncEntry* findLatest( uint8 acceptedMask ) const;
        /** @brief 오브젝트 @p object 가 무슨 애셋에서 왔는지입니다 — 프리팹 인스턴스면 그 프리팹, 아니면 메시 · 스프라이트 텍스처. 없으면 빈 글입니다. */
        [[nodiscard]] static string findObjectAssetPath( const GameObject& object );

    private:
        EditorSelectionSyncEntry _arrEntry[static_cast<size_t>( EditorSelectionContext::Count )]{};
        uint64                   _nextSequence{ 1 };
    };
} // namespace sw::editor
