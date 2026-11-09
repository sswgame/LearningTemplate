/**
 * @file SequencerViewModel.h
 * @brief 시퀀서 창의 ViewModel — 바인딩 문맥(이 시퀀스를 트는 지금 씬의 `SequencePlayerComponent`), 에디터 선택, 바인딩이 가리키는 오브젝트,
 *        뷰포트 미리보기 적용 · 풀기, 선택 묶기, 문서 읽기 · 쓰기를 맡습니다. ImGui 를 모릅니다(`docs/agents/EditorUI.md` MVVM 계약).
 * @details 시퀀스 문서(`SequenceModel`)는 창 문서라 패널이 들고 문서 되돌리기(`notifyDocumentEdited`)를 남긴다. 이 ViewModel 은 씬 쪽만 본다 —
 *          묶기는 씬 데이터(플레이어의 바인딩 표)라 그 오브젝트의 수정으로 되돌리기 한 단계를 남기고(`EditorTransaction`), 미리보기는 씬에 프레임을 적용할 뿐 기록하지 않는다
 *          (언리얼 Sequencer 가 레벨의 Level Sequence Actor 로 여는 것과 같다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

namespace sw
{
    class SequencePlayerComponent;
} // namespace sw

namespace sw::editor
{
    /** @brief 시퀀서 창이 한 프레임 그릴 씬 쪽 값입니다. 포인터 · 참조를 두지 않는다(MVVM 계약). */
    struct SequencerSceneViewState
    {
        vector<string> _listBoundName{};           ///< 문서 바인딩 순서대로 묶인 오브젝트 이름(묶이지 않았으면 빈 글)
        string         _contextName{};             ///< 바인딩 문맥 플레이어의 오브젝트 이름
        string         _selectionName{};           ///< 에디터에서 고른 오브젝트 이름
        uint8          _bHasContext{ SW_FALSE };   ///< 이 시퀀스를 트는 플레이어가 지금 씬에 있다
        uint8          _bHasSelection{ SW_FALSE }; ///< 메인 씬에서 고른 오브젝트가 있다
    };

    /**
     * @class SequencerViewModel
     * @brief 시퀀서 창의 씬 쪽 상태와 명령입니다. 파일 머리말 참고.
     */
    class SequencerViewModel
    {
    public:
        SequencerViewModel();
        ~SequencerViewModel();

        /** @brief 문서 @p asset(경로 @p documentPath)의 바인딩 문맥 · 선택 · 바인딩 대상 이름을 모읍니다(@p outState 를 비우고 채운다). */
        void gather( const SequenceAsset& asset, string_view documentPath, SequencerSceneViewState& outState ) const;

        /** @brief 문서를 새로 읽었다 — 그 시퀀스를 트는 첫 플레이어를 바인딩 문맥으로 고릅니다(없으면 비운다). */
        void resetContext( string_view documentPath );
        /**
         * @brief 바인딩 @p bindingID 를 에디터에서 고른 오브젝트에 묶습니다. 문맥 플레이어 오브젝트의 수정으로 되돌리기 한 단계("Bind Sequence Object")를 남긴다.
         * @return 문맥이나 고른 오브젝트가 없어 묶지 못했으면 false 입니다.
         */
        bool bindSelection( string_view documentPath, uint32 bindingID ) const;
        /** @brief 바인딩 문맥에서 묶이지 않은(스폰 항목도 아닌) 바인딩 수입니다. 문맥이 없으면 스폰하지 않는 모든 바인딩입니다. */
        [[nodiscard]] uint32 countUnboundBindings( const SequenceAsset& asset, string_view documentPath ) const;
        /** @brief @p track 의 대상(바인딩 문맥으로 찾는다)의 지금 채널 값을 @p pOutArrValue 에 읽습니다. 대상이 없거나 읽지 못하면 false 입니다. */
        [[nodiscard]] bool readTrackValues( const SequenceAsset& asset, string_view documentPath, const SequenceKeyTrack& track, float32* pOutArrValue, uint32 valueCapacity ) const;

        /** @brief @p asset 의 @p frame 을 바인딩 문맥으로 씬에 적용합니다(키 · 클립 · 컷 · 구간 동작 — 뷰포트 미리보기, 되돌리기 없음). */
        void applyPreviewFrame( const SequenceAsset& asset, string_view documentPath, int32 frame );
        /** @brief 미리보기가 들어간 구간(소리 · 애니메이션 덮어쓰기)을 나가고 컷을 풉니다(Stop · 문서 교체). */
        void releasePreview( const SequenceAsset& asset, string_view documentPath );

        /** @brief 창이 다루는 문서 종류(시퀀스)입니다. */
        [[nodiscard]] static EditorAssetType getDocumentType();
        /** @brief 시퀀스 파일 @p path 를 @p outAsset 에 읽습니다. */
        [[nodiscard]] static ToolAssetLoadResult loadSequence( SequenceAsset& outAsset, string_view path );
        /** @brief @p asset 을 시퀀스 파일 @p path 에 씁니다. 썼으면 true 입니다(실패는 로그). */
        [[nodiscard]] static bool saveSequence( const SequenceAsset& asset, string_view path );

    private:
        /** @brief 이 시퀀스를 트는 지금 씬의 플레이어입니다(고른 것이 사라졌으면 첫 것). 없으면 nullptr 입니다. */
        SequencePlayerComponent* findBindingContext( string_view documentPath ) const;
        /** @brief 바인딩 문맥의 표를 쓰는 창입니다(문맥이 없으면 빈 표). @p asset 은 호출 동안 살아 있어야 한다. */
        SequenceBindingView makeBindingView( const SequenceAsset& asset, string_view documentPath ) const;

    private:
        vector<SequenceSpanState>     _listPreviewSpan;  ///< 미리보기가 들어간 구간 동작(소리 · 애니메이션)
        vector<SequenceSpawnedObject> _listPreviewSpawn; ///< 미리보기는 스폰하지 않는다 — 바인딩 창이 비어 있는 목록을 가리킨다
        vector<SequenceBindingSlot>   _listNoSlot;       ///< 문맥이 없을 때 바인딩 창이 가리키는 빈 표(읽기만 한다)
        GameObjectHandle              _contextPlayer;    ///< 바인딩 문맥으로 고른 플레이어의 오브젝트
    };
} // namespace sw::editor
