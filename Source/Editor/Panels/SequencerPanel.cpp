#include "pch.h"

#include "Editor/Panels/SequencerPanel.h"

#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Workspace/EditorTransaction.h"

#include "Engine/Sequencer/SequenceAsset.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <ImSequencer.h>

namespace sw::editor
{
	struct Item
	{
		string _name;
		string _targetObject;
		int32  _start{ 0 };
		int32  _end{ 10 };
		int32  _type{ 0 };
		uint32 _color{ 0xFFAA8080 };
	};

	struct ClipSequence : ImSequencer::SequenceInterface
	{
		int32		   _frameMin{ 0 };
		int32		   _frameMax{ 100 };
		vector<Item>   _listItems;
		mutable string _labelScratch;

		int32 GetFrameMin() const override
		{
			return _frameMin;
		}
		int32 GetFrameMax() const override
		{
			return _frameMax;
		}
		int32 GetItemCount() const override
		{
			return static_cast<int32>( _listItems.size() );
		}
		int32 GetItemTypeCount() const override
		{
			return 2;
		}
		const utf8* GetItemTypeName( int32 typeIndex ) const override
		{
			return typeIndex == 0 ? "Clip" : "Event";
		}
		const utf8* GetItemLabel( int32 itemIndex ) const override
		{
			if ( itemIndex >= 0 && itemIndex < static_cast<int32>( _listItems.size() ) && _listItems[static_cast<size_t>( itemIndex )]._name.empty() == false )
				return _listItems[static_cast<size_t>( itemIndex )]._name.c_str();
			_labelScratch = "Item ";
			_labelScratch += to_string( itemIndex );
			return _labelScratch.c_str();
		}

		void Get( int32 itemIndex, int32** ppStart, int32** ppEnd, int32* pType, uint32* pColor ) override
		{
			Item& item = _listItems[static_cast<size_t>( itemIndex )];
			if ( ppStart != nullptr )
				*ppStart = &item._start;
			if ( ppEnd != nullptr )
				*ppEnd = &item._end;
			if ( pType != nullptr )
				*pType = item._type;
			if ( pColor != nullptr )
				*pColor = item._color;
		}

		void Add( int32 type ) override
		{
			Item item{};
			item._type	= type;
			item._start = _frameMin;
			item._end	= _frameMin + 10;
			item._color = type == 0 ? 0xFF80AA80u : 0xFF8080AAu;
			item._name	= type == 0
							? ( "Clip " + to_string( _listItems.size() ) )
							: ( "Event " + to_string( _listItems.size() ) );
			_listItems.push_back( item );
		}

		void Del( int32 itemIndex ) override
		{
			if ( itemIndex >= 0 && itemIndex < static_cast<int32>( _listItems.size() ) )
				_listItems.erase( _listItems.begin() + itemIndex );
		}

		void Duplicate( int32 itemIndex ) override
		{
			if ( itemIndex >= 0 && itemIndex < static_cast<int32>( _listItems.size() ) )
			{
				Item copy = _listItems[static_cast<size_t>( itemIndex )];
				copy._name += " Copy";
				_listItems.push_back( std::move( copy ) );
			}
		}
	};

	SequencerPanel::SequencerPanel()
		: EditorDocumentPanel{ EditorAssetKind::Sequence, false }
		, _bExpanded{ true }
		, _currentFrame{ 0 }
		, _selected{ -1 }
		, _firstFrame{ 0 }
		, _arrCinematicNote{}
		, _sequence{ make_unique<ClipSequence>() }
	{
		StringUtil::strncpy( _arrCinematicNote, "Cinematic notes (not a clip track).", sizeof( _arrCinematicNote ) - 1 );
		_sequence->Add( 0 );
		_sequence->Add( 1 );
		_sequence->_listItems[0]._name	= "Intro";
		_sequence->_listItems[1]._name	= "Cut";
		_sequence->_listItems[1]._start = 20;
		_sequence->_listItems[1]._end	= 40;
	}

	SequencerPanel::~SequencerPanel() = default;

	void SequencerPanel::drawContent()
	{
		loadFromFocusedPath();

		if ( EditorChrome::beginToolbar( "##SequencerToolbar" ) )
		{
			if ( ImGui::Button( "Load" ) )
				loadFromFocusedPath();
			ImGui::SameLine();
			if ( ImGui::Button( "Save" ) )
				saveToLoadedPath();
			ImGui::SameLine();
			if ( getLoadedAssetPath().empty() )
				ImGui::TextDisabled( "No .seq file focused" );
			else
				ImGui::TextDisabled( "%s", getLoadedAssetPath().c_str() );
		}
		EditorChrome::endToolbar();

		ImGui::SliderInt( "Scrub Frame", &_currentFrame, _sequence->_frameMin, _sequence->_frameMax );
		ImGui::Text( "Current Frame: %d", _currentFrame );

		ImGui::InputTextMultiline( "Cinematic Note", _arrCinematicNote, sizeof( _arrCinematicNote ), ImVec2( -1.0f, 60.0f ) );

		if ( _selected >= 0 && _selected < static_cast<int32>( _sequence->_listItems.size() ) )
		{
			Item& item = _sequence->_listItems[static_cast<size_t>( _selected )];
			utf8  arrNameBuf[constant::kMaxBuffer128];
			StringUtil::strncpy( arrNameBuf, item._name.c_str(), sizeof( arrNameBuf ) - 1 );
			arrNameBuf[sizeof( arrNameBuf ) - 1] = '\0';
			if ( ImGui::InputText( "Clip Name", arrNameBuf, sizeof( arrNameBuf ) ) )
				item._name = arrNameBuf;
			utf8 arrTargetBuf[constant::kMaxBuffer128];
			StringUtil::strncpy( arrTargetBuf, item._targetObject.c_str(), sizeof( arrTargetBuf ) - 1 );
			arrTargetBuf[sizeof( arrTargetBuf ) - 1] = '\0';
			if ( ImGui::InputText( "Target Object", arrTargetBuf, sizeof( arrTargetBuf ) ) )
				item._targetObject = arrTargetBuf;
		}

		ImSequencer::Sequencer( _sequence.get(), &_currentFrame, &_bExpanded, &_selected, &_firstFrame,
								ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_ADD | ImSequencer::SEQUENCER_DEL | ImSequencer::SEQUENCER_CHANGE_FRAME );
	}

	void SequencerPanel::loadFromFocusedPath()
	{
		if ( _sequence == nullptr || hasNewFocusedDocument() == false )
			return;

		const string_view focused = getMatchingFocusedPath();
		SequenceAsset	  asset;
		if ( EditorToolAssetCommands::loadSequence( asset, focused ) == false )
			return;

		acceptFocusedDocument();
		markDocumentLoaded();
		_sequence->_frameMin = asset._frameMin;
		_sequence->_frameMax = asset._frameMax;
		StringUtil::strncpy( _arrCinematicNote, asset._note.c_str(), sizeof( _arrCinematicNote ) - 1 );
		_arrCinematicNote[sizeof( _arrCinematicNote ) - 1] = '\0';
		_sequence->_listItems.clear();
		for ( const SequenceTrackItem& src : asset._listItem )
		{
			Item item{};
			item._name		   = src._name;
			item._targetObject = src._targetObject;
			item._start		   = src._start;
			item._end		   = src._end;
			item._type		   = src._type;
			item._color		   = src._color;
			_sequence->_listItems.push_back( std::move( item ) );
		}
	}

	void SequencerPanel::saveToLoadedPath()
	{
		if ( getLoadedAssetPath().empty() || _sequence == nullptr )
			return;

		SequenceAsset previous;
		const bool	  bHadFile	 = EditorToolAssetCommands::loadSequence( previous, getLoadedAssetPath() );
		const string  beforeJson = bHadFile ? previous.toJson() : string{};

		SequenceAsset asset;
		asset._frameMin = _sequence->_frameMin;
		asset._frameMax = _sequence->_frameMax;
		asset._note		= _arrCinematicNote;
		for ( const Item& src : _sequence->_listItems )
		{
			SequenceTrackItem item{};
			item._name		   = src._name;
			item._targetObject = src._targetObject;
			item._start		   = src._start;
			item._end		   = src._end;
			item._type		   = src._type;
			item._color		   = src._color;
			asset._listItem.push_back( std::move( item ) );
		}
		if ( EditorToolAssetCommands::saveSequence( asset, getLoadedAssetPath() ) == false )
			return;

		const string afterJson = asset.toJson();
		const string path	   = getLoadedAssetPath();
		EditorTransaction::recordDocumentText(
			beforeJson, afterJson, "Save Sequence",
			SW_DELEGATE_LAMBDA( EditorDocumentRestoreDelegate, [this, path]( string_view snapshot )
		{
			SequenceAsset restored;
			if ( snapshot.empty() == false )
				restored.parseJson( snapshot );
			EditorToolAssetCommands::saveSequence( restored, path );
			if ( getLoadedAssetPath() != path || _sequence == nullptr )
				return;
			_sequence->_frameMin = restored._frameMin;
			_sequence->_frameMax = restored._frameMax;
			StringUtil::strncpy( _arrCinematicNote, restored._note.c_str(), sizeof( _arrCinematicNote ) - 1 );
			_sequence->_listItems.clear();
			for ( const SequenceTrackItem& src : restored._listItem )
			{
				Item item{};
				item._name		   = src._name;
				item._targetObject = src._targetObject;
				item._start		   = src._start;
				item._end		   = src._end;
				item._type		   = src._type;
				item._color		   = src._color;
				_sequence->_listItems.push_back( std::move( item ) );
			}
		} ) );
	}
} // namespace sw::editor
