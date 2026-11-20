#include "pch.h"

#include "Editor/Panels/RenderDebugger/Model/RenderGraphLayout.h"

#include "Core/Algorithm/VectorUtil.h"

namespace sw::editor
{
    RenderGraphLayout RenderGraphLayout::make( const RenderGraphSnapshot& snapshot )
    {
        RenderGraphLayout layout{};
        vector<uint32>    listRowInColumn( snapshot._levelCount, 0u );
        layout._listNode.reserve( snapshot._listPass.size() );
        for ( uint32 passIndex = 0; passIndex < static_cast<uint32>( snapshot._listPass.size() ); ++passIndex )
        {
            const uint32 column = snapshot._listPass[passIndex]._level;
            if ( column >= listRowInColumn.size() )
                listRowInColumn.resize( column + 1, 0u );
            RenderGraphLayoutNode node{};
            node._pass     = passIndex;
            node._column   = column;
            node._row      = listRowInColumn[column]++;
            node._position = float2{ static_cast<float32>( column ) * kColumnSpacing, static_cast<float32>( node._row ) * kRowSpacing };
            layout._listNode.push_back( node );
        }
        layout._columnCount = static_cast<uint32>( listRowInColumn.size() );

        // 자원마다 마지막으로 쓴 패스 — 읽기(쓰기 아닌 접근)는 그 패스에서 잇는다. 한 패스 안에서는 읽기를 먼저 본다(읽고 쓰는 UAV 가 자기 자신을 잇지 않게).
        vector<optional<uint32>> listLastWriter( snapshot._listResource.size() );
        for ( uint32 passIndex = 0; passIndex < static_cast<uint32>( snapshot._listPass.size() ); ++passIndex )
        {
            const RenderGraphSnapshotPass& pass = snapshot._listPass[passIndex];
            for ( const RenderGraphSnapshotAccess& access : pass._listAccess )
            {
                const bool bRead = isRenderGraphWriteAccess( access._access ) == false || access._access == RenderGraphAccess::UnorderedAccess;
                if ( bRead == false || access._resource >= listLastWriter.size() || listLastWriter[access._resource].has_value() == false )
                    continue;
                const uint32 fromPass = *listLastWriter[access._resource];
                if ( fromPass == passIndex )
                    continue;
                const bool bLinked = VectorUtil::containsIf( layout._listLink, [fromPass, passIndex]( const RenderGraphLayoutLink& link )
                { return link._fromPass == fromPass && link._toPass == passIndex; } );
                if ( bLinked == false )
                    layout._listLink.push_back( RenderGraphLayoutLink{ fromPass, passIndex, access._resource } );
            }
            for ( const RenderGraphSnapshotAccess& access : pass._listAccess )
            {
                if ( isRenderGraphWriteAccess( access._access ) && access._resource < listLastWriter.size() )
                    listLastWriter[access._resource] = passIndex;
            }
        }
        return layout;
    }
} // namespace sw::editor
