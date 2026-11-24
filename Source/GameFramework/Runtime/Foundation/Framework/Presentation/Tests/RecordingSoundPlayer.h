/**
 * @file RecordingSoundPlayer.h
 * @brief 낸 사운드 이벤트를 적기만 하는 테스트용 소리 창구입니다. 게임 범위에 `ISoundPlayer` 로 겁니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Presentation/ISoundPlayer.h"

namespace sw
{
    /** @brief 낸 이벤트 이름과 자리를 차례로 적는 소리 창구입니다. */
    class RecordingSoundPlayer final : public ISoundPlayer
    {
    public:
        bool                         play( const AssetID&, const hashed_string& ) override { return false; }
        bool                         playMusic( const AssetID& ) override { return false; }
        [[nodiscard]] bool           loadEvents( const AssetID& ) override { return true; }
        void                         unloadEvents( string_view ) override {}
        SoundPlayingID               postEvent( const hashed_string& eventName ) override { return record( eventName, 0.0f ); }
        SoundPlayingID               postEventAt( const hashed_string& eventName, const float3& position ) override { return record( eventName, position._x ); }
        SoundPlayingID               postEventOn( GameObject&, const hashed_string& ) override { return 0; }
        void                         setObjectParameter( const GameObject&, const hashed_string&, float32 ) override {}
        void                         setParameter( const hashed_string&, float32 ) override {}
        void                         stop( SoundPlayingID, float32 ) override {}
        const vector<hashed_string>& getPosted() const { return _listPosted; }
        float32                      getLastX() const { return _lastX; }
        /** @brief @p eventName 을 낸 수입니다. */
        int32 countPosted( const utf8* pEventName ) const
        {
            const hashed_string eventName( pEventName );
            int32               count = 0;
            for ( const hashed_string& posted : _listPosted )
            {
                count += posted == eventName ? 1 : 0;
            }
            return count;
        }

    private:
        SoundPlayingID record( const hashed_string& eventName, float32 x )
        {
            _listPosted.push_back( eventName );
            _lastX = x;
            return _listPosted.size();
        }

    private:
        vector<hashed_string> _listPosted{};
        float32               _lastX{ 0.0f };
    };
} // namespace sw
