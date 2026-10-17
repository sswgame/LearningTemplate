/**
 * @file PairedAnimationComponent.h
 * @brief 페어드 애니메이션을 시작하는 쪽의 컴포넌트입니다. 에셋 경로와 이 오브젝트의 역할을 데이터로 들고, 세션을 씬의 `PairedAnimationSystem` 에 맡깁니다.
 * @details 같은 오브젝트의 `SkeletalAnimatorComponent` 가 `_role` 역할로 묶이고, 나머지 역할은 부르는 쪽(상호작용, 게임플레이)이 바인딩으로 넘긴다.
 *          `startSession` · `abortSession` 은 게임 스레드, 틱 밖에서 부른다(세션이 다른 오브젝트의 애니메이터를 바꾼다). 설계는 `docs/plans/PairedAnimation.md`.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Character/PairedAnimation/PairedAnimationSession.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class PairedAnimationDef;

    /**
     * @class PairedAnimationComponent
     * @brief 파일 머리말 참고.
     */
    REFLECT( Category = "Animation", DisplayName = "Paired Animation", Tooltip = "Starts a paired animation (*.pairedanim.xml) that plays several objects on one timeline" )
    class SW_API PairedAnimationComponent : public Component
    {
    public:
        REFLECT_BODY();

        PairedAnimationComponent();
        virtual ~PairedAnimationComponent() override;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 에셋 경로를 바꾸고 다시 읽습니다. */
        void          setPairedAnimationPath( string_view path );
        const string& getPairedAnimationPath() const { return _pairedAnimationPath; }
        /** @brief 에셋을 직접 정합니다(테스트와 절차 생성용이며 저장되지 않습니다). */
        void setPairedAnimationDef( shared_ptr<const PairedAnimationDef> def ) { _def = std::move( def ); }
        /** @brief 이 오브젝트의 애니메이터가 맡을 역할을 정합니다. */
        void          setRole( string_view roleName ) { _role = string{ roleName }; }
        const string& getRole() const { return _role; }

        /**
         * @brief 세션을 시작합니다(게임 스레드, 틱 밖). 이 오브젝트의 애니메이터는 `_role` 로 묶고, @p listOtherBinding 은 다른 역할입니다.
         * @details 이미 재생 중인 이 컴포넌트의 세션이 있으면 거절합니다(`ParticipantBusy`).
         */
        [[nodiscard]] PairedAnimationStartResult startSession( const vector<PairedAnimationBinding>& listOtherBinding );
        /** @brief 이 컴포넌트가 시작한 세션을 중단합니다. */
        void abortSession();
        /** @brief 마지막으로 시작한 세션의 상태입니다. 시작하지 않았거나, 끝나 목록에서 빠졌으면 `Idle` 입니다. */
        PairedAnimationSessionState getSessionState() const;
        /** @brief 마지막으로 시작한 세션 id 입니다(없으면 0). */
        uint32 getSessionID() const { return _sessionID; }

    private:
        /** @brief 경로의 에셋을 읽습니다. 경로가 비면 코드가 넣은 에셋을 그대로 둡니다. */
        void loadDef();

        PROPERTY( Category = "Animation", DisplayName = "Paired Animation", AssetPath, Tooltip = "Roles, clips, alignment and shared notifies (*.pairedanim.xml)" )
        string _pairedAnimationPath;
        PROPERTY( Category = "Animation", DisplayName = "Role", Tooltip = "Role this object's skeletal animator plays in the paired animation" )
        string _role;

        shared_ptr<const PairedAnimationDef> _def;
        uint32                               _sessionID;
        uint8                                _bSystemUser; ///< 씬의 `PairedAnimationSystem` 사용자로 올렸다(끝에서 내린다)
    };
} // namespace sw
