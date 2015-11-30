/*
 * Copyright (c) 2011-2015, Georgia Tech Research Corporation
 * All rights reserved.
 *
 * Author(s):
 * Date:
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

#include "dart/dynamics/MeshShape.h"

#include <limits>
#include <string>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/cimport.h>

#include "dart/renderer/RenderInterface.h"
#include "dart/common/Console.h"
#include "dart/dynamics/AssimpInputResourceAdaptor.h"
#include "dart/common/LocalResourceRetriever.h"
#include "dart/common/Uri.h"

// We define our own constructor for aiScene, because it seems to be missing
// from the standard assimp library
aiScene::aiScene()
  : mFlags(0),
    mRootNode(nullptr),
    mNumMeshes(0),
    mMeshes(nullptr),
    mNumMaterials(0),
    mMaterials(nullptr),
    mAnimations(nullptr),
    mNumTextures(0),
    mTextures(nullptr),
    mNumLights(0),
    mLights(nullptr),
    mNumCameras(0),
    mCameras(nullptr)
{

}

// We define our own destructor for aiScene, because it seems to be missing
// from the standard assimp library
aiScene::~aiScene()
{
  delete mRootNode;

  if(mNumMeshes && mMeshes)
    for(size_t a=0; a<mNumMeshes; ++a)
      delete mMeshes[a];
  delete[] mMeshes;

  if(mNumMaterials && mMaterials)
    for(size_t a=0; a<mNumMaterials; ++a)
      delete mMaterials[a];
  delete[] mMaterials;

  if(mNumAnimations && mAnimations)
    for(size_t a=0; a<mNumAnimations; ++a)
      delete mAnimations[a];
  delete[] mAnimations;

  if(mNumTextures && mTextures)
    for(size_t a=0; a<mNumTextures; ++a)
      delete mTextures[a];
  delete[] mTextures;

  if(mNumLights && mLights)
    for(size_t a=0; a<mNumLights; ++a)
      delete mLights[a];
  delete[] mLights;

  if(mNumCameras && mCameras)
    for(size_t a=0; a<mNumCameras; ++a)
      delete mCameras[a];
  delete[] mCameras;
}

// We define our own constructor for aiMaterial, because it seems to be missing
// from the standard assimp library
aiMaterial::aiMaterial()
{
  mNumProperties = 0;
  mNumAllocated = 5;
  mProperties = new aiMaterialProperty*[5];
  for(size_t i=0; i<5; ++i)
    mProperties[i] = nullptr;
}

// We define our own destructor for aiMaterial, because it seems to be missing
// from the standard assimp library
aiMaterial::~aiMaterial()
{
  for(size_t i=0; i<mNumProperties; ++i)
    delete mProperties[i];

  delete[] mProperties;
}


using namespace dart::math;
namespace dart {
namespace dynamics {

MeshShape::MeshShape(const Eigen::Vector3d& _scale, const aiScene* _mesh,
                     const std::string& _path,
                     const common::ResourceRetrieverPtr& _resourceRetriever)
  : Shape(MESH),
    mResourceRetriever(_resourceRetriever),
    mDisplayList(0),
    mColorMode(MATERIAL_COLOR),
    mColorIndex(0)
{
  assert(_scale[0] > 0.0);
  assert(_scale[1] > 0.0);
  assert(_scale[2] > 0.0);

  setMesh(_mesh, _path, _resourceRetriever);
  setScale(_scale);
}

MeshShape::~MeshShape() {
}


const std::string& MeshShape::getMeshUri() const
{
  return mMeshUri;
}

void MeshShape::update()
{
  // Do nothing
}

void MeshShape::setAlpha(double _alpha) {
  for (size_t i = 0; i < mMeshData.size(); ++i) {
      Mesh& mesh = mMeshData.at(i);
      for (size_t j = 0; j < mesh.colors.size(); ++j) {
          mesh.colors.at(j)(3) = _alpha;
      }
  }
}

const std::string &MeshShape::getMeshPath() const
{
  return mMeshPath;
}

void MeshShape::setMesh(
  const aiScene* _mesh, const std::string& _path,
  const common::ResourceRetrieverPtr& _resourceRetriever)
{

  if(nullptr == _mesh) {
    mMeshPath = "";
    mMeshUri = "";
    mResourceRetriever = nullptr;
    return;
  }

  common::Uri uri;
  if(uri.fromString(_path))
  {
    mMeshUri = _path;

    if(uri.mScheme.get_value_or("file") == "file")
      mMeshPath = uri.mPath.get_value_or("");
  }
  else
  {
    dtwarn << "[MeshShape::setMesh] Failed parsing URI '" << _path << "'.\n";
    mMeshUri = "";
    mMeshPath = "";
  }

  mResourceRetriever = _resourceRetriever;

  for (size_t i = 0; i < _mesh->mNumMeshes; ++i) {
      mMeshData.push_back(Mesh());
      Mesh& mesh = mMeshData.at(i);
      aiMesh* assimpMesh = _mesh->mMeshes[i];

      if (assimpMesh->HasNormals()) {
          mesh.normals.resize(assimpMesh->mNumVertices);
          for (size_t j = 0; j < assimpMesh->mNumVertices; ++j) {
              const aiVector3D& norm = assimpMesh->mNormals[i];
              mesh.normals.at(j)(0) = norm.x;
              mesh.normals.at(j)(1) = norm.y;
              mesh.normals.at(j)(2) = norm.z;
          }
      }

      if (assimpMesh->HasPositions()) {
          mesh.vertices.resize(assimpMesh->mNumVertices);
          for (size_t j = 0; j < assimpMesh->mNumVertices; ++j) {
              const aiVector3D& vert = assimpMesh->mVertices[i];
              mesh.vertices.at(j)(0) = vert.x;
              mesh.vertices.at(j)(1) = vert.y;
              mesh.vertices.at(j)(2) = vert.z;
          }
      }

      if (assimpMesh->HasFaces()) {
          mesh.indices.resize(assimpMesh->mNumFaces);

          for (size_t j = 0; j < assimpMesh->mNumFaces; ++j) {
              const aiFace& face = assimpMesh->mFaces[j];
              if (face.mNumIndices != 3) {
                  dtwarn << "[MeshShape::setMesh] Problem parsing URI '" << _path << ": only triangular faces supported.";
                  break;
              }
              for (size_t k = 0; k < 3; k++) {
                  mesh.indices[j][k] = face.mIndices[k];
              }
          }
      }

  }

  _updateBoundingBoxDim();
  computeVolume();
}

void MeshShape::setScale(const Eigen::Vector3d& _scale) {
  assert(_scale[0] > 0.0);
  assert(_scale[1] > 0.0);
  assert(_scale[2] > 0.0);
  mScale = _scale;
  computeVolume();
  _updateBoundingBoxDim();
}

const Eigen::Vector3d& MeshShape::getScale() const {
  return mScale;
}

void MeshShape::setColorMode(ColorMode _mode)
{
  mColorMode = _mode;
}

MeshShape::ColorMode MeshShape::getColorMode() const
{
  return mColorMode;
}

void MeshShape::setColorIndex(int _index)
{
  mColorIndex = _index;
}

int MeshShape::getColorIndex() const
{
  return mColorIndex;
}

int MeshShape::getDisplayList() const {
  return mDisplayList;
}

void MeshShape::setDisplayList(int _index) {
  mDisplayList = _index;
}

void MeshShape::draw(renderer::RenderInterface* _ri,
                     const Eigen::Vector4d& _color,
                     bool _useDefaultColor) const {
  if (!_ri)
    return;
  if (mHidden)
    return;

  if (!_useDefaultColor)
    _ri->setPenColor(_color);
  else
    _ri->setPenColor(mColor);
  _ri->pushMatrix();
  _ri->transform(mTransform);

  _ri->drawMesh(mScale, mMeshData);

  _ri->popMatrix();
}

Eigen::Matrix3d MeshShape::computeInertia(double _mass) const {
  // use bounding box to represent the mesh
  Eigen::Vector3d bounds = mBoundingBox.computeFullExtents();
  double l = bounds.x();
  double h = bounds.y();
  double w = bounds.z();

  Eigen::Matrix3d inertia = Eigen::Matrix3d::Identity();
  inertia(0, 0) = _mass / 12.0 * (h * h + w * w);
  inertia(1, 1) = _mass / 12.0 * (l * l + w * w);
  inertia(2, 2) = _mass / 12.0 * (l * l + h * h);

  return inertia;
}

void MeshShape::computeVolume() {
  Eigen::Vector3d bounds = mBoundingBox.computeFullExtents();
  mVolume = bounds.x() * bounds.y() * bounds.z();
}

void MeshShape::_updateBoundingBoxDim() {
  double max_X = -std::numeric_limits<double>::infinity();
  double max_Y = -std::numeric_limits<double>::infinity();
  double max_Z = -std::numeric_limits<double>::infinity();
  double min_X = std::numeric_limits<double>::infinity();
  double min_Y = std::numeric_limits<double>::infinity();
  double min_Z = std::numeric_limits<double>::infinity();

  for (unsigned int i = 0; i < mMeshData.size(); i++) {
    const Mesh& mesh = mMeshData.at(i);
    for (unsigned int j = 0; j < mesh.vertices.size(); j++) {
      const Eigen::Vector3d& vert = mesh.vertices.at(j);
      if (vert.x() > max_X)
        max_X = vert.x();
      else if (vert.x() < min_X)
        min_X = vert.x();
      if (vert.y() > max_Y)
        max_Y = vert.y();
      else if (vert.y() < min_Y)
        min_Y = vert.y();
      if (vert.z() > max_Z)
        max_Z = vert.z();
      else if (vert.z() < min_Z)
        min_Z = vert.z();
    }
  }
  mBoundingBox.setMin(Eigen::Vector3d(min_X * mScale[0], min_Y * mScale[1], min_Z * mScale[2]));
  mBoundingBox.setMax(Eigen::Vector3d(max_X * mScale[0], max_Y * mScale[1], max_Z * mScale[2]));
}

const aiScene* MeshShape::loadMesh(
  const std::string& _uri, const common::ResourceRetrieverPtr& _retriever)
{
  // Remove points and lines from the import.
  aiPropertyStore* propertyStore = aiCreatePropertyStore();
  aiSetImportPropertyInteger(propertyStore,
    AI_CONFIG_PP_SBP_REMOVE,
      aiPrimitiveType_POINT
    | aiPrimitiveType_LINE
  );

  // Wrap ResourceRetriever in an IOSystem from Assimp's C++ API.  Then wrap
  // the IOSystem in an aiFileIO from Assimp's C API. Yes, this API is
  // completely ridiculous...
  AssimpInputResourceRetrieverAdaptor systemIO(_retriever);
  aiFileIO fileIO = createFileIO(&systemIO);

  // Import the file.
  const aiScene* scene = aiImportFileExWithProperties(
    _uri.c_str(), 
      aiProcess_GenNormals
    | aiProcess_Triangulate
    | aiProcess_JoinIdenticalVertices
    | aiProcess_SortByPType
    | aiProcess_OptimizeMeshes,
    &fileIO,
    propertyStore
  );

  // If succeeded, store the importer in the scene to keep it alive. This is
  // necessary because the importer owns the memory that it allocates.
  if(!scene)
  {
    dtwarn << "[MeshShape::loadMesh] Failed loading mesh '" << _uri << "'.\n";
    return nullptr;
  }

  // Assimp rotates collada files such that the up-axis (specified in the
  // collada file) aligns with assimp's y-axis. Here we are reverting this
  // rotation. We are only catching files with the .dae file ending here. We
  // might miss files with an .xml file ending, which would need to be looked
  // into to figure out whether they are collada files.
  std::string extension;
  const size_t extensionIndex = _uri.find_last_of('.');
  if(extensionIndex != std::string::npos)
    extension = _uri.substr(extensionIndex);

  std::transform(std::begin(extension), std::end(extension),
                 std::begin(extension), ::tolower);

  if(extension == ".dae" || extension == ".zae")
    scene->mRootNode->mTransformation = aiMatrix4x4();

  // Finally, pre-transform the vertices. We can't do this as part of the
  // import process, because we may have changed mTransformation above.
  scene = aiApplyPostProcessing(scene, aiProcess_PreTransformVertices);
  if(!scene)
    dtwarn << "[MeshShape::loadMesh] Failed pre-transforming vertices.\n";

  return scene;
}

const aiScene* MeshShape::loadMesh(const std::string& _fileName)
{
  const auto retriever = std::make_shared<common::LocalResourceRetriever>();
  return loadMesh("file://" + _fileName, retriever);
}

}  // namespace dynamics
}  // namespace dart
