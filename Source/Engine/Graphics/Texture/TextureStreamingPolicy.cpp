#include "pch.h"

#include "Engine/Graphics/Texture/TextureStreamingPolicy.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Config/EngineConfig.h"

namespace sw
{
    namespace
    {
        struct TextureStreamingPolicyInternal
        {
            static constexpr uint32 kPercentScale = 100; ///< 비율(%)의 분모
            /** @brief 유휴를 반영한 필요한 밉입니다 — 오래 안 보인 텍스처는 가장 작은 밉만 있으면 된다. */
            static uint32 computeWantedFirstMip( const TextureStreamingCandidate& candidate, const TextureStreamingConfig& config )
            {
                const uint32 lastMip = candidate._mipCount > 0 ? candidate._mipCount - 1u : 0u;
                if ( candidate._idleFrames >= config._idleFramesBeforeRelease )
                    return lastMip;
                return MathUtil::min( candidate._wantedFirstMip, lastMip );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    uint64 TextureStreamingCandidate::computeResidentBytes( uint32 firstMip ) const
    {
        uint64 total    = 0;
        uint64 mipBytes = _topMipBytes;
        for ( uint32 mip = 0; mip < _mipCount; ++mip )
        {
            if ( mip >= firstMip )
                total += mipBytes;
            mipBytes = MathUtil::max( mipBytes / 4u, uint64( 1 ) );
        }
        return total;
    }

    uint32 TextureStreamingPolicy::computeWantedFirstMip( uint32 topWidth, uint32 mipCount, float32 requiredTexels )
    {
        const uint32 lastMip = mipCount > 0 ? mipCount - 1u : 0u;
        if ( requiredTexels <= 0.0f )
            return lastMip;
        uint32 mip = 0;
        while ( mip < lastMip && static_cast<float32>( MathUtil::max( topWidth >> ( mip + 1u ), 1u ) ) >= requiredTexels )
            ++mip;
        return mip;
    }

    void TextureStreamingPolicy::decide( vector_reference<const TextureStreamingCandidate> listCandidate, uint64 residentBytes, uint64 budgetBytes, const TextureStreamingConfig& config,
                                         vector<TextureStreamingDecision>& outListDecision )
    {
        using Internal = TextureStreamingPolicyInternal;
        outListDecision.clear();
        const uint32   count = static_cast<uint32>( listCandidate.size() );
        vector<uint32> listOrder( count );
        vector<uint32> listWanted( count );
        for ( uint32 index = 0; index < count; ++index )
        {
            listOrder[index]  = index;
            listWanted[index] = Internal::computeWantedFirstMip( listCandidate[index], config );
        }
        uint64 resident = residentBytes;

        if ( resident > budgetBytes )
        {
            // 내리기 — 화면에서 작은 것(필요한 밉 번호가 큰 것), 같으면 오래 안 보인 것부터. 예산 아래가 되면 멈춘다.
            VectorUtil::stableSort( listOrder, [&]( uint32 lhs, uint32 rhs )
            {
                if ( listWanted[lhs] != listWanted[rhs] )
                    return listWanted[lhs] > listWanted[rhs];
                return listCandidate[lhs]._idleFrames > listCandidate[rhs]._idleFrames;
            } );
            for ( const uint32 index : listOrder )
            {
                if ( resident <= budgetBytes )
                    break;
                const TextureStreamingCandidate& candidate = listCandidate[index];
                const uint32                     lastMip   = candidate._mipCount > 0 ? candidate._mipCount - 1u : 0u;
                if ( candidate._residentFirstMip >= lastMip || candidate._framesSinceChange < config._minFramesBetweenChanges )
                    continue; // 더 내릴 밉이 없거나 막 바꿨다
                // 필요한 것보다 많이 들었으면 필요한 밉까지, 이미 필요한 만큼이면 한 밉 — 예산이 화면보다 우선이다.
                const uint32 target = MathUtil::min( lastMip, MathUtil::max( candidate._residentFirstMip + 1u, listWanted[index] ) );
                resident -= candidate.computeResidentBytes( candidate._residentFirstMip ) - candidate.computeResidentBytes( target );
                outListDecision.push_back( TextureStreamingDecision{ index, target } );
            }
            return; // 넘은 프레임에는 올리지 않는다
        }

        // 올리기: 화면에서 큰 것(필요한 밉 번호가 작은 것)부터. 올려서 예산을 넘으면 그 텍스처는 건너뛴다(넘지 않는 한 작은 것이 그 뒤를 채울 수 있다).
        VectorUtil::stableSort( listOrder, [&]( uint32 lhs, uint32 rhs )
        {
            if ( listWanted[lhs] != listWanted[rhs] )
                return listWanted[lhs] < listWanted[rhs];
            return listCandidate[lhs]._idleFrames < listCandidate[rhs]._idleFrames;
        } );
        // 올리기 선 — 예산에서 여유를 뺀다. 내리기는 예산을 넘을 때라 둘 사이에서는 아무것도 바꾸지 않는다.
        const uint64 promoteBudget = budgetBytes - budgetBytes / Internal::kPercentScale * MathUtil::min( config._promoteBudgetMarginPercent, Internal::kPercentScale );
        for ( const uint32 index : listOrder )
        {
            const TextureStreamingCandidate& candidate = listCandidate[index];
            const uint32                     wanted    = listWanted[index];
            if ( wanted >= candidate._residentFirstMip || candidate._framesSinceChange < config._minFramesBetweenChanges )
                continue; // 이미 필요한 만큼 있거나(더 많은 것은 예산을 넘을 때만 내린다) 막 바꿨다
            const uint64 cost = candidate.computeResidentBytes( wanted ) - candidate.computeResidentBytes( candidate._residentFirstMip );
            if ( resident + cost > promoteBudget )
                continue;
            resident += cost;
            outListDecision.push_back( TextureStreamingDecision{ index, wanted } );
        }
    }
} // namespace sw
