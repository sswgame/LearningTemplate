/**
 * @file SequencerTool.cpp
 */
#include "Tools/SequencerTool.h"
#include "Runtime/EditorUIContext.h"

#include <imgui.h>
#include <cstring>

namespace sw
{
	SequencerTool::SequencerTool()
		: IEditorWindow( false )
	{
		std::strncpy( _cinematicNote, "Cinematic notes (not a clip track).", sizeof( _cinematicNote ) - 1 );
		_sequence.Add( 0 );
		_sequence.Add( 1 );
		_sequence.items[0].name	 = "Intro";
		_sequence.items[1].name  = "Cut";
		_sequence.items[1].start = 20;
		_sequence.items[1].end	 = 40;
	}

	void SequencerTool::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( ImGui::Begin( getWindowTitle(), getOpenPtr() ) == false )
		{
			ImGui::End();
			return;
		}

		ImGui::SliderInt( "Scrub Frame", &_currentFrame, _sequence.frameMin, _sequence.frameMax );
		ImGui::Text( "Current Frame: %d", _currentFrame );

		ImGui::InputTextMultiline( "Cinematic Note", _cinematicNote, sizeof( _cinematicNote ), ImVec2( -1.0f, 60.0f ) );

		if ( _selected >= 0 && _selected < static_cast<int>( _sequence.items.size() ) )
		{
			Item& item = _sequence.items[static_cast<size_t>( _selected )];
			char  nameBuf[128];
			std::strncpy( nameBuf, item.name.c_str(), sizeof( nameBuf ) - 1 );
			nameBuf[sizeof( nameBuf ) - 1] = '\0';
			if ( ImGui::InputText( "Clip Name", nameBuf, sizeof( nameBuf ) ) )
				item.name = nameBuf;
		}

		ImSequencer::Sequencer( &_sequence, &_currentFrame, &_bExpanded, &_selected, &_firstFrame,
								ImSequencer::SEQUENCER_EDIT_STARTEND | ImSequencer::SEQUENCER_ADD | ImSequencer::SEQUENCER_DEL | ImSequencer::SEQUENCER_CHANGE_FRAME );

		ImGui::End();
	}
} // namespace sw
