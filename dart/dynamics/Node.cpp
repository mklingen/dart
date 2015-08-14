/*
 * Copyright (c) 2015, Georgia Tech Research Corporation
 * All rights reserved.
 *
 * Author(s): Michael X. Grey <mxgrey@gatech.edu>
 *
 * Georgia Tech Graphics Lab and Humanoid Robotics Lab
 *
 * Directed by Prof. C. Karen Liu and Prof. Mike Stilman
 * <karenliu@cc.gatech.edu> <mstilman@cc.gatech.edu>
 *
 * This file is provided under the following "BSD-style" License:
 *   Redistribution and use in source and binary forms, with or
 *   without modification, are permitted provided that the following
 *   conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *   CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *   MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 *   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 *   USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 *   AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *   ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *   POSSIBILITY OF SUCH DAMAGE.
 */

#include "dart/dynamics/Node.h"
#include "dart/dynamics/BodyNode.h"

#define REPORT_INVALID_NODE( func )                                          \
  dterr << "[Node::" #func "] This Node was not constructed correctly. It "  \
        << "needs to specify a valid BodyNode pointer during construction. " \
        << "Please report this as a bug if it is not a custom node type!\n"; \
  assert(false);

namespace dart {
namespace dynamics {

//==============================================================================
NodeCleaner::NodeCleaner(Node* _node)
  : mNode(_node)
{
  // Do nothing
}

//==============================================================================
NodeCleaner::~NodeCleaner()
{
  delete mNode;
}

//==============================================================================
Node* NodeCleaner::getNode() const
{
  return mNode;
}

//==============================================================================
void Node::setNodeState(const std::unique_ptr<State>& /*otherState*/)
{
  // Do nothing
}

//==============================================================================
const Node::State* Node::getNodeState() const
{
  return mNodeStatePtr;
}

//==============================================================================
void Node::setNodeProperties(const std::unique_ptr<Properties>& properties)
{
  // Do nothing
}

//==============================================================================
const Node::Properties* Node::getNodeProperties() const
{
  return mNodePropertiesPtr;
}

//==============================================================================
BodyNodePtr Node::getBodyNodePtr()
{
  return mBodyNode;
}

//==============================================================================
ConstBodyNodePtr Node::getBodyNodePtr() const
{
  return mBodyNode;
}

//==============================================================================
bool Node::isRemoved() const
{
  if(nullptr == mBodyNode)
  {
    REPORT_INVALID_NODE(isRemoved);
    return true;
  }

  return !mAmAttached;
}

//==============================================================================
std::shared_ptr<NodeCleaner> Node::generateCleaner()
{
  std::shared_ptr<NodeCleaner> cleaner = mCleaner.lock();
  if(nullptr == cleaner)
  {
    cleaner = std::shared_ptr<NodeCleaner>(new NodeCleaner(this));
    mCleaner = cleaner;
  }

  return cleaner;
}

//==============================================================================
Node::Node(BodyNode* _bn)
  : mBodyNode(_bn),
    mAmAttached(false),
    mIndexInBodyNode(INVALID_INDEX)
{
  if(nullptr == mBodyNode)
  {
    REPORT_INVALID_NODE(Node);
    return;
  }

  setNodeStatePtr();
  setNodePropertiesPtr();
}

//==============================================================================
std::string Node::registerNameChange(const std::string& newName)
{
  const SkeletonPtr& skel = mBodyNode->getSkeleton();
  if(nullptr == skel)
    return newName;

  Skeleton::NodeNameMgrMap::iterator it =
      skel->mNodeNameMgrMap.find(typeid(*this));

  if(skel->mNodeNameMgrMap.end() == it)
    return newName;

  common::NameManager<Node*>& mgr = it->second;
  return mgr.changeObjectName(this, newName);
}

//==============================================================================
void Node::setNodeStatePtr(State* ptr)
{
  mNodeStatePtr = ptr;
}

//==============================================================================
void Node::setNodePropertiesPtr(Properties* ptr)
{
  mNodePropertiesPtr = ptr;
}

//==============================================================================
void Node::attach()
{
  if(nullptr == mBodyNode)
  {
    REPORT_INVALID_NODE(attach);
    return;
  }

  // If we are in release mode, and the Node believes it is attached, then we
  // can shortcut this procedure
#ifdef NDEBUG
  if(mAmAttached)
    return;
#endif

  BodyNode::NodeMap::iterator it = mBodyNode->mNodeMap.find(typeid(*this));

  if(mBodyNode->mNodeMap.end() == it)
  {
    mBodyNode->mNodeMap[typeid(*this)] = std::vector<Node*>();
    it = mBodyNode->mNodeMap.find(typeid(*this));
  }

  std::vector<Node*>& nodes = it->second;
  BodyNode::NodeCleanerSet& cleaners = mBodyNode->mNodeCleaners;

  NodeCleanerPtr cleaner = generateCleaner();
  if(INVALID_INDEX == mIndexInBodyNode)
  {
    // If the Node was not in the map, then its cleaner should not be in the set
    assert(cleaners.find(cleaner) == cleaners.end());

    // If this Node believes its index is invalid, then it should not exist
    // anywhere in the vector
    assert(std::find(nodes.begin(), nodes.end(), this) == nodes.end());

    nodes.push_back(this);
    mIndexInBodyNode = nodes.size()-1;

    cleaners.insert(cleaner);
  }

  assert(std::find(nodes.begin(), nodes.end(), this) != nodes.end());
  assert(cleaners.find(cleaner) != cleaners.end());

  const SkeletonPtr& skel = mBodyNode->getSkeleton();
  if(skel)
    skel->registerNode(this);

  mAmAttached = true;
}

//==============================================================================
void Node::stageForRemoval()
{
  if(nullptr == mBodyNode)
  {
    REPORT_INVALID_NODE(stageForRemoval);
    return;
  }

  // If we are in release mode, and the Node believes it is detached, then we
  // can shortcut this procedure.
#ifdef NDEBUG
  if(!mAmAttached)
    return;
#endif

  BodyNode::NodeMap::iterator it = mBodyNode->mNodeMap.find(typeid(*this));
  NodeCleanerPtr cleaner = generateCleaner();

  BodyNode::NodeCleanerSet& cleaners = mBodyNode->mNodeCleaners;

  if(mBodyNode->mNodeMap.end() == it)
  {
    // If the Node was not in the map, then its index should be invalid
    assert(INVALID_INDEX == mIndexInBodyNode);

    // If the Node was not in the map, then its cleaner should not be in the set
    assert(cleaners.find(cleaner) == cleaners.end());
    return;
  }

  BodyNode::NodeCleanerSet::iterator cleaner_iter = cleaners.find(cleaner);
  // This Node's cleaner should be in the set of cleaners
  assert(cleaners.end() != cleaner_iter);

  std::vector<Node*>& nodes = it->second;

  // This Node's index in the vector should be referring to this Node
  assert(nodes[mIndexInBodyNode] == this);
  nodes.erase(nodes.begin() + mIndexInBodyNode);
  cleaners.erase(cleaner_iter);

  // Reset all the Node indices that have been altered
  for(size_t i=mIndexInBodyNode; i < nodes.size(); ++i)
    nodes[i]->mIndexInBodyNode = i;

  assert(std::find(nodes.begin(), nodes.end(), this) == nodes.end());

  const SkeletonPtr& skel = mBodyNode->getSkeleton();
  if(skel)
    skel->unregisterNode(this);

  mIndexInBodyNode = INVALID_INDEX;
  mAmAttached = false;
}

} // namespace dynamics
} // namespace dart