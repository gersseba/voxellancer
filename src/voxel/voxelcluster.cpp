#include "voxelcluster.h"

#include <list>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <glow/ChangeListener.h>
#include <glow/logging.h>

#include "resource/clustercache.h"

#ifndef VOXELLANCER_HEADLESS_SERVER
#include "voxelrenderdata.h"
#endif
#include "voxel.h"
#include "voxelclusterbounds.h"


VoxelCluster::VoxelCluster(float scale):
#ifndef VOXELLANCER_HEADLESS_SERVER
    m_voxelRenderData(nullptr),
#endif
    m_bounds(new VoxelClusterBounds(this)),
    m_emissiveness(0),
    m_voxelRevision(0),
    m_transform(glm::vec3(0), scale)
{
}

VoxelCluster::~VoxelCluster() {
    for (auto& element : m_voxels) {
        delete element.second;
    }
}

VoxelClusterBounds& VoxelCluster::bounds() {
    return *m_bounds;
}

Transform& VoxelCluster::transform() {
    return m_transform;
}

const Transform& VoxelCluster::transform() const {
    return m_transform;
}

void VoxelCluster::setTransform(const Transform& transform) {
    m_transform = transform;
}

const glm::vec3& VoxelCluster::position() const {
    return m_transform.position();
}

const glm::quat& VoxelCluster::orientation() const {
    return m_transform.orientation();
}

Voxel* VoxelCluster::voxel(const glm::ivec3& position) {
    auto iter = m_voxels.find(position);
    return iter == m_voxels.end() ? nullptr : iter->second;
}

const Voxel* VoxelCluster::voxel(const glm::ivec3& position) const {
    auto iter = m_voxels.find(position);
    return iter == m_voxels.end() ? nullptr : iter->second;
}

void VoxelCluster::addVoxel(Voxel* voxel) {
    assert(m_voxels[voxel->cell()] == nullptr);

    m_voxels[voxel->cell()] = voxel;
    ++m_voxelRevision;
    m_bounds->addVoxel(voxel);
#ifndef VOXELLANCER_HEADLESS_SERVER
    if (m_voxelRenderData) {
        m_voxelRenderData->invalidate();
    }
#endif
}

void VoxelCluster::removeVoxel(Voxel* voxel) {
    assert(voxel != nullptr);

    m_bounds->removeVoxel(voxel); // Needs to be done before removal from m_voxels
    m_voxels.erase(voxel->cell());
    ++m_voxelRevision;
#ifndef VOXELLANCER_HEADLESS_SERVER
    if (m_voxelRenderData) {
        m_voxelRenderData->invalidate();
    }
#endif

    delete voxel;
}

#ifndef VOXELLANCER_HEADLESS_SERVER
VoxelRenderData* VoxelCluster::voxelRenderData(){
    if (!m_voxelRenderData) {
        m_voxelRenderData.reset(new VoxelRenderData(m_voxels));
    }
    return m_voxelRenderData.get();
}
#endif

const std::unordered_map<glm::ivec3, Voxel*>& VoxelCluster::voxelMap() const {
    return m_voxels;
}

int VoxelCluster::voxelCount() const {
    return m_voxels.size();
}

std::uint64_t VoxelCluster::voxelRevision() const {
    return m_voxelRevision;
}

float VoxelCluster::emissiveness() const {
    return m_emissiveness;
}

void VoxelCluster::setEmissiveness(float emissiveness) {
    m_emissiveness = emissiveness;
}
