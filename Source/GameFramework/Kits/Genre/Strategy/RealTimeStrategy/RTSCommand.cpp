#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCommand.h"

#include "Engine/Serialization/Format/Archive.h"

namespace sw
{
    namespace
    {
        struct RTSCommandInternal
        {
            static void valueUnitID( StateIO& io, RTSUnitID& inoutID )
            {
                uint64 packed = inoutID.packed();
                io.value( packed );
                if ( io.isReading() )
                    inoutID = RTSUnitID::fromPacked( packed );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void RTSCommand::serializeState( StateIO& io )
    {
        io.enumValue( _kind, RTSCommandKind::Count );
        io.check( _kind != RTSCommandKind::Count );
        io.list( _listUnit, sizeof( uint64 ), [&io]( RTSUnitID& unitID )
        { RTSCommandInternal::valueUnitID( io, unitID ); } );
        io.check( static_cast<int32>( _listUnit.size() ) <= kMaxUnitCount );
        io.value( _target );
        RTSCommandInternal::valueUnitID( io, _targetID );
        io.value( _defID );
        io.value( _cell );
        io.flag( _bQueue );
        io.flag( _bAttackMove );
    }

    void RTSCommand::writeList( const vector<RTSCommand>& listCommand, vector<uint8>& outBytes )
    {
        Archive archive;
        StateIO io( archive );
        uint32  version = kFormatVersion;
        io.value( version );
        vector<RTSCommand> listCopy = listCommand; // `list` 는 쓰기에도 고칠 수 있는 목록을 받는다(값은 바꾸지 않는다)
        io.list( listCopy, sizeof( uint8 ) + sizeof( uint32 ), [&io]( RTSCommand& command )
        { command.serializeState( io ); } );
        outBytes.assign( archive.getData(), archive.getData() + archive.getSize() );
    }

    bool RTSCommand::readList( const vector<uint8>& bytes, vector<RTSCommand>& outListCommand )
    {
        outListCommand.clear();
        if ( bytes.empty() )
            return true;
        Archive archive( bytes.data(), bytes.size() );
        StateIO io( archive );
        io.expect( kFormatVersion );
        vector<RTSCommand> listCommand;
        io.list( listCommand, sizeof( uint8 ) + sizeof( uint32 ), [&io]( RTSCommand& command )
        { command.serializeState( io ); } );
        io.end();
        if ( io.isOk() == false )
            return false;
        outListCommand = std::move( listCommand );
        return true;
    }
} // namespace sw
