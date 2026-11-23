#include "pch.h"

#include "Editor/Panels/RenderDebugger/Model/BufferViewLayout.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"

namespace sw::editor
{
    BufferViewLayout BufferViewLayout::make( uint32 elementSize, BufferViewScalar scalar, const vector<RenderBufferEntry::Member>& listMember )
    {
        BufferViewLayout layout{};
        layout._stride      = elementSize;
        layout._columnCount = MathUtil::min( elementSize / kCellBytes, kMaxColumnCount );
        layout._scalar      = scalar;
        for ( uint32 column = 0; column < layout._columnCount; ++column )
        {
            const uint32                     offset  = column * kCellBytes;
            const RenderBufferEntry::Member* pMember = nullptr;
            for ( const RenderBufferEntry::Member& member : listMember )
            {
                if ( member._offset <= offset && offset < member._offset + member._sizeBytes )
                    pMember = &member;
            }
            StringBuilder<constant::buffer::kMax64> name;
            BufferViewScalar                        columnScalar = BufferViewScalar::Hex;
            if ( pMember == nullptr )
            {
                name.appendFormat( "+%#", offset );
            }
            else
            {
                name.append( pMember->_name );
                if ( pMember->_sizeBytes > kCellBytes )
                    name.appendFormat( "[%#]", ( offset - pMember->_offset ) / kCellBytes );
                const string_view type{ pMember->_type };
                if ( type.find( "Float" ) == 0 )
                    columnScalar = BufferViewScalar::Float;
                else if ( type.find( "Int" ) == 0 )
                    columnScalar = BufferViewScalar::Int;
                else if ( type.find( "Uint" ) == 0 || type.find( "Bool" ) == 0 )
                    columnScalar = BufferViewScalar::Uint;
            }
            layout._listColumnName.emplace_back( name.view() );
            layout._listColumnScalar.push_back( columnScalar );
        }
        return layout;
    }

    uint32 BufferViewLayout::getRowCount( size_t byteCount ) const
    {
        if ( _stride == 0 )
            return 0;
        return static_cast<uint32>( MathUtil::min( byteCount / _stride, static_cast<size_t>( kMaxRowCount ) ) );
    }

    bool BufferViewLayout::formatRow( const vector<uint8>& byte, uint32 row, vector<string>& outListCell ) const
    {
        outListCell.clear();
        const size_t rowOffset = static_cast<size_t>( row ) * _stride;
        if ( _stride == 0 || rowOffset + _stride > byte.size() )
            return false;
        for ( uint32 column = 0; column < _columnCount; ++column )
        {
            uint32 bits = 0;
            Memory::copy( &bits, byte.data() + rowOffset + static_cast<size_t>( column ) * kCellBytes, kCellBytes );
            StringBuilder<constant::buffer::kMax32> cell;
            switch ( _scalar == BufferViewScalar::Shader ? _listColumnScalar[column] : _scalar )
            {
                case BufferViewScalar::Float:
                {
                    float32 value = 0.0f;
                    Memory::copy( &value, &bits, sizeof( value ) );
                    cell.appendFormat( "%.6g", static_cast<float64>( value ) );
                    break;
                }
                case BufferViewScalar::Int:
                {
                    int32 value = 0;
                    Memory::copy( &value, &bits, sizeof( value ) );
                    cell.appendFormat( "%#", value );
                    break;
                }
                case BufferViewScalar::Hex:
                {
                    cell.appendFormat( "0x%08X", bits );
                    break;
                }
                case BufferViewScalar::Uint:
                case BufferViewScalar::Shader:
                case BufferViewScalar::Count:
                {
                    cell.appendFormat( "%#", bits );
                    break;
                }
            }
            outListCell.emplace_back( cell.view() );
        }
        return true;
    }

    const utf8* BufferViewLayout::getScalarLabel( BufferViewScalar scalar )
    {
        switch ( scalar )
        {
            case BufferViewScalar::Shader:
                return "shader";
            case BufferViewScalar::Float:
                return "float";
            case BufferViewScalar::Uint:
                return "uint";
            case BufferViewScalar::Int:
                return "int";
            case BufferViewScalar::Hex:
                return "hex";
            case BufferViewScalar::Count:
                break;
        }
        return "";
    }
} // namespace sw::editor
