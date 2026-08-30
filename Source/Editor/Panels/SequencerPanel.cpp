#include "pch.h"

#include "Editor/Panels/SequencerPanel.h"

#include "Core/String/StringUtil.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Commands/EditorViewportPreview.h"
#include "Editor/Common/Gui/EditorChrome.h"

#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayer.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <ImSequencer.h>

namespace sw::editor
{
	struct Item
	{
		string _name;
		string _targetObject;
		float3 _translation{};
		float3 _rotation{};
		float3 _scale{ 1.0f, 1.0f, 1.0f };
		int32  _start{ 0 };
		int32  _end{ 10 };
		int32  _type{ 0 };
		uint32 _color{ 0xFFAA8080 };
	};

	struct ClipSequence : ImSequencer::SequenceInterface
	{
		int32		   _frameMin{ 0 };
		int32		   _frameMax{ 100 };
		vector<Item>   _listItem;
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
			return static_cast<int32>( _listItem.size() );
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
			if ( 0 <= itemIndex && itemIndex < static_cast<int32>( _listItem.size() ) && _listItem[static_cast<size_t>( itemIndex )]._name.empty() == false )
				return _listItem[static_cast<size_t>( itemIndex )]._name.c_str();
			_labelScratch = "Item ";
			_labelScratch += to_string( itemIndex );
			return _labelScratch.c_str();
		}

		void Get( int32 itemIndex, int32** ppStart, int32** ppEnd, int32* pType, uint32* pColor ) override
		{
			Item& item = _listItem[static_cast<size_t>( itemIndex )];
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
							? ( "Clip " + to_string( _listItem.size() ) )
							: ( "Event " + to_string( _listItem.size() ) );
			_listItem.push_back( item );
		}

		void Del( int32 itemIndex ) override
		{
			if ( 0 <= itemIndex && itemIndex < static_cast<int32>( _listItem.size() ) )
				_listItem.erase( _listItem.begin() + itemIndex );
		}

		void Duplicate( int32 itemIndex ) override
		{
			if ( 0 <= itemIndex && itemIndex < static_cast<int32>( _listItem.size() ) )
			{
				Item copy = _listItem[static_cast<size_t>( itemIndex )];
				copy._name += " Copy";
				_listItem.push_back( std::move( copy ) );
			}
		}
	};

	SequencerPanel::SequencerPanel()
		: EditorDocumentPanel{ EditorAssetKind::Sequence, false }
		, _cinematicNote{ "Cinematic notes (not a clip track)." }
		, _sequence{ make_unique<ClipSequence>() }
		, _previewPlayer{ make_unique<sw::SequencePlayer>() }
		, _currentFrame{ 0 }
		, _selected{ -1 }
		, _firstFrame{ 0 }
		, _bExpanded{ true }
	{
		_sequence->Add( 0 );
		_sequence->Add( 1 );
		_sequence->_listItem[0]._name  = "Intro";
		_sequence->_listItem[1]._name  = "Cut";
		_sequence->_listItem[1]._start = 20;
		_sequence->_listItem[1]._end   = 40;
	}

	SequencerPanel::~SequencerPanel() = default;

	void SequencerPanel::drawContent()
	{
		updateFocusedDocument();
		if ( isDocumentLoaded() == false )
			loadFromFocusedPath();

		if ( EditorChrome::beginToolbar( "##SequencerToolbar" ) )
		{
			if ( ImGui::Button( "Load" ) )
				loadFromFocusedPath();
			ImGui::SameLine();
			if ( ImGui::Button( "Save" ) )
				saveToLoadedPath();
			ImGui::SameLine();
			if ( ImGui::Button( "Play" ) )
			{
				syncPreviewPlayer();
				_previewPlayer->playFromFrame( _currentFrame );
			}
			ImGui::SameLine();
			if ( ImGui::Button( "Pause" ) )
				_previewPlayer->pause();
			ImGui::SameLine();
			if ( ImGui::Button( "Stop" ) )
			{
				_previewPlayer->stop();
				_currentFrame = _sequence != nullptr ? _sequence->_frameMin : 0;
			}
			ImGui::SameLine();
			if ( getLoadedAssetPath().empty() )
				ImGui::TextDisabled( "No .seq file focused" );
			else
				ImGui::TextDisabled( "%s", getLoadedAssetPath().c_str() );
		}
		EditorChrome::endToolbar();

		tickPreview( ImGui::GetIO().DeltaTime );

		ImGui::SliderInt( "Scrub Frame", &_currentFrame, _sequence->_frameMin, _sequence->_frameMax );
		if ( ImGui::IsItemDeactivatedAfterEdit() )
		{
			if ( _previewPlayer->isPlaying() )
				_previewPlayer->seekToFrame( _currentFrame );
			EditorViewportPreview::applySequenceFrame( captureAsset(), _currentFrame );
		}
		ImGui::Text( "Current Frame: %d", _currentFrame );

		ImGui::InputTextMultiline( "Cinematic Note", _cinematicNote.data(), _cinematicNote.capacity(), ImVec2( -1.0f, 60.0f ) );
		if ( ImGui::IsItemDeactivatedAfterEdit() )
			notifyDocumentEdited( "Edit Sequence Note", "sequence-note" );

		if ( 0 <= _selected && _selected < static_cast<int32>( _sequence->_listItem.size() ) )
		{
			Item&								  item = _sequence->_listItem[static_cast<size_t>( _selected )];
			fixed_string<constant::kMaxBuffer128> nameBuf{ item._name.c_str() };
			if ( ImGui::InputText( "Clip Name", nameBuf.data(), nameBuf.capacity() ) )
			{
				item._name = nameBuf.c_str();
			}
			if ( ImGui::IsItemDeactivatedAfterEdit() )
				notifyDocumentEdited( "Edit Sequence Clip", "sequence-clip" );
			fixed_string<constant::kMaxBuffer128> targetBuf{ item._targetObject.c_str() };
			if ( ImGui::InputText( "Target Object", targetBuf.data(), targetBuf.capacity() ) )
			{
				item._targetObject = targetBuf.c_str();
			}
			if ( ImGui::IsItemDeactivatedAfterEdit() )
				notifyDocumentEdited( "Edit Sequence Clip", "sequence-clip" );
			float32 arrTranslation[3] = { item._translation._x, item._translation._y, item._translation._z };
			if ( ImGui::DragFloat3( "Translation", arrTranslation, 0.1f ) )
			{
				item._translation._x = arrTranslation[0];
				item._translation._y = arrTranslation[1];
				item._translation._z = arrTranslation[2];
			}
			if ( ImGui::IsItemDeactivatedAfterEdit() )
				notifyDocumentEdited( "Edit Sequence Clip", "sequence-clip" );
			float32 arrRotation[3] = { item._rotation._x, item._rotation._y, item._rotation._z };
			if ( ImGui::DragFloat3( "Rotation", arrRotation, 0.5f ) )
			{
				item._rotation._x = arrRotation[0];
				item._rotation._y = arrRotation[1];
				item._rotation._z = arrRotation[2];
			}
			if ( ImGui::IsItemDeactivatedAfterEdit() )
				notifyDocumentEdited( "Edit Sequence Clip", "sequence-clip" );
			float32 arrScale[3] = { item._scale._x, item._scale._y, item._scale._z };
			if ( ImGui::DragFloat3( "Scale", arrScale, 0.01f ) )
			{
				item._scale._x = arrScale[0];
				item._scale._y = arrScale[1];
				item._scale._z = arrScale[2];
			}
			if ( ImGui::IsItemDeactivatedAfterEdit() )
				notifyDocumentEdited( "Edit Sequence Clip", "sequence-clip" );
		}

		ImSequencer::Sequencer( _sequence.get(), &_currentFrame, &_bExpanded, &_selected, &_firstFrame,
								ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_ADD | ImSequencer::SEQUENCER_DEL | ImSequencer::SEQUENCER_CHANGE_FRAME );
		if ( ImGui::IsItemEdited() )
			notifyDocumentEdited( "Edit Sequence Timeline", "sequence-timeline" );
	}

	void SequencerPanel::loadFromFocusedPath()
	{
		if ( _sequence == nullptr )
			return;

		string path = getLoadedAssetPath();
		if ( path.empty() )
			path = string{ getMatchingFocusedPath() };
		if ( path.empty() )
			return;

		SequenceAsset asset;
		if ( EditorToolAssetCommands::loadSequence( asset, path ) == false )
			return;

		if ( getLoadedAssetPath().empty() )
			acceptFocusedDocument();
		applyAsset( asset );
		markDocumentLoaded();
	}

	void SequencerPanel::saveToLoadedPath()
	{
		if ( getLoadedAssetPath().empty() || _sequence == nullptr )
			return;
		if ( EditorToolAssetCommands::saveSequence( captureAsset(), getLoadedAssetPath() ) == false )
			return;
		clearDocumentDirty();
		syncDocumentUndoBaseline();
	}

	bool SequencerPanel::saveDocument()
	{
		if ( getLoadedAssetPath().empty() )
			return false;
		saveToLoadedPath();
		return isDocumentDirty() == false;
	}

	SequenceAsset SequencerPanel::captureAsset() const
	{
		SequenceAsset asset;
		if ( _sequence == nullptr )
			return asset;
		asset._frameMin = _sequence->_frameMin;
		asset._frameMax = _sequence->_frameMax;
		asset._note		= _cinematicNote.c_str();
		for ( const Item& src : _sequence->_listItem )
		{
			SequenceTrackItem item{};
			item._name		   = src._name;
			item._targetObject = src._targetObject;
			item._translation  = src._translation;
			item._rotation	   = src._rotation;
			item._scale		   = src._scale;
			item._start		   = src._start;
			item._end		   = src._end;
			item._type		   = src._type;
			item._color		   = src._color;
			asset._listItem.push_back( std::move( item ) );
		}
		return asset;
	}

	void SequencerPanel::applyAsset( const SequenceAsset& asset )
	{
		if ( _sequence == nullptr )
			return;
		_sequence->_frameMin = asset._frameMin;
		_sequence->_frameMax = asset._frameMax;
		_cinematicNote		 = asset._note.c_str();
		_sequence->_listItem.clear();
		for ( const SequenceTrackItem& src : asset._listItem )
		{
			Item item{};
			item._name		   = src._name;
			item._targetObject = src._targetObject;
			item._translation  = src._translation;
			item._rotation	   = src._rotation;
			item._scale		   = src._scale;
			item._start		   = src._start;
			item._end		   = src._end;
			item._type		   = src._type;
			item._color		   = src._color;
			_sequence->_listItem.push_back( std::move( item ) );
		}
	}

	string SequencerPanel::captureDocumentText() const
	{
		return captureAsset().toJson();
	}

	void SequencerPanel::applyDocumentText( string_view text )
	{
		SequenceAsset restored;
		if ( text.empty() == false )
			restored.parseJson( text );
		applyAsset( restored );
	}

	void SequencerPanel::syncPreviewPlayer()
	{
		if ( _previewPlayer == nullptr )
			return;
		_previewPlayer->setAsset( captureAsset() );
		_previewPlayer->setLoop( true );
	}

	void SequencerPanel::tickPreview( float32 deltaSeconds )
	{
		if ( _previewPlayer == nullptr || _previewPlayer->isPlaying() == false )
			return;
		_previewPlayer->update( deltaSeconds );
		_currentFrame = _previewPlayer->getCurrentFrame();
		EditorViewportPreview::applySequenceFrame( _previewPlayer->getAsset(), _currentFrame );
		if ( _previewPlayer->isPlaying() == false )
			return;
		vector<const SequenceTrackItem*> listActive;
		_previewPlayer->collectActiveItems( listActive );
		(void)listActive;
	}
} // namespace sw::editor
