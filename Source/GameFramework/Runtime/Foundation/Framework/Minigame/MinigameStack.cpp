#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameStack.h"

#include "Core/Log/Logger.h"

#include "Engine/Content/AssetRegistry.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Inherit/AssetRedirect.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/GameStateSnapshot.h"
#include "GameFramework/Runtime/Foundation/Framework/Save/SaveLock.h"

#include <type_traits>

namespace sw
{
    namespace
    {
        struct MinigameStackInternal
        {
            static constexpr uint32 kMinBytesPerFrame = sizeof( uint32 ) + sizeof( uint8 ); ///< 이름 길이 + 갈래 번호

            /** @brief 씬을 GUID 참조로 적습니다 — 세이브가 남은 뒤 씬을 옮겨도 풀린다. */
            static void writeScene( Archive& arch, const string& scenePath )
            {
                arch.writeString( AssetRegistry::makeReferenceText( scenePath, AssetRegistry::resolveGUID( scenePath ) ) );
            }

            /** @brief 돌아올 자리 하나를 갈래 번호와 함께 씁니다. */
            static void writeReturnPoint( Archive& arch, const MinigameReturnPoint& returnPoint )
            {
                arch << static_cast<uint8>( returnPoint.index() );
                std::visit( [&arch]( const auto& value )
                {
                    using T = std::decay_t<decltype( value )>;
                    if constexpr ( std::is_same_v<T, MinigameSuspendedScene> )
                    {
                        writeScene( arch, value._scenePath );
                        arch.writeSection( value._saveSnapshot.data(), static_cast<uint32>( value._saveSnapshot.size() ) );
                    }
                    else if constexpr ( std::is_same_v<T, MinigameSubLevelReturn> )
                    {
                        writeScene( arch, value._scenePath );
                        arch.writeString( value._subLevelName.view() );
                    }
                    else
                        static_assert( std::is_same_v<T, MinigameEntranceReturn>, "every return point kind is written to a save" );
                }, returnPoint );
            }

            /** @brief `writeReturnPoint` 의 짝입니다. 모르는 갈래거나 잘렸거나 씬이 없으면 없음입니다. 안에 든 옛 판 봉투는 바깥 봉투처럼 한 번 올린다. */
            static optional<MinigameReturnPoint> readReturnPoint( Archive& arch )
            {
                uint8 kind = 0;
                arch >> kind;
                if ( arch.isError() || kind >= std::variant_size_v<MinigameReturnPoint> )
                    return nullopt;
                if ( kind == MinigameReturnPoint{ MinigameEntranceReturn{} }.index() )
                    return MinigameReturnPoint{ MinigameEntranceReturn{} };
                const expected<AssetReferenceTarget> scene = AssetRedirect::readSavedReference( arch.readString() ); // 씬을 옮긴 뒤에도 옛 세이브가 제 씬을 찾는다
                if ( scene.has_value() == false )
                    return nullopt;
                const string& scenePath = scene->_path;
                if ( kind == MinigameReturnPoint{ MinigameSubLevelReturn{} }.index() )
                {
                    const string subLevelName = arch.readString();
                    if ( arch.isError() || scenePath.empty() || subLevelName.empty() )
                        return nullopt;
                    return MinigameReturnPoint{
                        MinigameSubLevelReturn{ scenePath, hashed_string( subLevelName ) }
                    };
                }
                MinigameSuspendedScene suspended;
                suspended._scenePath = scenePath;
                if ( arch.readSection( suspended._saveSnapshot ) == false || arch.isError() || scenePath.empty() || suspended._saveSnapshot.empty() )
                    return nullopt;
                if ( GameStateSnapshot::upgradeEnvelope( suspended._saveSnapshot ) == false )
                    return nullopt; // 까닭은 판 올림이 남겼다
                return MinigameReturnPoint{ std::move( suspended ) };
            }
        };
    } // namespace

    bool MinigameFrame::canSave() const
    {
        if ( _definition._savePolicy != MinigameSavePolicy::Allowed )
            return false;
        const MinigameSuspendedScene* const pSuspended = std::get_if<MinigameSuspendedScene>( &_returnPoint );
        return pSuspended == nullptr || pSuspended->_saveSnapshot.empty() == false;
    }

    void MinigameFrame::updateSaveBlock( SaveLock& saveLock )
    {
        if ( canSave() )
            _saveBlock.reset();
        else if ( _saveBlock.isActive() == false )
            _saveBlock = saveLock.block( hashed_string( kSaveBlockReason ) );
    }

    void MinigameStack::writeSaveState( vector<uint8>& outBytes ) const
    {
        outBytes.clear();
        if ( _listFrame.empty() )
            return;
        Archive arch;
        arch << static_cast<uint32>( _listFrame.size() );
        for ( const MinigameFrame& frame : _listFrame )
        {
            arch.writeString( frame._definition._name.view() );
            MinigameStackInternal::writeReturnPoint( arch, frame._returnPoint );
        }
        outBytes.assign( arch.getData(), arch.getData() + arch.getSize() );
    }

    bool MinigameStack::readSaveState( const vector<uint8>& bytes, vector<MinigameSavedFrame>& outListFrame )
    {
        vector<MinigameSavedFrame> listFrame;
        if ( bytes.empty() == false )
        {
            Archive      arch( bytes.data(), bytes.size() );
            const uint32 count = arch.readCount( MinigameStackInternal::kMinBytesPerFrame );
            for ( uint32 index = 0; index < count && arch.isError() == false; ++index )
            {
                const string                  name        = arch.readString();
                optional<MinigameReturnPoint> returnPoint = MinigameStackInternal::readReturnPoint( arch );
                if ( name.empty() || returnPoint.has_value() == false )
                    break;
                listFrame.push_back( MinigameSavedFrame{ hashed_string( name ), std::move( *returnPoint ) } );
            }
            if ( arch.isError() || count == 0 || listFrame.size() != count || arch.getRemainingBytes() != 0 )
            {
                SW_LOG_ERROR_ID( "MinigameStack.SaveStateNotRead", "The minigames this save was made in could not be read (%# of %# frames) - the save is not used",
                                 static_cast<uint32>( listFrame.size() ), count );
                return false;
            }
        }
        outListFrame = std::move( listFrame );
        return true;
    }

    bool MinigameStack::matches( const vector<MinigameSavedFrame>& listFrame ) const
    {
        if ( listFrame.size() != _listFrame.size() )
            return false;
        for ( size_t index = 0; index < listFrame.size(); ++index )
        {
            if ( listFrame[index]._minigame != _listFrame[index]._definition._name || listFrame[index]._returnPoint.index() != _listFrame[index]._returnPoint.index() )
                return false;
        }
        return true;
    }

    void MinigameStack::applyReturnPoints( vector<MinigameSavedFrame> listFrame, SaveLock& saveLock )
    {
        SW_ASSERT( matches( listFrame ) );
        for ( size_t index = 0; index < listFrame.size(); ++index )
        {
            _listFrame[index]._returnPoint = std::move( listFrame[index]._returnPoint );
            _listFrame[index].updateSaveBlock( saveLock );
        }
    }

    void MinigameFrame::takeMatchResult( MinigameResult& inoutResult ) const
    {
        if ( _matchID == 0 )
            return;
        inoutResult._matchID    = _matchID;
        inoutResult._matchBoard = _definition._matchBoard;
        for ( size_t index = 0; index < inoutResult._listValue.size(); ++index )
        {
            if ( inoutResult._listValue[index]._name != _definition._matchScore )
                continue;
            inoutResult._matchScore = inoutResult._listValue[index]._amount;
            inoutResult._listValue.erase( inoutResult._listValue.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }

    void MinigameStack::push( MinigameFrame frame )
    {
        _listFrame.push_back( std::move( frame ) );
    }

    bool MinigameStack::pop( MinigameFrame& outFrame )
    {
        if ( _listFrame.empty() )
            return false;
        outFrame = std::move( _listFrame.back() );
        _listFrame.pop_back();
        return true;
    }

    uint64 MinigameStack::findMatchID() const
    {
        for ( const MinigameFrame& frame : _listFrame )
        {
            if ( frame._matchID != 0 )
                return frame._matchID;
        }
        return 0;
    }

    bool MinigameStack::hasSuspendedScene() const
    {
        for ( const MinigameFrame& frame : _listFrame )
        {
            if ( std::holds_alternative<MinigameSuspendedScene>( frame._returnPoint ) )
                return true;
        }
        return false;
    }
} // namespace sw
