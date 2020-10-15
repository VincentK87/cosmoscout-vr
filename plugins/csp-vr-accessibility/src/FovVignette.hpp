////////////////////////////////////////////////////////////////////////////////////////////////////
//                               This file is part of CosmoScout VR                               //
//      and may be used under the terms of the MIT license. See the LICENSE file for details.     //
//                        Copyright: (c) 2019 German Aerospace Center (DLR)                       //
////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef CSP_VR_ACCESSIBILITY_FOVVIGNETTE_HPP
#define CSP_VR_ACCESSIBILITY_FOVVIGNETTE_HPP

#include "Plugin.hpp"

#include "../../../src/cs-scene/CelestialObject.hpp"

#include <VistaKernel/GraphicsManager/VistaOpenGLDraw.h>
#include <VistaKernel/GraphicsManager/VistaOpenGLNode.h>
#include <VistaKernel/GraphicsManager/VistaTransformNode.h>
#include <VistaOGLExt/VistaBufferObject.h>
#include <VistaOGLExt/VistaGLSLShader.h>
#include <VistaOGLExt/VistaTexture.h>
#include <VistaOGLExt/VistaVertexArrayObject.h>
#include <VistaKernel/DisplayManager/VistaViewport.h>

namespace cs::core {
class Settings;
class SolarSystem;
} // namespace cs::core

namespace csp::vraccessibility {
/// The FoV Vignette. It draws a vignette when the observer is moving.
class FovVignette : public IVistaOpenGLDraw{
 public:
  FovVignette(std::shared_ptr<cs::core::SolarSystem> solarSystem);

  FovVignette(FovVignette const& other) = delete;
  FovVignette(FovVignette&& other) = default;

  FovVignette& operator=(FovVignette const& other) = delete;
  FovVignette& operator=(FovVignette&& other) = delete;

  ~FovVignette() override;

  /// Configures the internal renderer according to the given values.
  void configure(std::shared_ptr<Plugin::Settings> settings);

  bool Do() override;
  bool GetBoundingBox(VistaBoundingBox& bb) override;

 private:
  std::shared_ptr<cs::core::Settings>     mSettings;
  std::shared_ptr<cs::core::SolarSystem>  mSolarSystem;

  std::shared_ptr<Plugin::Settings>       mVignetteSettings;

  VistaGLSLShader                         mShader;
  VistaVertexArrayObject                  mVAO;
  VistaBufferObject                       mVBO;

  struct GBufferData {
    std::unique_ptr<VistaTexture> mDepthBuffer;
    std::unique_ptr<VistaTexture> mColorBuffer;
  };

  std::unordered_map<VistaViewport*, GBufferData> mGBufferData;

  static const char* VERT_SHADER;
  static const char* FRAG_SHADER;
}; // class FloorGrid
} // namespace csp::vraccessibility

#endif // CSP_VR_ACCESSIBILITY_FOVVIGNETTE_HPP