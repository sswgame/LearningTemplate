#include "pch.h"

#include "GameFramework/Base/Character/Camera/PhotoModeCamera.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Base/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    PhotoModeCamera::PhotoModeCamera()
        : _settings{}
        , _rotation{}
        , _position{}
        , _yaw{ 0.0f }
        , _pitch{ 0.0f }
        , _roll{ 0.0f }
        , _fieldOfViewY{ 0.0f }
        , _orthoHeight{ 0.0f }
        , _exposureOffset{ 0.0f }
        , _bOrthographic{ false }
        , _bActive{ false }
    {
    }

    void PhotoModeCamera::begin( const CameraPose& from )
    {
        _rotation      = from._rotation;
        _position      = from._position;
        _fieldOfViewY  = from._fieldOfViewY;
        _orthoHeight   = from._orthoHeight;
        _bOrthographic = from._bOrthographic == SW_TRUE;
        // 요 · 피치 · 롤은 `makeFromYawPitchRoll` 의 역(`getEulerAngles`)이다 — 롤도 읽어야 둘러보기를 시작해도 기울기가 남는다.
        const float3 euler = from._rotation.getEulerAngles();
        _pitch             = euler._x;
        _yaw               = euler._y;
        _roll              = euler._z;
        _exposureOffset    = 0.0f;
        _bActive           = true;
    }

    void PhotoModeCamera::end()
    {
        _bActive = false;
    }

    void PhotoModeCamera::applyInput( const PhotoModeInput& input, float32 deltaTime )
    {
        if ( _bActive == false )
            return;
        const float32 elapsed = MathUtil::max( deltaTime, 0.0f );
        if ( _bOrthographic )
        {
            // 2D: 화면의 오른쪽 · 위로 옮긴다. 화면 높이에 비례해 빨라져서 확대해도 화면 기준 속도가 같다.
            const float3  right    = float3::transform( float3{ 1.0f, 0.0f, 0.0f }, _rotation );
            const float3  up       = float3::transform( float3{ 0.0f, 1.0f, 0.0f }, _rotation );
            const float32 distance = _settings._orthoPanScreensPerSecond * elapsed * _orthoHeight;
            _position              = _position + ( right * input._move._x + up * input._move._y ) * distance;
            if ( input._zoomNotches != 0.0f )
                _orthoHeight = MathUtil::clamp( _orthoHeight * MathUtil::pow( _settings._zoomStep, input._zoomNotches ), _settings._minOrthoHeight, _settings._maxOrthoHeight );
        }
        else
        {
            const bool bLooked = input._look._x != 0.0f || input._look._y != 0.0f;
            if ( bLooked )
            {
                _yaw   = MathUtil::wrapAngle( _yaw + input._look._x * _settings._lookSensitivity );
                _pitch = MathUtil::clamp( _pitch + input._look._y * _settings._lookSensitivity, -_settings._pitchLimit, _settings._pitchLimit );
            }
            if ( input._roll != 0.0f )
                _roll = MathUtil::wrapAngle( _roll + input._roll * _settings._rollSpeed * elapsed );
            // 입력이 없으면 켤 때의 방향을 그대로 둔다(피치 한계 밖이던 시점도 건드리기 전까지는 그대로).
            if ( bLooked || input._roll != 0.0f )
                _rotation = quaternion::makeFromYawPitchRoll( _yaw, _pitch, _roll );
            // 수평 이동은 요 기준(앞 +Z, 오른쪽 +X 에서 돈다), 오르내림은 월드 위다.
            const float3  forward  = float3{ MathUtil::sin( _yaw ), 0.0f, MathUtil::cos( _yaw ) };
            const float3  right    = float3{ MathUtil::cos( _yaw ), 0.0f, -MathUtil::sin( _yaw ) };
            const float3  up       = float3{ 0.0f, 1.0f, 0.0f };
            const float32 distance = _settings._moveSpeed * elapsed;
            _position              = _position + ( forward * input._move._y + right * input._move._x + up * input._rise ) * distance;
            if ( input._zoomNotches != 0.0f )
                _fieldOfViewY = MathUtil::clamp( _fieldOfViewY * MathUtil::pow( _settings._zoomStep, input._zoomNotches ), _settings._minFieldOfViewY, _settings._maxFieldOfViewY );
        }
        if ( input._exposureSteps != 0.0f )
            _exposureOffset = MathUtil::clamp( _exposureOffset + input._exposureSteps * _settings._exposureStep, -_settings._maxExposureOffset, _settings._maxExposureOffset );
    }

    CameraPose PhotoModeCamera::makePose( const CameraPose& base ) const
    {
        if ( _bActive == false )
            return base;
        CameraPose pose    = base;
        pose._position     = _position;
        pose._rotation     = _rotation;
        pose._fieldOfViewY = _fieldOfViewY;
        pose._orthoHeight  = _orthoHeight;
        pose._postProcess._exposureCompensation += _exposureOffset;
        return pose;
    }

    namespace
    {
        /**
         * @brief 개발 명령 `photo.on` · `photo.off` · `photo.shot` — 활성 씬의 게임 카메라 디렉터에 포토 모드를 켜고 끄며 화면을 찍습니다.
         * @details 찍기는 엔진의 `screenshot` 명령을 레지스트리로 불러 같은 경로 규칙(`Saved/Screenshots/<시각>.png`)을 쓴다 — 렌더 스레드를 직접 알지 않는다.
         */
        struct PhotoModeDevCommandsInternal
        {
            /** @brief 활성 씬의 오브젝트 매니저입니다. 없으면 nullptr 입니다. */
            static GameObjectManager* findActiveManager()
            {
                if ( game::findSubsystem<SceneManager>() == nullptr )
                    return nullptr;
                SceneManager* pSceneManager = game::findSubsystem<SceneManager>();
                if ( pSceneManager == nullptr )
                    return nullptr;
                Scene* pScene = pSceneManager->getActiveScene();
                return pScene != nullptr ? pScene->getObjectManager() : nullptr;
            }

            /** @brief 게임 시점 카메라(`CameraRole::Game`)를 모는 첫 디렉터입니다. @p bInPhotoMode 이면 포토 모드가 켜진 것만 봅니다. */
            static CameraDirectorComponent* findDirector( bool bInPhotoMode )
            {
                GameObjectManager* pManager = findActiveManager();
                if ( pManager == nullptr )
                    return nullptr;
                for ( CameraDirectorComponent* pDirector : pManager->getComponentRegistry().getAll<CameraDirectorComponent>() )
                {
                    const GameObject*      pOwner  = pDirector->getOwner();
                    const CameraComponent* pCamera = pOwner != nullptr ? pOwner->getComponent<CameraComponent>() : nullptr;
                    if ( pCamera == nullptr || pCamera->getRole() != CameraRole::Game )
                        continue;
                    if ( bInPhotoMode && pDirector->isInPhotoMode() == false )
                        continue;
                    return pDirector;
                }
                return nullptr;
            }

            static bool runOn( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                CameraDirectorComponent* pDirector = findDirector( false );
                if ( pDirector == nullptr )
                {
                    outReply = "photo mode: no game camera director in the active scene";
                    return true;
                }
                if ( pDirector->isInPhotoMode() )
                {
                    outReply = "photo mode: already on";
                    return true;
                }
                outReply = pDirector->enterPhotoMode() ? "photo mode: on (game paused)" : "photo mode: not entered - no pose yet or the photo mode input map did not load (see the log)";
                return true;
            }

            static bool runOff( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                CameraDirectorComponent* pDirector = findDirector( true );
                if ( pDirector == nullptr )
                {
                    outReply = "photo mode: already off";
                    return true;
                }
                pDirector->exitPhotoMode();
                outReply = "photo mode: off";
                return true;
            }

            static bool runShot( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                const DevCommandRegistration* pScreenshot = DevCommandRegistry::get().findCommand( "screenshot" );
                if ( pScreenshot == nullptr )
                {
                    outReply = "photo mode: the screenshot command is not registered";
                    return true;
                }
                return pScreenshot->_pFunc( listArgument, outReply );
            }
        };
    } // namespace

    SW_DEV_COMMAND( PhotoOn, "photo.on", "photo.on", "Pause the game and fly a free camera from the current view (photo mode)", &PhotoModeDevCommandsInternal::runOn );
    SW_DEV_COMMAND( PhotoOff, "photo.off", "photo.off", "Leave photo mode and restore the game camera and time", &PhotoModeDevCommandsInternal::runOff );
    SW_DEV_COMMAND( PhotoShot, "photo.shot", "photo.shot [file]", "Save the next frame as PNG (same as screenshot)", &PhotoModeDevCommandsInternal::runShot );
} // namespace sw
