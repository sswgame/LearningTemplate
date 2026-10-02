#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "Engine/Dialogue/DialogueGraphAsset.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class IFlagStore;

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
        int32 _nodeId{ 0 };
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

        PROPERTY( Category = "Dialogue", DisplayName = "Graph", AssetPath, AssetType = "DialogueGraph", Tooltip = "Dialogue graph asset" )
        string _graphPath;

        using OnDialogueLineFunc     = OnDialogueLineDelegate;
        using OnDialogueChoicesFunc  = OnDialogueChoicesDelegate;
        using OnDialogueEventFunc    = OnDialogueEventDelegate;
        using OnDialogueFinishedFunc = OnDialogueFinishedDelegate;

        DialogueRunnerComponent();
        virtual ~DialogueRunnerComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        [[nodiscard]] bool loadGraphFile( string_view jsonPath );
        [[nodiscard]] bool loadGraphJson( string_view jsonContent );

        FUNCTION( Category = "Playback", DisplayName = "Start Dialogue", CallInEditor )
        bool startDialogue( int32 startNodeId = -1 );
        bool advance();
        bool selectChoice( int32 choiceIndex );
        void stopDialogue();
        FUNCTION( Category = "Preview", DisplayName = "Preview Line" )
        void previewLine( string speaker, string text );

        void setFlagStore( IFlagStore* pFlagStore );

        DialogueRunnerState   getState() const;
        int32                 getCurrentNodeId() const;
        const string&         getCurrentSpeaker() const;
        const string&         getCurrentText() const;
        const vector<string>& getCurrentChoices() const;

        void setOnDialogueLine( OnDialogueLineFunc func );
        void setOnDialogueChoices( OnDialogueChoicesFunc func );
        void setOnDialogueEvent( OnDialogueEventFunc func );
        void setOnDialogueFinished( OnDialogueFinishedFunc func );

    private:
        /**
         * @brief Branch 노드의 조건식을 평가합니다. 빈 식은 참입니다.
         * @details 모양은 `[flag.]키` (= `키 == 1`) 또는 `[flag.]키 <연산자> 정수` 이고 연산자는 `==` · `!=` · `>=` · `<=` · `>` · `<` 입니다.
         *          읽지 못한 식(정수가 아닌 오른쪽, 표에 없는 연산자 글자)은 경고하고 거짓입니다.
         */
        bool evaluateCondition( const string& condition ) const;
        void executeNode( int32 nodeId, int32 recursionDepth = 0 );
        void executeAction( string actionCmd );
        /**
         * @brief `_onLine` 을 **사본으로** 부릅니다. 핸들러가 그 안에서 진행시켜도 안전합니다.
         * @details 델리게이트가 받는 `const string&` 가 이 객체의 멤버를 그대로 가리키면,
         *          핸들러가 `advance()` · `stopDialogue()` 를 부르는 순간 자기가 받은 참조가
         *          바뀌거나 비워집니다. 대화 UI 에서 가장 흔한 사용법이 바로 그것입니다.
         */
        void notifyLine();
        /** @brief `_onChoices` 를 사본으로 부릅니다. 핸들러가 돌면서 `selectChoice()` 를 불러도 됩니다. */
        void notifyChoices();

        DialogueGraphAsset     _graph;
        IFlagStore*            _pFlagStore;
        string                 _currentSpeaker;
        string                 _currentText;
        vector<string>         _listCurrentChoice;
        OnDialogueLineFunc     _onLine;
        OnDialogueChoicesFunc  _onChoices;
        OnDialogueEventFunc    _onEvent;
        OnDialogueFinishedFunc _onFinished;
        DialogueRunnerState    _state;
        int32                  _currentNodeId;
    };
} // namespace sw
