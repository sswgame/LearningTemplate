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
	struct SaveSlot;

	namespace generated
	{
		struct sw_DialogueRunnerComponent_Registrar;
	} // namespace generated

	REFLECT()
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
	SW_DECLARE_DELEGATE( void, OnDialogueChoicesDelegate, const vector<string>& listChoices );
	SW_DECLARE_DELEGATE( void, OnDialogueEventDelegate, const string& command );
	using OnDialogueFinishedDelegate = Delegate<void()>;

	REFLECT()
	class SW_GF_API DialogueRunnerComponent : public Component
	{
		friend struct ::sw::generated::sw_DialogueRunnerComponent_Registrar;

	public:
		REFLECT_BODY();

		using OnDialogueLineFunc	 = OnDialogueLineDelegate;
		using OnDialogueChoicesFunc	 = OnDialogueChoicesDelegate;
		using OnDialogueEventFunc	 = OnDialogueEventDelegate;
		using OnDialogueFinishedFunc = OnDialogueFinishedDelegate;

		DialogueRunnerComponent();
		virtual ~DialogueRunnerComponent() override								 = default;
		DialogueRunnerComponent( DialogueRunnerComponent&& ) noexcept			 = default;
		DialogueRunnerComponent& operator=( DialogueRunnerComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		bool loadGraphFile( string_view jsonPath );
		bool loadGraphJson( string_view jsonContent );

		FUNCTION( Category = "Playback", DisplayName = "Start Dialogue", CallInEditor )
		bool startDialogue( int32 startNodeId = -1 );
		bool advance();
		bool selectChoice( int32 choiceIndex );
		void stopDialogue();
		FUNCTION( Category = "Preview", DisplayName = "Preview Line" )
		void previewLine( string speaker, string text );

		void setSaveSlot( SaveSlot* pSaveSlot );

		DialogueRunnerState	  getState() const;
		int32				  getCurrentNodeId() const;
		const string&		  getCurrentSpeaker() const;
		const string&		  getCurrentText() const;
		const vector<string>& getCurrentChoices() const;

		void setOnDialogueLine( OnDialogueLineFunc func );
		void setOnDialogueChoices( OnDialogueChoicesFunc func );
		void setOnDialogueEvent( OnDialogueEventFunc func );
		void setOnDialogueFinished( OnDialogueFinishedFunc func );

	private:
		bool evaluateCondition( const string& condition ) const;
		void executeNode( int32 nodeId );
		void executeAction( const string& actionCmd );

		PROPERTY( Category = "Dialogue", DisplayName = "Graph", AssetPath, AssetType = "DialogueGraph", Tooltip = "Dialogue graph asset" )
		string				_graphPath;
		DialogueGraphAsset	_graph;
		SaveSlot*			_pSaveSlot;
		DialogueRunnerState _state;
		int32				_currentNodeId;
		string				_currentSpeaker;
		string				_currentText;
		vector<string>		_listCurrentChoice;

		OnDialogueLineFunc	   _onLine;
		OnDialogueChoicesFunc  _onChoices;
		OnDialogueEventFunc	   _onEvent;
		OnDialogueFinishedFunc _onFinished;
	};
} // namespace sw
