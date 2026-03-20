#include "SceneGraph.h"

#include <algorithm>
#include <cmath>

namespace {
/**
 * @brief Extracts translation from transform matrix
 * @param transform Matrix to inspect
 * @return Translation vector
 */
glm::vec3 matrix_translation(const glm::mat4& transform) {
    return glm::vec3(transform[3][0], transform[3][1], transform[3][2]);
}

/**
 * @brief Computes a sphere-backed AABB from a transform and radius
 * @param transform World transform containing the sphere center
 * @param radius Sphere radius
 * @return Pair of minimum and maximum world-space bounds
 */
std::pair<glm::vec3, glm::vec3> sphere_bounds(const glm::mat4& transform, float radius) {
    // extract the world-space center of the object from the matrix translation column
    const glm::vec3 center = matrix_translation(transform);
    // turn the scalar sphere radius into a per-axis extent for AABB expansion
    const glm::vec3 extent(std::max(radius, 0.0f));
    // min corner is center minus the extent on each axis
    // max corner is center plus the extent on each axis
    return std::make_pair(center - extent, center + extent);
}

/**
 * @brief Computes the squared XZ distance from a point to an AABB
 * @param point Query point in world space
 * @param min_bounds Inclusive minimum AABB corner
 * @param max_bounds Inclusive maximum AABB corner
 * @return Squared distance in the XZ plane
 */
float squared_distance_to_aabb_xz(const glm::vec3& point,
                                  const glm::vec3& min_bounds,
                                  const glm::vec3& max_bounds) {
    // dx stores horizontal separation in X between the point and the box
    float dx = 0.0f;
    if (point.x < min_bounds.x) {
        // point lies left of the box, so measure distance to the left face
        dx = min_bounds.x - point.x;
    } else if (point.x > max_bounds.x) {
        // point lies right of the box, so measure distance to the right face
        dx = point.x - max_bounds.x;
    }

    // dz stores horizontal separation in Z between the point and the box
    float dz = 0.0f;
    if (point.z < min_bounds.z) {
        // point lies in front of the box on Z, so measure to the near face
        dz = min_bounds.z - point.z;
    } else if (point.z > max_bounds.z) {
        // point lies behind the box on Z, so measure to the far face
        dz = point.z - max_bounds.z;
    }

    // squared distance avoids a square root and is enough for radius comparisons
    return dx * dx + dz * dz;
}

/**
 * @brief Tests XZ overlap between two AABBs
 * @param min_a Inclusive minimum corner for the first box
 * @param max_a Inclusive maximum corner for the first box
 * @param min_b Inclusive minimum corner for the second box
 * @param max_b Inclusive maximum corner for the second box
 * @return True when the XZ projections overlap
 */
bool overlaps_aabb_xz(const glm::vec3& min_a,
                      const glm::vec3& max_a,
                      const glm::vec3& min_b,
                      const glm::vec3& max_b) {
    // overlap exists only when the projections overlap on both X and Z
    return (max_a.x >= min_b.x) &&
           (min_a.x <= max_b.x) &&
           (max_a.z >= min_b.z) &&
           (min_a.z <= max_b.z);
}
}  // namespace

SceneGraph::SceneGraph()
    : max_leaf_objects_(2),
      nodes_(),
      free_node_ids_(),
      object_to_node_(),
      spatial_object_nodes_(),
      bvh_nodes_() {
    // reserve node 0 as a permanent root so hierarchy code always has a valid anchor
    SceneNode root{};
    root.id = 0;
    root.parent = 0;
    root.children.clear();
    root.object_reference = std::nullopt;
    root.local_transform = glm::mat4(1.0f);
    root.world_transform = glm::mat4(1.0f);
    root.bounding_radius = 0.0f;
    root.active = true;
    nodes_.push_back(root);
}

SceneNodeId SceneGraph::rootNodeId() const {
    // node 0 is always the synthetic root created in the constructor
    return 0;
}

void SceneGraph::setMaxLeafObjects(std::size_t max_leaf_objects) {
    if (max_leaf_objects == 0) {
        return;
    }
    // changing the leaf size changes how aggressively the BVH splits
    max_leaf_objects_ = max_leaf_objects;
    rebuildSpatialIndex();
}

std::size_t SceneGraph::maxLeafObjects() const {
    return max_leaf_objects_;
}

SceneNodeId SceneGraph::createNode(SceneNodeId parent,
                                   std::optional<std::uint32_t> object_reference,
                                   const glm::mat4& local_transform,
                                   float bounding_radius) {
    if (!isNodeActive(parent)) {
        // invalid parents fall back to the root so callers do not create dangling nodes
        parent = rootNodeId();
    }

    if (object_reference.has_value()) {
        const auto existing_it = object_to_node_.find(object_reference.value());
        if (existing_it != object_to_node_.end() && isNodeActive(existing_it->second)) {
            const SceneNodeId existing_id = existing_it->second;
            // object ids stay unique, so reusing an existing id updates the old node in place
            nodes_[existing_id].local_transform = local_transform;
            nodes_[existing_id].bounding_radius = std::max(0.0f, bounding_radius);
            setParent(existing_id, parent);
            return existing_id;
        }
    }

    SceneNode node{};
    if (!free_node_ids_.empty()) {
        // recycle dead node slots to avoid unbounded growth when objects are created and destroyed
        node.id = free_node_ids_.back();
        free_node_ids_.pop_back();
    } else {
        node.id = static_cast<SceneNodeId>(nodes_.size());
    }
    node.parent = parent;
    node.children.clear();
    node.object_reference = object_reference;
    node.local_transform = local_transform;
    node.world_transform = local_transform;
    node.bounding_radius = std::max(0.0f, bounding_radius);
    node.active = true;
    if (node.id < nodes_.size()) {
        // overwrite the recycled slot with the new node payload
        nodes_[node.id] = node;
    } else {
        nodes_.push_back(node);
    }
    // parent owns the child link used by hierarchy traversal
    nodes_[parent].children.push_back(node.id);

    if (object_reference.has_value()) {
        // this map gives fast lookup from game object id to scene node id
        object_to_node_[object_reference.value()] = node.id;
    }
    return node.id;
}

bool SceneGraph::setParent(SceneNodeId child, SceneNodeId new_parent) {
    if (!isNodeActive(child) || child == rootNodeId()) {
        // inactive nodes and the root itself cannot be reparented
        return false;
    }
    if (!isNodeActive(new_parent)) {
        // invalid targets collapse to the root instead of failing hard
        new_parent = rootNodeId();
    }
    if (child == new_parent) {
        return false;
    }
    if (isAncestor(child, new_parent)) {
        // prevent cycles so the hierarchy always remains a real tree
        return false;
    }

    SceneNode& child_node = nodes_[child];
    if (child_node.parent == new_parent) {
        // no work needed when the requested relationship already exists
        return true;
    }

    if (isNodeActive(child_node.parent)) {
        SceneNode& old_parent = nodes_[child_node.parent];
        // remove the old parent link before adding the new one
        old_parent.children.erase(
            std::remove(old_parent.children.begin(), old_parent.children.end(), child),
            old_parent.children.end());
    }

    child_node.parent = new_parent;
    // add the child to the destination parent list so future transform recursion sees it
    nodes_[new_parent].children.push_back(child);
    return true;
}

bool SceneGraph::setParentByObject(std::uint32_t child_object_reference,
                                   std::optional<std::uint32_t> parent_object_reference) {
    const auto child_it = object_to_node_.find(child_object_reference);
    if (child_it == object_to_node_.end() || !isNodeActive(child_it->second)) {
        return false;
    }

    SceneNodeId parent_id = rootNodeId();
    if (parent_object_reference.has_value()) {
        const auto parent_it = object_to_node_.find(parent_object_reference.value());
        if (parent_it == object_to_node_.end() || !isNodeActive(parent_it->second)) {
            return false;
        }
        parent_id = parent_it->second;
    }
    return setParent(child_it->second, parent_id);
}

bool SceneGraph::removeNodeByObject(std::uint32_t object_reference) {
    const auto it = object_to_node_.find(object_reference);
    if (it == object_to_node_.end()) {
        return false;
    }
    // removal is recursive because children inherit transforms from the removed parent
    removeNodeRecursive(it->second);
    return true;
}

bool SceneGraph::setLocalTransformByObject(std::uint32_t object_reference, const glm::mat4& local_transform) {
    const auto it = object_to_node_.find(object_reference);
    if (it == object_to_node_.end()) {
        return false;
    }
    const SceneNodeId node_id = it->second;
    if (node_id >= nodes_.size() || !nodes_[node_id].active) {
        return false;
    }
    // local transform updates are cheap, world transforms are refreshed in updateWorldTransforms
    nodes_[node_id].local_transform = local_transform;
    return true;
}

bool SceneGraph::setBoundingRadiusByObject(std::uint32_t object_reference, float bounding_radius) {
    const auto it = object_to_node_.find(object_reference);
    if (it == object_to_node_.end()) {
        return false;
    }
    const SceneNodeId node_id = it->second;
    if (!isNodeActive(node_id)) {
        return false;
    }
    // broad-phase queries rely on this radius when building the BVH object bounds
    nodes_[node_id].bounding_radius = std::max(0.0f, bounding_radius);
    return true;
}

void SceneGraph::updateWorldTransforms() {
    // start at the synthetic root with identity so each subtree accumulates parent transforms
    updateWorldRecursive(rootNodeId(), glm::mat4(1.0f));
}

void SceneGraph::rebuildSpatialIndex() {
    // rebuild from scratch each frame because object positions can change after transform updates
    // for this assignment scale that is simpler and easier to reason about than incremental BVH edits
    // conceptually this turns a flat list of active objects into nested spatial boxes
    // large parent boxes cover many objects and small child boxes cover localized subsets
    // clear the previous frame's flat object list
    spatial_object_nodes_.clear();
    // clear the previous frame's BVH nodes
    bvh_nodes_.clear();
    for (const SceneNode& node : nodes_) {
        if (!node.active || !node.object_reference.has_value()) {
            // skip helper nodes or dead nodes because they are not renderable spatial objects
            continue;
        }
        // only object-backed nodes enter the BVH because empty transform helpers do not need culling
        // store the scene node id rather than copying the whole scene node
        spatial_object_nodes_.push_back(node.id);
    }

    if (spatial_object_nodes_.empty()) {
        // no active spatial objects means there is no BVH to build this frame
        return;
    }

    // a binary tree storing N leaves needs fewer than 2N nodes so reserve avoids vector churn
    bvh_nodes_.reserve(spatial_object_nodes_.size() * 2);
    // the first built node becomes the BVH root at index 0
    // the recursive builder will keep subdividing until the leaf policy is satisfied
    buildBvhRecursive(0, spatial_object_nodes_.size());
}

void SceneGraph::render(std::vector<std::uint32_t>& render_queue,
                        const glm::vec3& camera_position,
                        float cull_radius) const {
    queryRadius(render_queue, camera_position, cull_radius);
}

void SceneGraph::queryRadius(std::vector<std::uint32_t>& out_objects,
                             const glm::vec3& center,
                             float radius) const {
    out_objects.clear();
    if (bvh_nodes_.empty() || radius < 0.0f) {
        return;
    }
    queryRadiusRecursive(out_objects, 0, center, radius);
}

void SceneGraph::queryAabb(std::vector<std::uint32_t>& out_objects,
                           const glm::vec2& min_xz,
                           const glm::vec2& max_xz) const {
    out_objects.clear();
    if (bvh_nodes_.empty()) {
        return;
    }
    queryAabbRecursive(out_objects, 0, min_xz, max_xz);
}

glm::mat4 SceneGraph::worldTransformForObject(std::uint32_t object_reference) const {
    const auto it = object_to_node_.find(object_reference);
    if (it == object_to_node_.end()) {
        // callers get identity when the object is not currently mapped into the hierarchy
        return glm::mat4(1.0f);
    }
    const SceneNodeId node_id = it->second;
    if (node_id >= nodes_.size() || !nodes_[node_id].active) {
        return glm::mat4(1.0f);
    }
    return nodes_[node_id].world_transform;
}

std::size_t SceneGraph::activeObjectCount() const {
    // object_to_node_ only stores active object-backed nodes
    return object_to_node_.size();
}

void SceneGraph::updateWorldRecursive(SceneNodeId node_id, const glm::mat4& parent_world) {
    if (node_id >= nodes_.size()) {
        return;
    }

    SceneNode& node = nodes_[node_id];
    if (!node.active) {
        return;
    }

    // hierarchical transform propagation
    // each node inherits the accumulated world transform from its parent
    node.world_transform = parent_world * node.local_transform;
    for (SceneNodeId child_id : node.children) {
        updateWorldRecursive(child_id, node.world_transform);
    }
}

void SceneGraph::removeNodeRecursive(SceneNodeId node_id) {
    if (node_id >= nodes_.size() || node_id == rootNodeId()) {
        return;
    }

    SceneNode& node = nodes_[node_id];
    if (!node.active) {
        return;
    }

    // copy child ids first because recursive removal mutates child vectors as it unwinds
    const std::vector<SceneNodeId> child_ids = node.children;
    for (SceneNodeId child_id : child_ids) {
        removeNodeRecursive(child_id);
    }
    node.children.clear();

    if (isNodeActive(node.parent)) {
        SceneNode& parent = nodes_[node.parent];
        // detach this node from its parent after its subtree is already cleared
        parent.children.erase(
            std::remove(parent.children.begin(), parent.children.end(), node_id),
            parent.children.end());
    }

    if (node.object_reference.has_value()) {
        // object lookup must be cleared so future queries do not see stale ids
        object_to_node_.erase(node.object_reference.value());
    }
    node.parent = rootNodeId();
    node.active = false;
    node.local_transform = glm::mat4(1.0f);
    node.world_transform = glm::mat4(1.0f);
    node.bounding_radius = 0.0f;
    node.object_reference = std::nullopt;
    // remember this slot so createNode can reuse it later
    free_node_ids_.push_back(node_id);
}

bool SceneGraph::isNodeActive(SceneNodeId node_id) const {
    return node_id < nodes_.size() && nodes_[node_id].active;
}

bool SceneGraph::isAncestor(SceneNodeId ancestor, SceneNodeId node) const {
    if (!isNodeActive(ancestor) || !isNodeActive(node)) {
        return false;
    }

    SceneNodeId cursor = node;
    while (isNodeActive(cursor)) {
        if (cursor == ancestor) {
            return true;
        }
        const SceneNodeId parent = nodes_[cursor].parent;
        if (parent == cursor) {
            // reached the synthetic root
            break;
        }
        if (!isNodeActive(parent)) {
            break;
        }
        cursor = parent;
    }
    return false;
}

glm::vec3 SceneGraph::worldPositionForNode(SceneNodeId node_id) const {
    if (!isNodeActive(node_id)) {
        // invalid nodes contribute a safe zero position
        return glm::vec3(0.0f);
    }
    // node position is the translation column of the accumulated world transform
    return matrix_translation(nodes_[node_id].world_transform);
}

std::pair<glm::vec3, glm::vec3> SceneGraph::boundsForNode(SceneNodeId node_id) const {
    if (!isNodeActive(node_id)) {
        // invalid nodes contribute an empty safe box
        return std::make_pair(glm::vec3(0.0f), glm::vec3(0.0f));
    }
    // each object is treated as a sphere for broad phase and expanded into an AABB for BVH storage
    return sphere_bounds(nodes_[node_id].world_transform, nodes_[node_id].bounding_radius);
}

std::pair<glm::vec3, glm::vec3> SceneGraph::computeRangeBounds(std::size_t start, std::size_t end) const {
    if (start >= end || end > spatial_object_nodes_.size()) {
        // invalid ranges return a safe zero box
        return std::make_pair(glm::vec3(0.0f), glm::vec3(0.0f));
    }

    // seed the combined bounds with the first object then expand to contain the rest
    // this gives the final box for one BVH subtree candidate
    const auto first_bounds = boundsForNode(spatial_object_nodes_[start]);
    // initialize the running minimum corner from the first object
    glm::vec3 min_bounds = first_bounds.first;
    // initialize the running maximum corner from the first object
    glm::vec3 max_bounds = first_bounds.second;
    for (std::size_t index = start + 1; index < end; ++index) {
        // fetch the next object's bounding box
        const auto object_bounds = boundsForNode(spatial_object_nodes_[index]);
        // expand the minimum corner component-wise
        min_bounds = glm::min(min_bounds, object_bounds.first);
        // expand the maximum corner component-wise
        max_bounds = glm::max(max_bounds, object_bounds.second);
    }
    // return the full bounding box that encloses every object in the range
    return std::make_pair(min_bounds, max_bounds);
}

std::pair<glm::vec3, glm::vec3> SceneGraph::computeCentroidBounds(std::size_t start,
                                                                  std::size_t end) const {
    if (start >= end || end > spatial_object_nodes_.size()) {
        // invalid ranges return a safe zero result
        return std::make_pair(glm::vec3(0.0f), glm::vec3(0.0f));
    }

    // centroid bounds are separate from object bounds because splitting should follow object distribution
    // use the first centroid as the starting min and max
    glm::vec3 min_centroid = worldPositionForNode(spatial_object_nodes_[start]);
    // both min and max start from the same centroid
    glm::vec3 max_centroid = min_centroid;
    for (std::size_t index = start + 1; index < end; ++index) {
        // compute the current object's centroid from its world position
        const glm::vec3 centroid = worldPositionForNode(spatial_object_nodes_[index]);
        // shrink the minimum centroid corner if needed
        min_centroid = glm::min(min_centroid, centroid);
        // grow the maximum centroid corner if needed
        max_centroid = glm::max(max_centroid, centroid);
    }
    // this box measures spatial spread of object centers only
    return std::make_pair(min_centroid, max_centroid);
}

std::uint32_t SceneGraph::buildBvhRecursive(std::size_t start, std::size_t end) {
    // every recursive call owns one contiguous slice of spatial_object_nodes_
    // that slice represents all objects inside one spatial region of the BVH
    // compute the final enclosing bounds for this whole slice
    const auto bounds = computeRangeBounds(start, end);
    // the current BVH node index is just the next slot in the flat node vector
    const std::uint32_t node_index = static_cast<std::uint32_t>(bvh_nodes_.size());
    // push a provisional leaf node first
    // if the slice turns out to be splittable, this node will later become an internal node
    bvh_nodes_.push_back(BvhNode{
        // min corner of the region covered by this node
        bounds.first,
        // max corner of the region covered by this node
        bounds.second,
        // child indices are placeholders until we decide whether this is internal
        0,
        0,
        // leaves refer back into the flat object slice by start index
        start,
        // leaves know how many object node ids belong to them
        end - start,
        // start as a leaf by default
        true
    });

    // small ranges stay as leaves so traversal can stop and test the contained objects directly
    if ((end - start) <= max_leaf_objects_) {
        // the leaf is already fully configured, so return its index
        return node_index;
    }

    // choose the widest centroid axis so the split follows the longest spread of objects
    // a BVH tries to separate space into smaller regions that are easier to reject during queries
    // this is only a heuristic, but it is simple and usually effective
    const auto centroid_bounds = computeCentroidBounds(start, end);
    // extent tells us how much the centroids vary on each axis
    const glm::vec3 extent = centroid_bounds.second - centroid_bounds.first;

    // default to splitting along X
    int axis = 0;
    if (extent.y > extent.x && extent.y >= extent.z) {
        // if Y has the greatest spread, split along Y instead
        axis = 1;
    } else if (extent.z > extent.x && extent.z >= extent.y) {
        // if Z has the greatest spread, split along Z instead
        axis = 2;
    }

    // if all centroids collapse to nearly the same point then another split would be meaningless
    if (extent[axis] <= 0.0001f) {
        // keep the node as a leaf because subdivision would not produce useful spatial separation
        return node_index;
    }

    // partition around the median centroid on the chosen axis for a reasonably balanced binary tree
    // after nth_element, objects on the left half are generally on one side of space
    // and objects on the right half are generally on the other side
    // the midpoint is the boundary between those two halves
    const std::size_t mid = start + ((end - start) / 2);
    std::nth_element(
        // beginning of the slice to partition
        spatial_object_nodes_.begin() + static_cast<std::ptrdiff_t>(start),
        // nth position that should hold the median element after partitioning
        spatial_object_nodes_.begin() + static_cast<std::ptrdiff_t>(mid),
        // one-past-the-end of the slice to partition
        spatial_object_nodes_.begin() + static_cast<std::ptrdiff_t>(end),
        [this, axis](SceneNodeId left, SceneNodeId right) {
            // compare centroids along the chosen split axis only
            return worldPositionForNode(left)[axis] < worldPositionForNode(right)[axis];
        });

    // safety guard in case the partition fails to produce two non-empty halves
    if (mid == start || mid == end) {
        // leave this node as a leaf if the split would be degenerate
        return node_index;
    }

    // internal nodes drop direct object storage and point at two child BVH nodes instead
    // this is what makes the structure hierarchical
    // each internal node says "first test my large box, then maybe test my two smaller boxes"
    // mark the node as internal because it now owns two children instead of direct objects
    bvh_nodes_[node_index].is_leaf = false;
    // recursively build the left child over the first half of the object slice
    bvh_nodes_[node_index].left_child = buildBvhRecursive(start, mid);
    // recursively build the right child over the second half of the object slice
    bvh_nodes_[node_index].right_child = buildBvhRecursive(mid, end);
    // internal nodes no longer store a leaf start index
    bvh_nodes_[node_index].start = 0;
    // internal nodes no longer store a leaf object count
    bvh_nodes_[node_index].count = 0;
    // return the root index for this subtree
    return node_index;
}

void SceneGraph::queryRadiusRecursive(std::vector<std::uint32_t>& out_objects,
                                      std::uint32_t bvh_node_index,
                                      const glm::vec3& center,
                                      float radius) const {
    if (bvh_node_index >= bvh_nodes_.size()) {
        // invalid indices are ignored defensively
        return;
    }

    // fetch the current BVH node from flat storage
    const BvhNode& bvh_node = bvh_nodes_[bvh_node_index];
    // broad phase prune
    // if the query circle cannot reach this nodes AABB in XZ we can discard the whole subtree
    // this is the main payoff of a BVH
    // one cheap test can eliminate many objects at once
    if (squared_distance_to_aabb_xz(center, bvh_node.min_bounds, bvh_node.max_bounds) >
        (radius * radius)) {
        return;
    }

    if (bvh_node.is_leaf) {
        // leaves store actual object references so this is where narrow broad-phase checks happen
        for (std::size_t offset = 0; offset < bvh_node.count; ++offset) {
            // recover the scene node id stored in this leaf slot
            const SceneNodeId node_id = spatial_object_nodes_[bvh_node.start + offset];
            if (!isNodeActive(node_id)) {
                // ignore stale ids just in case
                continue;
            }

            // fetch the full scene node
            const SceneNode& node = nodes_[node_id];
            if (!node.object_reference.has_value()) {
                // helper nodes should never appear here, but skip them if they do
                continue;
            }

            // compute the object center in world space
            const glm::vec3 world_position = worldPositionForNode(node_id);
            // reduce the query to the XZ plane because this engine currently broad-tests in ground space
            const glm::vec2 delta_xz(world_position.x - center.x,
                                     world_position.z - center.z);
            // the object radius expands the accepted distance so large objects still get returned
            const float max_distance = radius + node.bounding_radius;
            // compare squared distance against squared allowed radius
            if (glm::dot(delta_xz, delta_xz) <= (max_distance * max_distance)) {
                // append the game-object id rather than the internal scene-node id
                out_objects.push_back(node.object_reference.value());
            }
        }
        // once a leaf is processed, there are no child nodes to recurse into
        return;
    }

    // recurse into both children because either branch may contain overlapping objects
    // left child covers one spatial half of this node
    queryRadiusRecursive(out_objects, bvh_node.left_child, center, radius);
    // right child covers the other spatial half
    queryRadiusRecursive(out_objects, bvh_node.right_child, center, radius);
}

void SceneGraph::queryAabbRecursive(std::vector<std::uint32_t>& out_objects,
                                    std::uint32_t bvh_node_index,
                                    const glm::vec2& min_xz,
                                    const glm::vec2& max_xz) const {
    if (bvh_node_index >= bvh_nodes_.size()) {
        // invalid indices are ignored defensively
        return;
    }

    // normalize the query box so min really is the minimum corner
    const float min_x = std::min(min_xz.x, max_xz.x);
    const float min_z = std::min(min_xz.y, max_xz.y);
    const float max_x = std::max(min_xz.x, max_xz.x);
    const float max_z = std::max(min_xz.y, max_xz.y);

    // embed the 2D XZ query in 3D vectors so the same AABB helper can be reused
    const glm::vec3 query_min(min_x, 0.0f, min_z);
    const glm::vec3 query_max(max_x, 0.0f, max_z);
    // fetch the current BVH node
    const BvhNode& bvh_node = bvh_nodes_[bvh_node_index];
    // same broad-phase idea as the radius query but with box-vs-box overlap in XZ
    // if the parent box misses, every object stored below it also misses
    if (!overlaps_aabb_xz(query_min, query_max, bvh_node.min_bounds, bvh_node.max_bounds)) {
        // reject the entire subtree immediately
        return;
    }

    if (bvh_node.is_leaf) {
        // once inside a leaf we test each object volume against the query box
        for (std::size_t offset = 0; offset < bvh_node.count; ++offset) {
            // recover one scene node id from the leaf range
            const SceneNodeId node_id = spatial_object_nodes_[bvh_node.start + offset];
            if (!isNodeActive(node_id)) {
                // skip stale or dead nodes
                continue;
            }

            // fetch the full scene node
            const SceneNode& node = nodes_[node_id];
            if (!node.object_reference.has_value()) {
                // helper nodes are not valid query results
                continue;
            }

            // compute the object's own broad-phase box
            const auto object_bounds = boundsForNode(node_id);
            if (overlaps_aabb_xz(query_min, query_max, object_bounds.first, object_bounds.second)) {
                // append the object id if its bounds overlap the query box
                out_objects.push_back(node.object_reference.value());
            }
        }
        // done with this leaf
        return;
    }

    // internal nodes never hold direct objects so traversal continues down both children
    // descend into the left subtree first
    queryAabbRecursive(out_objects, bvh_node.left_child, min_xz, max_xz);
    // then descend into the right subtree
    queryAabbRecursive(out_objects, bvh_node.right_child, min_xz, max_xz);
}
