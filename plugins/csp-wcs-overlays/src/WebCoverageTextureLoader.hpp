////////////////////////////////////////////////////////////////////////////////////////////////////
//                               This file is part of CosmoScout VR                               //
//      and may be used under the terms of the MIT license. See the LICENSE file for details.     //
//                        Copyright: (c) 2019 German Aerospace Center (DLR)                       //
////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef CSP_WCS_OVERLAYS_TEXTURE_LOADER_HPP
#define CSP_WCS_OVERLAYS_TEXTURE_LOADER_HPP

#include "./common/GDALReader.hpp"
#include "WebCoverage.hpp"
#include "WebCoverageService.hpp"

#include "../../../src/cs-utils/ThreadPool.hpp"

#include <boost/filesystem.hpp>

#include <array>
#include <map>

class GDALReader;

namespace csp::wcsoverlays {

/// Struct for storing texture data along with some metadata.
struct WebCoverageTexture {
  std::unique_ptr<unsigned char> mData;
  int                            mWidth;
  int                            mHeight;
};

/// Class for requesting map textures from Web Map Services.
class WebCoverageTextureLoader {
 public:
  /// Struct for defining parameters for a request to a WCS.
  struct Request {
    int                        mMaxSize{};
    Bounds                     mBounds;
    std::optional<std::string> mTime;
    std::optional<std::string> mFormat;

    // Not used in actual requests, only internally
    std::optional<int> layer;
  };

  /// Creates a new ThreadPool with the specified amount of threads.
  WebCoverageTextureLoader();

  /// Async WCS texture loader.
  /// Returns an empty optional if loading the texture failed.
  std::future<std::optional<GDALReader::GreyScaleTexture>> loadTextureAsync(
      WebCoverageService const& wcs, WebCoverage const& coverage, Request const& request,
      std::string const& mapCache, bool saveToCache);

  /// WCS texture loader.
  /// Returns an empty optional if loading the texture failed.
  std::optional<GDALReader::GreyScaleTexture> loadTexture(WebCoverageService const& wcs,
      WebCoverage const& coverage, Request const& request, std::string const& mapCache,
      bool saveToCache);

 private:
  /// Requests a map texture from a WCS.
  /// Returns a binary stream of the texture file if the request succeeds.
  /// Returns an empty optional if the request fails.
  std::optional<std::stringstream> requestTexture(
      WebCoverageService const& wcs, WebCoverage const& layer, Request const& request);

  /// Saves a binary stream of a texture file to the given path.
  void saveTextureToFile(boost::filesystem::path const& file, std::stringstream const& data);

  /// Constructs a path for loading/saving the texture requested with the given parameters.
  boost::filesystem::path getCachePath(WebCoverageService const& wcs, WebCoverage const& coverage,
      Request const& request, std::string const& mapCache);

  /// Constructs a request URL for the given parameters.
  std::string getRequestUrl(
      WebCoverageService const& wcs, WebCoverage const& coverage, Request const& request);

  const std::map<std::string, std::string> mMimeToExtension = {{"image/png", "png"},
      {"image/jpeg", "jpg"}, {"image/jpg", "jpg"}, {"image/tiff", "tiff"},
      {"application/x-netcdf", "nc"}, {"application/x-netcdf4", "nc"}};

  std::mutex            mTextureMutex;
  cs::utils::ThreadPool mThreadPool;
};

} // namespace csp::wcsoverlays

#endif // CSP_WCS_OVERLAYS_TEXTURE_LOADER_HPP