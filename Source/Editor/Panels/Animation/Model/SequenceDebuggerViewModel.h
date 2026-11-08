/**
 * @file SequenceDebuggerViewModel.h
 * @brief 컷씬 디버거의 ViewModel — 활성 씬의 시퀀스 플레이어를 찾아 고른 하나의 플레이헤드 · 활성 항목 · 바인딩 · 기록 · 키트 해석을 값으로 모읍니다(`gather`). ImGui 를 모릅니다.
 * @details 창은 읽기 전용이다(고치려면 시퀀서 패널). 플레이어 고르기는 보기 상태라 되돌리기에 남지 않는다. `docs/agents/EditorUI.md` MVVM 계약.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/GameObjectHandle.h"

namespace sw
{
    struct SequenceDiagnosticRegistration;
} // namespace sw

namespace sw::editor
{
    /** @brief 고를 수 있는 플레이어 하나입니다(값만). */
    struct SequenceDebuggerPlayerRow
    {
        string           _name;
        GameObjectHandle _handle{};
        uint32           _objectID{ 0 };
    };

    /** @brief 바인딩 하나가 지금 무엇에 붙었는지입니다. */
    enum class SequenceDebuggerBindingState : uint8
    {
        Bound = 0,     ///< 오브젝트에 붙었다(`_objectName`)
        NotSpawnedYet, ///< 스폰 항목이 아직 만들지 않았다
        Unbound        ///< 붙을 곳이 없다(오류)
    };

    /** @brief 바인딩 표의 한 줄입니다(값만). */
    struct SequenceDebuggerBindingRow
    {
        string                       _name;
        string                       _objectName;
        uint32                       _id{ 0 };
        SequenceDebuggerBindingState _state{ SequenceDebuggerBindingState::Unbound };
    };

    /** @brief 기록 표의 한 줄입니다(최근 줄이 앞이다, 값만). */
    struct SequenceDebuggerHistoryRow
    {
        string      _what;
        const utf8* _pKindLabel{ "" }; ///< 정적 글
        int32       _frame{ 0 };
    };

    /** @brief 키트 해석 하나입니다(값만). */
    struct SequenceDebuggerProviderRow
    {
        string _name;
        string _text;
    };

    /** @brief 컷씬 디버거가 한 프레임 그릴 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct SequenceDebuggerViewState
    {
        vector<SequenceDebuggerPlayerRow>   _listPlayer;     ///< 비면 활성 씬에 플레이어가 없다
        vector<string>                      _listActiveItem; ///< 지금 프레임에 걸린 항목 글(`이름 (바인딩) 시작..끝`)
        vector<SequenceDebuggerBindingRow>  _listBinding;
        vector<SequenceDebuggerHistoryRow>  _listHistory;
        vector<SequenceDebuggerProviderRow> _listProvider;
        string                              _watchedName;       ///< 고른 플레이어 이름(없으면 `-`)
        uint32                              _watchedIndex{ 0 }; ///< `_listPlayer` 의 고른 줄
        int32                               _currentFrame{ 0 };
        int32                               _frameMin{ 0 };
        int32                               _frameMax{ 0 };
        uint8                               _bHasWatched{ SW_FALSE }; ///< 고른 플레이어가 있다
        uint8                               _bPlaying{ SW_FALSE };
    };

    /** @brief 컷씬 디버거의 ViewModel 입니다. */
    class SequenceDebuggerViewModel
    {
    public:
        SequenceDebuggerViewModel();

        /** @brief 활성 씬(플레이 중이면 플레이 월드)에서 창이 그릴 값을 모읍니다(@p outState 를 비우고 채운다). 고른 플레이어가 없으면 첫 플레이어를 봅니다. */
        void gather( SequenceDebuggerViewState& outState );
        /** @brief 볼 플레이어를 고릅니다(보기 상태 — 되돌리기에 남지 않는다). */
        void watchPlayer( GameObjectHandle handle ) { _watched = handle; }

    private:
        vector<const SequenceDiagnosticRegistration*> _listProvider; ///< 이번 프레임의 제공자(멤버라 프레임마다 할당하지 않는다)
        GameObjectHandle                              _watched;      ///< 고른 플레이어의 오브젝트
    };
} // namespace sw::editor
