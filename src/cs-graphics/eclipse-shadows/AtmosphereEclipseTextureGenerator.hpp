////////////////////////////////////////////////////////////////////////////////////////////////////
//                               This file is part of CosmoScout VR                               //
//      and may be used under the terms of the MIT license. See the LICENSE file for details.     //
//                        Copyright: (c) 2019 German Aerospace Center (DLR)                       //
////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef CS_GRAPHICS_ATMOSPHERE_ECLIPSE_SHADOW_GENERATOR_HPP
#define CS_GRAPHICS_ATMOSPHERE_ECLIPSE_SHADOW_GENERATOR_HPP

#include "../../cs-utils/SimpleTexture.hpp"
#include "BodyProperties.hpp"
#include "ColorConverter.hpp"
#include "Photon.hpp"
#include "PhotonAtmosphereTracer.hpp"
#include "TextureTracer.hpp"
#include <random>

namespace cs::graphics {
class AtmosphereEclipseTextureGenerator {
 public:
  AtmosphereEclipseTextureGenerator();

  std::pair<cs::utils::Texture4f, double> createShadowMap(
      BodyWithAtmosphere const& body, size_t photonCount);

 private:
  /// Generates a point on one half of the suns surface:
  /// o  o
  ///       o
  ///        o
  ///        o
  ///       o
  /// o  o
  glm::dvec2           randomPointOnSunSurface(double sunPositionX);
  PhotonF              emitPhoton(BodyWithAtmosphere const& body);
  std::vector<PhotonF> generatePhotons(uint32_t count, BodyWithAtmosphere const& body);

  std::mt19937_64                         mRNG;
  std::uniform_int_distribution<uint32_t> mDistributionWavelength;
  std::bernoulli_distribution             mDistributionBoolean;

  PhotonAtmosphereTracer         mPhotonAtmosphereTracer;
  std::unique_ptr<TextureTracer> mTextureTracer;
  ColorConverter                 mColorConverter;
};
} // namespace cs::graphics

#endif // CS_GRAPHICS_ATMOSPHERE_ECLIPSE_SHADOW_GENERATOR_HPP