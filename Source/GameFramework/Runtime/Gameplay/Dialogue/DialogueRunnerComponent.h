#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/DelegateSlot.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Behavior/ActionRegistry.h"
#include "Engine/Behavior/ConditionExpr.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Dialogue/DialogueGraphAsset.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class DialogueGraphAsset;
    struct DialogueStepInput;

    class GameFlags;
    class GameObject;
    class ISubtitleSink;

    ENUM()
    enum class DialogueRunnerState : uint8
    {
        Idle,
        ShowingDialogue,
        WaitingForChoice,
        Finished
    };

    REFLECT()
    struct SW_GF_API DialogueRunnerLine
    {
        REFLECT_BODY();
        PROPERTY()
        string _speaker;
        PROPERTY()
        string _text;
        PROPERTY()
        int32 _nodeID{ 0 };
    };

    /**
     * @brief 러너의 화자 표 한 줄 — 그래프의 화자 id 에 묶인 이 씬의 오브젝트입니다(시퀀서 `SequenceBindingSlot` 과 같은 모양).
     * @details 오브젝트는 핸들 PROPERTY 라 씬 파일에는 엔티티의 파일 id 로 적힌다 — 이름을 바꿔도 끊기지 않는다. 이름으로 찾는 폴백은 없다.
     */
    REFLECT()
    struct SW_GF_API DialogueSpeakerSlot
    {
        REFLECT_BODY();

        PROPERTY( Min = 1, Tooltip = "Speaker id in the dialogue graph" )
        uint32 _speaker{ kDialogueNoSpeaker };
        PROPERTY( Tooltip = "Scene object that speaks as that speaker" )
        GameObjectHandle _object{};
    };
} // namespace sw

namespace sw
{
    /** @brief 그래프를 받을 때 Branch · Action 노드를 컴파일해 둔 것입니다. 대화 중에 글을 읽지 않습니다. */
    struct DialogueCompiledNode
    {
        ConditionExpr  _condition{};
        GameActionCall _action{};
        int32          _nodeID{ 0 };
    };

    SW_DECLARE_DELEGATE( void, OnDialogueLineDelegate, const string& speaker, const string& text );
    SW_DECLARE_DELEGATE( void, OnDialogueChoicesDelegate, const vector<string>& listChoice );
    SW_DECLARE_DELEGATE( void, OnDialogueEventDelegate, const string& command );
    using OnDialogueFinishedDelegate = Delegate<void()>;

    REFLECT()
    class SW_GF_API DialogueRunnerComponent : public Component
    {
    public:
        REFLECT_BODY();

        PROPERTY( Category = "Dialogue", DisplayName = "Graph", Tooltip = "Dialogue graph asset" )
        AssetRef<DialogueGraphAsset> _graphPath;
        PROPERTY( Category = "Dialogue", DisplayName = "Post Subtitles", Tooltip = "Also show each line as a subtitle (leave off when a dialogue UI already shows the text)" )
        bool _bPostSubtitles;
        /** @brief 그래프의 화자 id 마다 이 씬의 오브젝트입니다. 같은 그래프를 다른 씬 · 다른 캐릭터에 다시 쓴다. */
        PROPERTY( Category = "Dialogue", DisplayName = "Speakers", Tooltip = "Scene object for each speaker id of the graph" )
        vector<DialogueSpeakerSlot> _listSpeakerSlot;

        static constexpr int32 kMaxChainedNodes = 64; ///< 사람을 기다리지 않고 한 번에 지나가는 노드 수의 상한

        using OnDialogueLineFunc     = OnDialogueLineDelegate;
        using OnDialogueChoicesFunc  = OnDialogueChoicesDelegate;
        using OnDialogueEventFunc    = OnDialogueEventDelegate;
        using OnDialogueFinishedFunc = OnDialogueFinishedDelegate;

        DialogueRunnerComponent();
        virtual ~DialogueRunnerComponent() override = default;

        void onRegister( GameObjectManager& manager ) override;
        void onUnregister( GameObjectManager& manager ) override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( TickContext& context ) override;
        /** @brief 읽어 들인 `_graphPath` 로 그래프를 다시 엽니다. 플레이 전(에디터)에도 Start Dialogue 가 그 그래프를 돌립니다. */
        void onPostLoad() override;
        /** @brief `_graphPath` 가 바뀌면 그 그래프를 다시 엽니다. */
        void onPropertyChanged( hashed_string propertyName ) override;

        [[nodiscard]] bool loadGraphFile( const AssetID& assetID );
        [[nodiscard]] bool loadGraphXML( string_view xmlContent );
        /** @brief 이미 만든 그래프를 씁니다. 코드로 만든 그래프나 에디터에서 넘긴 그래프를 받으며, 진행 중인 대화는 멈추지 않습니다. 틀린 조건 · 동작이 있으면 오류를 남기고 빈 그래프가 됩니다. */
        void setGraph( DialogueGraphAsset graph );

        FUNCTION( Category = "Playback", DisplayName = "Start Dialogue", CallInEditor )
        bool startDialogue( int32 startNodeID = -1 );
        bool advance();
        bool selectChoice( int32 choiceIndex );
        void stopDialogue();
        /**
         * @brief 에디터 대사 미리보기입니다. 이 러너의 그래프가 @p graphPath 면 줄을 보이고 화자 @p speakerID 에 묶인 오브젝트를 돌려줍니다(에디터가 그것을 고른다).
         * @details 다른 그래프의 러너는 아무것도 하지 않고 무효 핸들을 돌려준다. 화자 이름은 이 러너의 그래프 화자 목록에서 푼다 — 씬 오브젝트를 이름으로 찾지 않는다.
         */
        FUNCTION( Category = "Preview", DisplayName = "Preview Line", EditorPreview = "DialogueLine" )
        GameObjectHandle previewLine( string graphPath, uint32 speakerID, string text );
        /** @brief 화자 @p speakerID 에 묶인 살아 있는 오브젝트입니다. 묶이지 않았으면 nullptr 입니다. */
        GameObject* findSpeakerObject( uint32 speakerID ) const;
        /** @brief 화자 @p speakerID 를 @p object 에 묶습니다(칸이 있으면 바꾸고, 무효 핸들이면 칸을 지운다). 코드 · 테스트가 쓴다 — 씬 파일은 PROPERTY 로 든다. */
        void setSpeakerObject( uint32 speakerID, GameObjectHandle object );

        /** @brief Branch 조건과 `flag.*` 동작이 읽고 쓰는 월드 플래그입니다. 빌려 쓰므로 러너보다 오래 살아야 하며, nullptr 이면 모든 플래그가 0 입니다. */
        void setFlags( GameFlags* pFlags );
        /** @brief 조건 · 동작이 쓸 문맥의 바탕입니다(태그 · 달력 칸 등). 값을 복사합니다. 플래그는 `setFlags` 가 얹습니다. */
        void setGameplayContext( const GameplayContext& context );
        /** @brief `_bPostSubtitles` 가 줄을 보낼 곳입니다. 테스트가 정하며 nullptr 이면 엔진 서비스(`ISubtitleSink`, 전용 서버는 없다)를 씁니다. 빌려 씁니다. */
        void setSubtitleSink( ISubtitleSink* pSubtitleSink ) { _pSubtitleSinkOverride = pSubtitleSink; }

        DialogueRunnerState getState() const;
        int32               getCurrentNodeID() const;
        const string&       getCurrentSpeaker() const;
        /** @brief 지금 줄의 화자 id 입니다(없으면 `kDialogueNoSpeaker`). `findSpeakerObject` 로 오브젝트를 얻는다. */
        uint32                getCurrentSpeakerID() const { return _currentSpeakerID; }
        const string&         getCurrentText() const;
        const vector<string>& getCurrentChoices() const;

        // 대화 알림 칸은 칸마다 주인이 하나다(대화 UI) — 거는 쪽이 구독을 쥐고, 놓으면 칸이 빈다(이미 차 있으면 단언, `DelegateSlot`).
        [[nodiscard]] Subscription bindOnDialogueLine( OnDialogueLineFunc func ) { return _onLine.bind( std::move( func ) ); }
        [[nodiscard]] Subscription bindOnDialogueChoices( OnDialogueChoicesFunc func ) { return _onChoices.bind( std::move( func ) ); }
        [[nodiscard]] Subscription bindOnDialogueEvent( OnDialogueEventFunc func ) { return _onEvent.bind( std::move( func ) ); }
        [[nodiscard]] Subscription bindOnDialogueFinished( OnDialogueFinishedFunc func ) { return _onFinished.bind( std::move( func ) ); }

    private:
        /** @brief `_graph` 의 Branch · Action 노드를 컴파일합니다. 틀린 글이 하나라도 있으면(오류는 로그에 남는다) 그래프를 비우고 false 입니다. 대화가 틀린 분기를 타지 않게 로드에서 멈춘다. */
        [[nodiscard]] bool          compileGraph();
        const DialogueCompiledNode* findCompiled( int32 nodeID ) const;
        /** @brief 노드 @p nodeID 를 평가할 문맥입니다. 씨앗은 (진행 번호, 노드)입니다. */
        GameplayContext makeWorld( int32 nodeID ) const;
        /** @brief Branch 노드의 컴파일된 조건을 평가합니다. 플래그 저장소가 없으면 모든 이름이 0 입니다. */
        bool evaluateCondition( int32 nodeID ) const;
        /** @brief `_graphPath` 의 그래프를 엽니다. 경로가 비었으면 아무것도 하지 않고, 못 열면 경고합니다. */
        void loadGraphFromPath();
        /** @brief 노드 @p startNodeID 부터 실행합니다(비어 있으면 대화 끝). 지나가는 노드(시작 · 분기 · 행동)는 이어서 돌고, 사람을 기다리거나 끝나면 멈춥니다. 서로를 가리키는 노드는 상한에서 경고하고 끝냅니다. */
        void executeNode( optional<int32> startNodeID );
        /**
         * @brief 노드 하나를 실행합니다. 이어서 돌 노드가 있으면 true 와 그 id 입니다.
         * @details 모르는 타입은 경고하고 대화를 끝낸다. 대화가 멈춘 채 남지 않게 하려는 것이다.
         */
        [[nodiscard]] bool runNode( optional<int32> nodeID, optional<int32>& outNextNodeID );
        /** @brief 노드를 지나 다음 노드의 id 입니다. 노드가 그새 사라졌거나 다음이 없으면 비어 있습니다. */
        optional<int32> findNextNode( int32 nodeID, const DialogueStepInput& input ) const;
        /** @brief 상태를 Finished 로 두고 `_onFinished` 를 부릅니다. */
        void finishDialogue();
        void executeAction( int32 nodeID, string actionCmd );
        /**
         * @brief `_bPostSubtitles` 면 줄을 자막으로 보내고, `_onLine` 을 **사본으로** 부릅니다. 핸들러가 그 안에서 진행시켜도 안전합니다.
         * @details 델리게이트가 받는 `const string&` 가 이 객체의 멤버를 그대로 가리키면,
         *          핸들러가 `advance()` 나 `stopDialogue()` 를 부르는 순간 자기가 받은 참조가 바뀌거나 비워진다.
         *          대화 UI 에서 가장 흔한 사용법이 바로 그것이다.
         */
        void notifyLine();
        /** @brief `_onChoices` 를 사본으로 부릅니다. 핸들러가 돌면서 `selectChoice()` 를 불러도 됩니다. */
        void notifyChoices();

        DialogueGraphAsset                   _graph;
        vector<DialogueCompiledNode>         _listCompiled;
        GameFlags*                           _pFlags;
        GameplayContext                      _world;
        ISubtitleSink*                       _pSubtitleSinkOverride; ///< 자막을 보낼 곳(테스트), nullptr 이면 엔진 서비스
        string                               _currentSpeaker;
        string                               _currentText;
        vector<string>                       _listCurrentChoice;
        DelegateSlot<OnDialogueLineFunc>     _onLine;
        DelegateSlot<OnDialogueChoicesFunc>  _onChoices;
        DelegateSlot<OnDialogueEventFunc>    _onEvent;
        DelegateSlot<OnDialogueFinishedFunc> _onFinished;
        DialogueRunnerState                  _state;
        int32                                _currentNodeID;
        uint32                               _currentSpeakerID;
        /** @brief 노드에 들어가거나 대화를 멈출 때마다 늘어납니다. Action 핸들러가 그 안에서 대화를 옮겼는지 봅니다. */
        uint32 _transitionSerial;
    };
} // namespace sw
