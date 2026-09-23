/**
 * @file NullAudioSystem.h
 * @brief 오디오 출력을 지원하지 않는 플랫폼용 더미 구현입니다.
 *
 * @note Windows 에서는 쓰이지 않지만 **항상 컴파일됩니다**(`IAudioSystem.cpp` 가 무조건 include 합니다).
 *       `#else` 안에만 두면 Windows 에서 한 번도 컴파일되지 않아 조용히 썩습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Log/Logger.h"

#include "Engine/Audio/IAudioSystem.h"
#include "Engine/Resource/ResourceUtil.h"

namespace sw
{
    /**
     * @class NullAudioSystem
     * @brief 소리를 내지 않는 오디오 시스템입니다. 요청의 성공 · 실패 판정만 실제 백엔드와 같게 흉내 냅니다.
     */
    class NullAudioSystem : public IAudioSystem
    {
    public:
        /** @brief 더미 오디오 시스템을 해제합니다. */
        ~NullAudioSystem() override = default;

        /** @brief 초기화된 것으로 표시합니다. 항상 성공합니다. */
        bool initialize() override
        {
            _bInitialized = true;
            SW_LOG_INFO( "Initialized." );
            return true;
        }

        /** @brief 재생 상태를 비웁니다. */
        void shutdown() override
        {
            stopMusic();
            _bInitialized = false;
            SW_LOG_INFO( "Shut down." );
        }

        /** @brief 초기화 여부를 반환합니다. */
        bool isInitialized() const override { return _bInitialized; }

        /** @brief 할 일이 없습니다. */
        void update( float32 ) override {}

        /** @brief 리소스가 있으면 재생한 것으로 칩니다. */
        bool play( string_view path ) override
        {
            if ( hasPlayableResource( path ) == false )
                return false;
            SW_LOG_TRACE( "play: %#", path );
            return true;
        }

        /** @brief 리소스가 있으면 배경음악을 재생한 것으로 치고 경로를 기억합니다. */
        bool playMusic( string_view path ) override
        {
            if ( hasPlayableResource( path ) == false )
                return false;
            _musicPath = string( path );
            SW_LOG_TRACE( "playMusic: %#", path );
            return true;
        }

        /** @brief 기억해 둔 배경음악 경로를 지웁니다. */
        void stopMusic() override { _musicPath.clear(); }

        /** @brief 할 일이 없습니다. */
        void pauseMusic() override {}
        /** @brief 할 일이 없습니다. */
        void resumeMusic() override {}

        /** @brief 마지막으로 요청된 배경음악 경로입니다. */
        string getMusicPath() const override { return _musicPath; }

    private:
        /** @brief 빈 경로가 아니고 실제로 있는 리소스인지 봅니다. */
        static bool hasPlayableResource( string_view path )
        {
            return path.empty() == false && ResourceUtil::hasResource( path );
        }

        string _musicPath;             /**< 마지막으로 요청된 배경음악 경로입니다. */
        bool   _bInitialized{ false }; /**< initialize 가 불렸는지 여부입니다. */
    };
} // namespace sw
