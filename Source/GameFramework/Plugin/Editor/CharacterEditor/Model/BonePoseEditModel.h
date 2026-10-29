/**
 * @file BonePoseEditModel.h
 * @brief 스켈레톤(`.skeleton.xml`)을 읽어 본 목록 · 본 트리를 주고, 레퍼런스 포즈 덮어쓰기(`*.refpose.xml`)를 편집하는 모델입니다. ImGui 와 씬을 모릅니다.
 * @details 스켈레톤은 재임포트가 다시 쓰는 산출물이라 **읽기만** 한다. 편집은 `ReferencePoseOverride` 에만 쌓고 파일로 쓴다(적은 필드만 쓴다 — 위치만 고치면 회전 · 스케일은 임포트 값).
 *          본의 "지금 값"(`getEffectiveTransform`)은 스켈레톤의 레퍼런스 포즈 위에 덮어쓰기를 얹은 결과이다. 좌우 짝은 이름 규칙(`MirrorNameRules`)으로 찾아 쌍으로 등록한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/FileSystem/AssetID.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/CharacterMirrorUtil.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/Model/EditSnapshotHistory.h"
#include "GameFramework/Plugin/Editor/CharacterEditor/Model/ReferencePoseOverride.h"

namespace sw::editor
{
    /**
     * @class BonePoseEditModel
     * @brief 스켈레톤 하나(읽기 전용)와 레퍼런스 포즈 덮어쓰기 하나(편집 대상), 그 편집 기록입니다.
     */
    class BonePoseEditModel
    {
    public:
        BonePoseEditModel();

        // --- 스켈레톤 ------------------------------------------------------------------------------------------------

        /** @brief 스켈레톤을 정합니다(테스트와 이미 읽은 것을 넘길 때). 덮어쓰기는 그대로 둡니다. */
        void setSkeleton( const Skeleton& skeleton ) { _skeleton = skeleton; }
        /** @brief 리소스 경로(또는 절대 경로)의 스켈레톤을 읽습니다. 실패하면 현재 스켈레톤을 그대로 두고 false 입니다. */
        [[nodiscard]] bool loadSkeleton( const AssetID& assetID );
        const Skeleton&    getSkeleton() const { return _skeleton; }
        /** @brief 본 이름을 스켈레톤 순서로 모읍니다(`SocketEditModel::setBoneNames` 의 입력). */
        void collectBoneNames( vector<hashed_string>& outListBoneName ) const;
        /** @brief 본의 자식 인덱스를 스켈레톤 순서로 모읍니다. @p parentIndex 가 -1 이면 루트들입니다(본 트리를 그릴 때). */
        void collectChildren( int32 parentIndex, vector<uint32>& outListChild ) const;

        // --- 포즈 ----------------------------------------------------------------------------------------------------

        /** @brief 본의 지금 로컬 변환입니다 — 임포트 레퍼런스 위에 덮어쓰기를 얹은 값. 본이 없으면 false 입니다. */
        [[nodiscard]] bool getEffectiveTransform( const hashed_string& bone, BoneTransform& outTransform ) const;
        /** @brief 본에 덮어쓰기가 있으면 true 입니다. */
        bool                         hasOverride( const hashed_string& bone ) const { return _override.findOverride( bone ) != nullptr; }
        const ReferencePoseOverride& getOverride() const { return _override; }

        // --- 편집 ----------------------------------------------------------------------------------------------------
        // 실패하면(없는 본) 아무것도 바꾸지 않고 기록도 남기지 않으며, 이유를 영어 한 문장으로 @p outError 에 담습니다.
        // 지금 값과 같으면 false 이고 오류는 비어 있습니다. bContinuous 는 끌기 한 번을 되돌리기 한 단계로 합칩니다(`endContinuousEdit`).

        [[nodiscard]] bool setTranslation( const hashed_string& bone, const float3& translation, bool bContinuous, string& outError );
        /** @brief 회전을 도(`makeFromYawPitchRoll` 와 같은 배치)로 정합니다. */
        [[nodiscard]] bool setRotationDegrees( const hashed_string& bone, const float3& degrees, bool bContinuous, string& outError );
        [[nodiscard]] bool setScale( const hashed_string& bone, const float3& scale, bool bContinuous, string& outError );
        /** @brief 본의 덮어쓰기를 지웁니다(임포트 값으로). 덮어쓰기가 없으면 false 입니다. */
        [[nodiscard]] bool clearBone( const hashed_string& bone, string& outError );
        /**
         * @brief 본의 덮어쓰기를 반대편 본에 뒤집어 적습니다. 반대편 이름은 @p rules 로 찾고, 스켈레톤에 있어야 합니다. 짝을 쌍으로 등록합니다.
         * @param outMirrorBone 반대편 본 이름입니다.
         */
        [[nodiscard]] bool mirrorBone( const hashed_string& bone, const MirrorNameRules& rules, hashed_string& outMirrorBone, string& outError );
        void               setMirrorAxis( MirrorAxis axis ) { _override.setMirrorAxis( static_cast<uint8>( axis ) ); }
        void               endContinuousEdit() { _history.endCoalescing(); }

        // --- 읽기 · 쓰기 · 되돌리기 ----------------------------------------------------------------------------------

        /** @brief XML 에서 읽어 덮어쓰기를 바꿉니다. 실패하면 그대로 두고 false 입니다. 성공하면 기록을 지우고 "저장된 상태" 로 맞춥니다. */
        [[nodiscard]] bool loadFromXMLText( string_view xmlText, string_view sourceName );
        [[nodiscard]] bool loadFromFile( string_view absolutePath );
        /** @brief 덮어쓰기를 비웁니다. 기록을 지우고 "저장된 상태" 로 맞춥니다. */
        void   clear();
        string saveToXMLText() const { return _override.saveToXMLText(); }
        /** @brief 파일에 씁니다. 성공하면 "저장된 상태" 로 맞춥니다. */
        [[nodiscard]] bool saveToFile( string_view absolutePath );
        bool               isDirty() const { return ( _override.saveToXMLText() == _savedText ) == false; }
        /** @brief 편집을 버리고 마지막으로 읽거나 저장한 상태로 돌아갑니다(창의 "Don't Save"). 기록을 지웁니다. 그 글을 다시 읽지 못하면 그대로 두고 false 입니다. */
        [[nodiscard]] bool revertToSaved() { return loadFromXMLText( string( _savedText ), "(saved reference pose state)" ); }

        [[nodiscard]] bool undo();
        [[nodiscard]] bool redo();
        bool               canUndo() const { return _history.canUndo(); }
        bool               canRedo() const { return _history.canRedo(); }

    private:
        /** @brief 본의 지금 값을 구합니다. 스켈레톤에 없으면 오류를 쓰고 false 입니다. */
        [[nodiscard]] bool findCurrent( const hashed_string& bone, BoneTransform& outCurrent, string& outError ) const;
        /** @brief 편집 직전에 부릅니다. @p fieldName 이 있으면 같은 본 · 필드의 연속 편집을 한 단계로 합칩니다. */
        void recordUndo( const hashed_string& bone, string_view fieldName );

        Skeleton                                   _skeleton;
        ReferencePoseOverride                      _override;
        string                                     _savedText;
        EditSnapshotHistory<ReferencePoseOverride> _history;
    };
} // namespace sw::editor
