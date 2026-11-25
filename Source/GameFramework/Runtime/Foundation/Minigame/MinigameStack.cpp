#include "pch.h"

#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"

#include "Engine/Content/AssetRegistry.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Foundation/Save/GameStateSnapshot.h"
#include "GameFramework/Runtime/Foundation/Save/SaveLock.h"

#include <type_traits>

namespace sw
{
    namespace
    {
        struct MinigameStackInternal
        {
            static constexpr uint32 kMinBytesPerFrame  = sizeof( uint32 ) + sizeof( uint8 ); ///< 이름 길이 + 갈래 번호
            static constexpr uint32 kMinBytesPerResult = sizeof( uint32 ) * 4;               ///< 씬 · 미니게임 · 결과 이름 길이 + 값 수
            static constexpr uint32 kMinBytesPerValue  = sizeof( uint32 ) + sizeof( int64 ); ///< 이름 길이 + 수

            /** @brief 씬을 GUID 참조로 적습니다 — 세이브가 남은 뒤 씬을 옮겨도 풀린다. */
            static void writeScene( Archive& arch, const string& scenePath )
            {
                arch.writeString( AssetRegistry::makeReferenceText( scenePath, AssetRegistry::resolveGUID( scenePath ) ) );
            }

            /** @brief 넘기지 못한 결과 하나를 씁니다. */
            static void writeResult( Archive& arch, const MinigamePendingResult& pending )
            {
                writeScene( arch, pending._scenePath );
                arch.writeString( pending._result._minigame.view() );
                arch.writeString( pending._result._resultName.view() );
                arch << static_cast<uint32>( pending._result._listValue.size() );
                for ( const MinigameResultValue& value : pending._result._listValue )
                {
                    arch.writeString( value._name.view() );
                    arch << value._amount;
                }
            }

            /** @brief `writeResult` 의 짝입니다. 잘렸거나 씬이 없으면 없음입니다. */
            static optional<MinigamePendingResult> readResult( Archive& arch )
            {
                const expected<AssetReferenceTarget> scene = AssetRegistry::readReferenceText( arch.readString() );
                MinigamePendingResult                pending;
                pending._result._minigame   = hashed_string( arch.readString() );
                pending._result._resultName = hashed_string( arch.readString() );
                const uint32 count          = arch.readCount( kMinBytesPerValue );
                for ( uint32 index = 0; index < count && arch.isError() == false; ++index )
                {
                    MinigameResultValue value;
                    value._name = hashed_string( arch.readString() );
                    arch >> value._amount;
                    pending._result._listValue.push_back( std::move( value ) );
                }
                if ( arch.isError() || scene.has_value() == false || scene->_path.empty() )
                    return nullopt;
                pending._scenePath = scene->_path;
                return pending;
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
                const expected<AssetReferenceTarget> scene = AssetRegistry::readReferenceText( arch.readString() ); // GUID 참조라 씬을 옮긴 뒤에도 제 씬을 찾는다
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
        if ( _listFrame.empty() && _listResult.empty() )
            return;
        Archive arch;
        arch << static_cast<uint32>( _listFrame.size() );
        for ( const MinigameFrame& frame : _listFrame )
        {
            arch.writeString( frame._definition._name.view() );
            MinigameStackInternal::writeReturnPoint( arch, frame._returnPoint );
        }
        arch << static_cast<uint32>( _listResult.size() );
        for ( const MinigamePendingResult& pending : _listResult )
        {
            MinigameStackInternal::writeResult( arch, pending );
        }
        outBytes.assign( arch.getData(), arch.getData() + arch.getSize() );
    }

    bool MinigameStack::readSaveState( const vector<uint8>& bytes, MinigameSavedState& outState )
    {
        MinigameSavedState          state;
        vector<MinigameSavedFrame>& listFrame = state._listFrame;
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
            const uint32 resultCount = listFrame.size() == count ? arch.readCount( MinigameStackInternal::kMinBytesPerResult ) : 0;
            for ( uint32 index = 0; index < resultCount && arch.isError() == false; ++index )
            {
                optional<MinigamePendingResult> pending = MinigameStackInternal::readResult( arch );
                if ( pending.has_value() == false )
                    break;
                state._listResult.push_back( std::move( *pending ) );
            }
            const bool bRead = arch.isError() == false && listFrame.size() == count && state._listResult.size() == resultCount && arch.getRemainingBytes() == 0;
            if ( bRead == false || ( count == 0 && resultCount == 0 ) )
            {
                SW_LOG_ERROR_ID( "MinigameStack.SaveStateNotRead", "The minigames this save was made in could not be read (%# of %# frames) - the save is not used",
                                 static_cast<uint32>( listFrame.size() ), count );
                return false;
            }
        }
        outState = std::move( state );
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

    void MinigameFrame::removeMatchScore( vector<MinigameResultValue>& inoutListValue ) const
    {
        if ( _matchID == 0 )
            return;
        VectorUtil::removeIf( inoutListValue, [this]( const MinigameResultValue& value )
        { return value._name == _definition._matchScore; } );
    }

    void MinigameStack::pushResult( const string& scenePath, MinigameResult result )
    {
        _listResult.push_back( MinigamePendingResult{ scenePath, std::move( result ) } );
    }

    vector<MinigameResult> MinigameStack::takeResults( const string& scenePath )
    {
        vector<MinigameResult> listTaken;
        for ( MinigamePendingResult& pending : _listResult )
        {
            if ( pending._scenePath == scenePath )
                listTaken.push_back( std::move( pending._result ) );
        }
        VectorUtil::removeIf( _listResult, [&scenePath]( const MinigamePendingResult& pending )
        { return pending._scenePath == scenePath; } );
        return listTaken;
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
