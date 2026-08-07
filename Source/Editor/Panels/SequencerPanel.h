#pragma once
/**
 * @file SequencerPanel.h
 * @brief ImSequencer demo (from ImGuizmo package)
 */
#include "IEditorPanel.h"
#include "Core/Common/CommonHeaders.h"
#include <ImSequencer.h>

namespace sw
{
	class SequencerPanel : public IEditorPanel
	{
	public:
		SequencerPanel();

		const char* getWindowTitle() const override { return "Sequencer"; }
		void		draw( const EditorUIContext& ctx ) override;

	private:
		struct Item
		{
			int			 start = 0;
			int			 end   = 10;
			int			 type  = 0;
			unsigned int color = 0xFFAA8080;
		};

		struct DemoSequence : public ImSequencer::SequenceInterface
		{
			int					   frameMin = 0;
			int					   frameMax = 100;
			std::vector<Item>	   items;
			mutable std::string	   labelScratch;

			int GetFrameMin() const override { return frameMin; }
			int GetFrameMax() const override { return frameMax; }
			int GetItemCount() const override { return static_cast<int>( items.size() ); }
			int GetItemTypeCount() const override { return 2; }
			const char* GetItemTypeName( int typeIndex ) const override
			{
				return typeIndex == 0 ? "Clip" : "Event";
			}
			const char* GetItemLabel( int index ) const override
			{
				labelScratch = "Item ";
				labelScratch += std::to_string( index );
				return labelScratch.c_str();
			}
			void Get( int index, int** start, int** end, int* type, unsigned int* color ) override
			{
				Item& item = items[static_cast<size_t>( index )];
				if ( start )
					*start = &item.start;
				if ( end )
					*end = &item.end;
				if ( type )
					*type = item.type;
				if ( color )
					*color = item.color;
			}
			void Add( int type ) override
			{
				Item item{};
				item.type  = type;
				item.start = frameMin;
				item.end   = frameMin + 10;
				item.color = type == 0 ? 0xFF80AA80 : 0xFF8080AA;
				items.push_back( item );
			}
			void Del( int index ) override
			{
				if ( index >= 0 && index < static_cast<int>( items.size() ) )
					items.erase( items.begin() + index );
			}
			void Duplicate( int index ) override
			{
				if ( index >= 0 && index < static_cast<int>( items.size() ) )
					items.push_back( items[static_cast<size_t>( index )] );
			}
		};

		bool		 _bExpanded	   = true;
		int			 _currentFrame = 0;
		int			 _selected	   = -1;
		int			 _firstFrame   = 0;
		DemoSequence _sequence;
	};
} // namespace sw
