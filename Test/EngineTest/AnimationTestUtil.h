/**
 * @file AnimationTestUtil.h
 * @brief 애니메이션 시험이 함께 쓰는 재생할 것 · 풀이 · 손으로 만든 스켈레톤과 원본 클립입니다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimPlayback.h"
#include "Engine/Animation/Codec/AnimCodec.h"
#include "Engine/Animation/Graph/AnimGraphPlayer.h"
#include "Engine/Animation/Skeletal/Skeleton.h"

namespace test
{
    /**
     * @class TestPlayable
     * @brief 길이 · 반복 · 알림만 가진 재생할 것입니다.
     */
    class TestPlayable final : public sw::IAnimPlayable
    {
    public:
        TestPlayable( float32 playLength, bool bLoop )
            : _notifyTrack{}
            , _playLength{ playLength }
            , _bLoop{ bLoop }
        {
        }

        float32                    getPlayLength() const override { return _playLength; }
        bool                       isLoopingByDefault() const override { return _bLoop; }
        const sw::AnimNotifyTrack* findNotifyTrack() const override { return _notifyTrack.isEmpty() ? nullptr : &_notifyTrack; }
        void                       addNotify( const utf8* pName, float32 time ) { _notifyTrack.addEvent( sw::AnimNotifyEvent{ sw::hashed_string( pName ), time, 0.0f } ); }

    private:
        sw::AnimNotifyTrack _notifyTrack;
        float32             _playLength;
        bool                _bLoop;
    };
} // namespace test

namespace test
{
    /**
     * @class TestPlayableSource
     * @brief 이름 → 재생할 것 표입니다.
     */
    class TestPlayableSource final : public sw::IAnimPlayableSource
    {
    public:
        void                     add( const utf8* pName, const sw::IAnimPlayable* pPlayable ) { _mapPlayable[sw::hashed_string( pName )] = pPlayable; }
        const sw::IAnimPlayable* findPlayable( const sw::hashed_string& name ) const override
        {
            const auto it = _mapPlayable.find( name );
            return it != _mapPlayable.end() ? it->second : nullptr;
        }

    private:
        sw::unordered_map<sw::hashed_string, const sw::IAnimPlayable*> _mapPlayable;
    };

    /** @brief 본 변환 하나를 만듭니다. */
    inline sw::BoneTransform makeBoneTransform( const sw::float3& translation, const sw::quaternion& rotation = sw::quaternion::Identity )
    {
        sw::BoneTransform transform{};
        transform._translation = translation;
        transform._rotation    = rotation;
        return transform;
    }

    /** @brief 사슬 스켈레톤(root → 1 → 2 …)입니다. 본마다 Y 로 1 m 위, 역 바인드는 레퍼런스에서 구합니다. */
    inline sw::Skeleton makeChainSkeleton( uint32 boneCount )
    {
        sw::Skeleton skeleton;
        for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
        {
            const sw::string name = sw::string( "bone" ) + sw::to_string( boneIndex ).c_str();
            const sw::float3 offset{ 0.0f, boneIndex == 0 ? 0.0f : 1.0f, 0.0f };
            (void)skeleton.addBone( sw::hashed_string( name ), static_cast<int32>( boneIndex ) - 1, makeBoneTransform( offset ), sw::float4x4::Identity );
        }
        skeleton.computeInverseBindFromReference();
        return skeleton;
    }

    /**
     * @brief 사슬 스켈레톤의 원본 클립입니다 — 루트는 +Z 로 1 m/s, 다른 본은 Z 축으로 사인 회전(진폭 @p amplitude 라디안).
     * @param sampleCount 표본 수(길이 = (표본 - 1) / 표본율).
     */
    inline sw::AnimRawClip makeChainRawClip( const sw::Skeleton& skeleton, uint32 sampleCount, float32 sampleRate, float32 amplitude )
    {
        sw::AnimRawClip clip;
        clip._sampleRate       = sampleRate;
        clip._sampleCount      = sampleCount;
        const uint32 boneCount = skeleton.getBoneCount();
        for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
        {
            clip._listTrackName.push_back( skeleton.getBone( boneIndex )._name );
            clip._listTrackParent.push_back( skeleton.getBone( boneIndex )._parentIndex );
        }
        for ( uint32 sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex )
        {
            const float32 time = static_cast<float32>( sampleIndex ) / sampleRate;
            for ( uint32 boneIndex = 0; boneIndex < boneCount; ++boneIndex )
            {
                sw::BoneTransform transform = skeleton.getBone( boneIndex )._referencePose;
                if ( boneIndex == 0 )
                    transform._translation = sw::float3{ 0.0f, 0.0f, time };
                else
                    transform._rotation = sw::quaternion::makeFromAxisAngle( sw::float3{ 0.0f, 0.0f, 1.0f },
                                                                             amplitude * sw::MathUtil::sin( time * 6.0f + static_cast<float32>( boneIndex ) ) );
                clip._listSample.push_back( transform );
            }
        }
        return clip;
    }
} // namespace test
