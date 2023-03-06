/**
 *
 *    Copyright (c) 2022 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#pragma once
#include <app/clusters/scenes/SceneTable.h>
#include <lib/core/DataModelTypes.h>
#include <lib/support/CommonIterator.h>
#include <lib/support/PersistentData.h>
#include <lib/support/Pool.h>

namespace chip {
namespace scenes {

using clusterId = chip::ClusterId;

/// @brief Default implementation of handler, handle EFS from add scene and view scene commands for any cluster
///        The implementation of SerializeSave and ApplyScene were omitted and must be implemented in a way that
///        is compatible with the SerializeAdd output in order to function with the Default Scene Handler.
///        It is worth noting that this implementation is very memory consuming. In the current worst case,
///        (Color control cluster), the Extension Field Set's value pair list TLV occupies 99 bytes of memory
class DefaultSceneHandlerImpl : public scenes::SceneHandler
{
public:
    static constexpr uint8_t kMaxValueSize = 4;
    static constexpr uint8_t kMaxAvPair    = 15;

    DefaultSceneHandlerImpl() = default;
    ~DefaultSceneHandlerImpl() override{};

    /// @brief Function to serialize data from an add scene command, assume the incoming extensionFieldSet is initialized
    /// @param endpoint Target Endpoint
    /// @param cluster Cluster in the Extension field set, filled by the function
    /// @param serialisedBytes Mutable Byte span to hold EFS data from command
    /// @param extensionFieldSet Extension field set from commmand, pre initialized
    /// @return CHIP_NO_ERROR if success, specific CHIP_ERROR otherwise
    virtual CHIP_ERROR SerializeAdd(EndpointId endpoint, ClusterId & cluster, MutableByteSpan & serialisedBytes,
                                    app::Clusters::Scenes::Structs::ExtensionFieldSet::DecodableType & extensionFieldSet) override
    {
        app::DataModel::List<app::Clusters::Scenes::Structs::AttributeValuePair::Type> attributeValueList;
        app::Clusters::Scenes::Structs::AttributeValuePair::DecodableType aVPair;
        TLV::TLVWriter writer;
        TLV::TLVType outer;

        uint8_t pairCount  = 0;
        uint8_t valueBytes = 0;

        VerifyOrReturnError(SupportsCluster(endpoint, extensionFieldSet.clusterID), CHIP_ERROR_INVALID_ARGUMENT);

        cluster = extensionFieldSet.clusterID;

        auto pair_iterator = extensionFieldSet.attributeValueList.begin();
        while (pair_iterator.Next() && pairCount < kMaxAvPair)
        {
            aVPair                          = pair_iterator.GetValue();
            mAVPairs[pairCount].attributeID = aVPair.attributeID;
            auto value_iterator             = aVPair.attributeValue.begin();

            valueBytes = 0;
            while (value_iterator.Next() && valueBytes < kMaxValueSize)
            {
                mValueBuffer[pairCount][valueBytes] = value_iterator.GetValue();
                valueBytes++;
            }
            // Check we could go through all bytes of the value
            VerifyOrReturnError(value_iterator.Next() == false, CHIP_ERROR_BUFFER_TOO_SMALL);
            mAVPairs[pairCount].attributeValue = mValueBuffer[pairCount];
            mAVPairs[pairCount].attributeValue.reduce_size(valueBytes);
            pairCount++;
        }

        // Check we could go through all pairs in incomming command
        VerifyOrReturnError(pair_iterator.Next() == false, CHIP_ERROR_BUFFER_TOO_SMALL);

        attributeValueList = mAVPairs;
        attributeValueList.reduce_size(pairCount);

        writer.Init(serialisedBytes);
        ReturnErrorOnFailure(writer.StartContainer(TLV::AnonymousTag(), TLV::kTLVType_Structure, outer));
        ReturnErrorOnFailure(app::DataModel::Encode(
            writer, TLV::ContextTag(to_underlying(app::Clusters::Scenes::Structs::ExtensionFieldSet::Fields::kAttributeValueList)),
            attributeValueList));
        ReturnErrorOnFailure(writer.EndContainer(outer));

        return CHIP_NO_ERROR;
    }

    /// @brief Simulates taking data from nvm and loading it in a command object if the cluster is supported by the endpoint
    /// @param endpoint target endpoint
    /// @param cluster  target cluster
    /// @param serialisedBytes data to deserialize into EFS
    /// @return CHIP_NO_ERROR if Extension Field Set was successfully populated, specific CHIP_ERROR otherwise
    virtual CHIP_ERROR Deserialize(EndpointId endpoint, ClusterId cluster, ByteSpan & serialisedBytes,
                                   app::Clusters::Scenes::Structs::ExtensionFieldSet::Type & extensionFieldSet) override
    {
        app::DataModel::DecodableList<app::Clusters::Scenes::Structs::AttributeValuePair::DecodableType> attributeValueList;
        app::Clusters::Scenes::Structs::AttributeValuePair::DecodableType decodePair;

        TLV::TLVReader reader;
        TLV::TLVType outer;
        uint8_t pairCount  = 0;
        uint8_t valueBytes = 0;

        VerifyOrReturnError(SupportsCluster(endpoint, cluster), CHIP_ERROR_INVALID_ARGUMENT);

        extensionFieldSet.clusterID = cluster;
        reader.Init(serialisedBytes);
        ReturnErrorOnFailure(reader.Next());
        ReturnErrorOnFailure(reader.EnterContainer(outer));
        ReturnErrorOnFailure(reader.Next());
        attributeValueList.Decode(reader);

        auto pair_iterator = attributeValueList.begin();
        while (pair_iterator.Next() && pairCount < kMaxAvPair)
        {
            decodePair                      = pair_iterator.GetValue();
            mAVPairs[pairCount].attributeID = decodePair.attributeID;
            auto value_iterator             = decodePair.attributeValue.begin();
            valueBytes                      = 0;

            while (value_iterator.Next() && valueBytes < kMaxValueSize)
            {
                mValueBuffer[pairCount][valueBytes] = value_iterator.GetValue();
                valueBytes++;
            }
            // Check we could go through all bytes of the value
            VerifyOrReturnError(value_iterator.Next() == false, CHIP_ERROR_BUFFER_TOO_SMALL);
            mAVPairs[pairCount].attributeValue = mValueBuffer[pairCount];
            mAVPairs[pairCount].attributeValue.reduce_size(valueBytes);
            pairCount++;
        };

        // Check we could go through all pairs stored in memory
        VerifyOrReturnError(pair_iterator.Next() == false, CHIP_ERROR_BUFFER_TOO_SMALL);
        ReturnErrorOnFailure(reader.ExitContainer(outer));

        extensionFieldSet.attributeValueList = mAVPairs;
        extensionFieldSet.attributeValueList.reduce_size(pairCount);

        return CHIP_NO_ERROR;
    }

private:
    app::Clusters::Scenes::Structs::AttributeValuePair::Type mAVPairs[kMaxAvPair];
    uint8_t mValueBuffer[kMaxAvPair][kMaxValueSize];
};

/**
 * @brief Implementation of a storage in nonvolatile storage of the scene table.
 *
 * DefaultSceneTableImpl is an implementation that allows to store scenes using PersistentStorageDelegate.
 * It handles the storage of scenes by their ID, GroupID and EnpointID over multiple fabrics.
 * It is meant to be used exclusively when the scene cluster is enable for at least one endpoint
 * on the device.
 */
class DefaultSceneTableImpl : public SceneTable
{
public:
    DefaultSceneTableImpl() = default;

    ~DefaultSceneTableImpl() override {}

    CHIP_ERROR Init(PersistentStorageDelegate * storage) override;
    void Finish() override;

    // Scene access by Id
    CHIP_ERROR SetSceneTableEntry(FabricIndex fabric_index, const SceneTableEntry & entry) override;
    CHIP_ERROR GetSceneTableEntry(FabricIndex fabric_index, SceneStorageId scene_id, SceneTableEntry & entry) override;
    CHIP_ERROR RemoveSceneTableEntry(FabricIndex fabric_index, SceneStorageId scene_id) override;
    CHIP_ERROR RemoveSceneTableEntryAtPosition(FabricIndex fabric_index, SceneIndex scened_idx) override;

    // SceneHandlers
    CHIP_ERROR RegisterHandler(SceneHandler * handler) override;
    CHIP_ERROR UnregisterHandler(SceneHandler * handler) override;
    CHIP_ERROR UnregisterAllHandler() override;

    // Extension field sets operation
    CHIP_ERROR SceneSaveEFS(SceneTableEntry & scene) override;
    CHIP_ERROR SceneApplyEFS(FabricIndex fabric_index, const SceneStorageId & scene_id) override;

    // Fabrics
    CHIP_ERROR RemoveFabric(FabricIndex fabric_index) override;

    // Iterators
    SceneEntryIterator * IterateSceneEntry(FabricIndex fabric_index) override;

protected:
    class SceneEntryIteratorImpl : public SceneEntryIterator
    {
    public:
        SceneEntryIteratorImpl(DefaultSceneTableImpl & provider, FabricIndex fabric_index);
        size_t Count() override;
        bool Next(SceneTableEntry & output) override;
        void Release() override;

    protected:
        DefaultSceneTableImpl & mProvider;
        FabricIndex mFabric = kUndefinedFabricIndex;
        SceneIndex mNextSceneIdx;
        SceneIndex mSceneIndex = 0;
        uint8_t mTotalScene    = 0;
    };
    bool IsInitialized() { return (mStorage != nullptr); }

    chip::PersistentStorageDelegate * mStorage = nullptr;
    ObjectPool<SceneEntryIteratorImpl, kIteratorsMax> mSceneEntryIterators;
}; // class DefaultSceneTableImpl

} // namespace scenes
} // namespace chip