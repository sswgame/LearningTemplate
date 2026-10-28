#include "pch.h"

#include "Engine/Object/GameObject/Tick/FrameSystemLanePlan.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        constexpr bool intersects( const SceneDataKindSet& left, const SceneDataKindSet& right )
        {
            return left.intersect( right ).isEmpty() == false;
        }
    } // namespace

    bool hasFrameAccessConflict( const SceneFrameAccess& earlier, const SceneFrameAccess& later )
    {
        // 쓰기 · 쓰기, 쓰기 · 읽기(이번 프레임 · 지난 프레임 모두), 읽기 · 쓰기가 겹침이다. 읽기 · 읽기는 겹치지 않는다.
        if ( intersects( earlier._writes, later._writes ) || intersects( earlier._writes, later._reads ) || intersects( earlier._writes, later._readsPrevious ) )
            return true;
        return intersects( earlier._reads, later._writes ) || intersects( earlier._readsPrevious, later._writes );
    }

    void buildFrameSystemLanePlan( span<const SceneFrameAccess> listAccess, FrameSystemLanePlan& outPlan )
    {
        // 파도 벡터는 비우되 안의 용량은 프레임 사이에 둔다(`_listSystem` 만 비운다).
        for ( FrameSystemWave& wave : outPlan._listWave )
        {
            wave._listSystem.clear();
        }
        uint32 waveCount = 0;
        // 앞 시스템의 파도 번호 — 계획은 입력 순서로 한 번 훑어 만든다(앞 시스템이 이미 정해져 있다).
        vector<uint32>& listWaveOf = outPlan._listWaveOf;
        listWaveOf.clear();
        listWaveOf.reserve( listAccess.size() );
        for ( size_t systemIndex = 0; systemIndex < listAccess.size(); ++systemIndex )
        {
            uint32 wave = 0;
            for ( size_t earlierIndex = 0; earlierIndex < systemIndex; ++earlierIndex )
            {
                if ( hasFrameAccessConflict( listAccess[earlierIndex], listAccess[systemIndex] ) )
                    wave = MathUtil::max( wave, listWaveOf[earlierIndex] + 1 );
            }
            listWaveOf.push_back( wave );
            if ( outPlan._listWave.size() <= wave )
                outPlan._listWave.resize( wave + 1 );
            outPlan._listWave[wave]._listSystem.push_back( static_cast<uint32>( systemIndex ) );
            waveCount = MathUtil::max( waveCount, wave + 1 );
        }
        outPlan._listWave.resize( waveCount );
    }
} // namespace sw
