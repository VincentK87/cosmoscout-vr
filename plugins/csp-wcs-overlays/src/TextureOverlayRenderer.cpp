// Plugin Includes
#include "TextureOverlayRenderer.hpp"

#include "logger.hpp"

#include "../../../src/cs-core/Settings.hpp"
#include "../../../src/cs-core/SolarSystem.hpp"
#include "../../../src/cs-core/TimeControl.hpp"
#include "../../../src/cs-graphics/TextureLoader.hpp"
#include "../../../src/cs-utils/FrameTimings.hpp"
#include "../../../src/cs-utils/utils.hpp"

// VISTA includes
#include <VistaInterProcComm/Connections/VistaByteBufferDeSerializer.h>
#include <VistaKernel/Cluster/VistaClusterMode.h>
#include <VistaKernel/DisplayManager/VistaDisplayManager.h>
#include <VistaKernel/DisplayManager/VistaProjection.h>
#include <VistaKernel/DisplayManager/VistaViewport.h>
#include <VistaKernel/GraphicsManager/VistaOpenGLNode.h>
#include <VistaKernel/GraphicsManager/VistaSceneGraph.h>
#include <VistaKernel/GraphicsManager/VistaTransformNode.h>
#include <VistaKernel/VistaSystem.h>
#include <VistaKernelOpenSGExt/VistaOpenSGMaterialTools.h>
#include <VistaOGLExt/Rendering/ABuffer/VistaABufferOIT.h>
#include <VistaOGLExt/VistaBufferObject.h>
#include <VistaOGLExt/VistaGLSLShader.h>
#include <VistaOGLExt/VistaTexture.h>

// Standard includes
#include <boost/filesystem.hpp>
#include <functional>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <rpcndr.h>

namespace csp::wcsoverlays {

////////////////////////////////////////////////////////////////////////////////////////////////////

TextureOverlayRenderer::TextureOverlayRenderer(std::string center,
    std::shared_ptr<cs::core::SolarSystem>                 solarSystem,
    std::shared_ptr<cs::core::TimeControl>                 timeControl,
    std::shared_ptr<cs::core::Settings> settings, std::shared_ptr<Plugin::Settings> pluginSettings,
    std::shared_ptr<cs::core::GuiManager> guiManager)
    : mSettings(std::move(settings))
    , mPluginSettings(std::move(pluginSettings))
    , mCenterName(std::move(center))
    , mSolarSystem(std::move(solarSystem))
    , mTimeControl(std::move(timeControl))
    , mGuiManager(std::move(guiManager))
    , mMinBounds({static_cast<float>(-mSolarSystem->getRadii(mCenterName)[0]),
          static_cast<float>(-mSolarSystem->getRadii(mCenterName)[1]),
          static_cast<float>(-mSolarSystem->getRadii(mCenterName)[2])})
    , mMaxBounds({static_cast<float>(mSolarSystem->getRadii(mCenterName)[0]),
          static_cast<float>(mSolarSystem->getRadii(mCenterName)[1]),
          static_cast<float>(mSolarSystem->getRadii(mCenterName)[2])})
    , mTransferFunction(std::make_unique<cs::graphics::ColorMap>(
          boost::filesystem::path("../share/resources/transferfunctions/HeatLight.json")))
    , m_pSurfaceShader(new VistaGLSLShader()) {

  m_pSurfaceShader->InitVertexShaderFromString(SURFACE_VERT);
  m_pSurfaceShader->InitFragmentShaderFromString(SURFACE_FRAG);
  m_pSurfaceShader->InitGeometryShaderFromString(SURFACE_GEOM);
  m_pSurfaceShader->Link();

  // create textures ---------------------------------------------------------
  for (auto const& viewport : GetVistaSystem()->GetDisplayManager()->GetViewports()) {
    GBufferData bufferData;

    // Texture for previous renderer depth buffer
    bufferData.mDepthBuffer = new VistaTexture(GL_TEXTURE_RECTANGLE);
    bufferData.mDepthBuffer->Bind();
    bufferData.mDepthBuffer->SetWrapS(GL_CLAMP);
    bufferData.mDepthBuffer->SetWrapT(GL_CLAMP);
    bufferData.mDepthBuffer->SetMinFilter(GL_NEAREST);
    bufferData.mDepthBuffer->SetMagFilter(GL_NEAREST);
    bufferData.mDepthBuffer->Unbind();

    // Color texture to overlay
    bufferData.mColorBuffer = new VistaTexture(GL_TEXTURE_2D);
    bufferData.mColorBuffer->Bind();
    bufferData.mColorBuffer->SetWrapS(GL_CLAMP);
    bufferData.mColorBuffer->SetWrapT(GL_CLAMP);
    bufferData.mColorBuffer->SetMinFilter(GL_NEAREST);
    bufferData.mColorBuffer->SetMagFilter(GL_NEAREST);
    bufferData.mColorBuffer->Unbind();

    mGBufferData[viewport.second] = bufferData;
  }

  VistaSceneGraph* pSG = GetVistaSystem()->GetGraphicsManager()->GetSceneGraph();
  mGLNode.reset(pSG->NewOpenGLNode(pSG->GetRoot(), this));

  // Render after planets which are rendered at cs::utils::DrawOrder::ePlanets
  VistaOpenSGMaterialTools::SetSortKeyOnSubtree(
      mGLNode.get(), static_cast<int>(cs::utils::DrawOrder::eOpaqueItems) - 50);

  pBounds.connect([this](Bounds const& value) {
    clearTextures();

    WebCoverageTextureLoader::Request request = getRequest();
    request.mBounds                           = value;
    getTimeIndependentTexture(request);
  });

  mPluginSettings->mMaxTextureSize.connect([this](int value) {
    clearTextures();
    WebCoverageTextureLoader::Request request = getRequest();
    request.mMaxSize                          = value;
    getTimeIndependentTexture(request);
  });

  // Initialize GDAL only once
  GDALReader::InitGDAL();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TextureOverlayRenderer::~TextureOverlayRenderer() {
  clearTextures();

  VistaSceneGraph* pSG = GetVistaSystem()->GetGraphicsManager()->GetSceneGraph();
  pSG->GetRoot()->DisconnectChild(mGLNode.get());

  for (auto data : mGBufferData) {
    delete data.second.mDepthBuffer;
    delete data.second.mColorBuffer;
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

std::string const& TextureOverlayRenderer::getCenter() const {
  return mCenterName;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void TextureOverlayRenderer::configure(Plugin::Settings::Body settings) {
  mSimpleWCSOverlaySettings = std::move(settings);
  pBounds                   = mSimpleWCSOverlaySettings.mActiveBounds.get();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void TextureOverlayRenderer::setActiveWCS(
    WebCoverageService const& wcs, WebCoverage const& coverage) {
  mUpdateTexture = true;
  clearActiveWCS();

  mActiveWCS.emplace(wcs);
  mActiveWCSCoverage.emplace(coverage);

  if (mActiveWCSCoverage) {
    pBounds = coverage.getSettings().mBounds;
    // getTimeIndependentTexture(getRequest());
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void TextureOverlayRenderer::clearActiveWCS() {
  clearTextures();

  mActiveWCS.reset();
  mActiveWCSCoverage.reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void TextureOverlayRenderer::clearTextures() {
  mTexture.buffer = nullptr;
  mTextures.clear();
  mTexturesBuffer.clear();
  mWrongTextures.clear();

  mCurrentTexture = "";
  mActiveLayer    = 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void TextureOverlayRenderer::updateLonLatRange() {
  if (!mActiveWCS || !mActiveWCSCoverage) {
    return;
  }

  VistaProjection::VistaProjectionProperties* projectionProperties =
      GetVistaSystem()
          ->GetDisplayManager()
          ->GetCurrentRenderInfo()
          ->m_pViewport->GetProjection()
          ->GetProjectionProperties();

  float posX, posY, posZ;
  projectionProperties->GetProjPlaneMidpoint(posX, posY, posZ);
  double left, right, bottom, top;
  projectionProperties->GetProjPlaneExtents(left, right, bottom, top);

  // Get the intersections of the camera rays at the corners of the screen with the body.
  std::array<std::pair<bool, glm::dvec3>, 4> intersections;
  intersections[0].first =
      mSolarSystem->getBody(mCenterName)
          ->getIntersection(glm::dvec3(0, 0, 0), glm::normalize(glm::dvec3(left, top, posZ)),
              intersections[0].second);
  intersections[1].first =
      mSolarSystem->getBody(mCenterName)
          ->getIntersection(glm::dvec3(0, 0, 0), glm::normalize(glm::dvec3(left, bottom, posZ)),
              intersections[1].second);
  intersections[2].first =
      mSolarSystem->getBody(mCenterName)
          ->getIntersection(glm::dvec3(0, 0, 0), glm::normalize(glm::dvec3(right, bottom, posZ)),
              intersections[2].second);
  intersections[3].first =
      mSolarSystem->getBody(mCenterName)
          ->getIntersection(glm::dvec3(0, 0, 0), glm::normalize(glm::dvec3(right, top, posZ)),
              intersections[3].second);

  if (!std::all_of(intersections.begin(), intersections.end(),
          [](auto intersection) { return intersection.first; })) {
    // The body is not visible in all four corners of the screen.
    // For now this results in using the maximum bounds of the map.
    pBounds = mActiveWCSCoverage->getSettings().mBounds;
  } else {
    // All four corners of the screen show the body.
    // The intersection points can be converted to longitude and latitude.
    Bounds currentBounds;

    glm::dvec3                radii = mSolarSystem->getRadii(mCenterName);
    std::array<glm::dvec2, 4> screenBounds{};
    for (int i = 0; i < 4; i++) {
      screenBounds[i] = cs::utils::convert::cartesianToLngLat(intersections[i].second, radii);
      screenBounds[i] = cs::utils::convert::toDegrees(screenBounds[i]);
    }

    currentBounds.mMinLon = screenBounds[0][0];
    currentBounds.mMaxLon = screenBounds[0][0];

    // Determine the minimum and maximum longitude.
    // To do so, the edges between neighboring corners are examined and classified as one of four
    // categories. Depending on the category the longitude range can be updated.
    // Also save the lengths of the edges for later (lonDiffs).
    // Uses counterclockwise winding order.
    std::array<double, 4> lonDiffs{};
    double                offset = 0;
    for (int i = 1; i < 5; i++) {
      if (screenBounds[i % 4][0] > screenBounds[i - 1][0]) {
        if (screenBounds[i % 4][0] - screenBounds[i - 1][0] < 180) {
          // 0  90  180 270 360
          // | x---x |   |   |
          //   1   2
          // West to east, dateline is not crossed
          currentBounds.mMaxLon = std::max(currentBounds.mMaxLon, screenBounds[i % 4][0] + offset);
          lonDiffs[i - 1]       = screenBounds[i % 4][0] - screenBounds[i - 1][0];
        } else {
          // 0  90  180 270 360
          // --x |   |   | x--
          //   1           2
          // East to west, dateline is crossed
          currentBounds.mMinLon = std::min(currentBounds.mMinLon + 360, screenBounds[i % 4][0]);
          currentBounds.mMaxLon = currentBounds.mMaxLon + 360;
          lonDiffs[i - 1]       = screenBounds[i % 4][0] - (screenBounds[i - 1][0] + 360);
        }
      } else {
        if (screenBounds[i - 1][0] - screenBounds[i % 4][0] < 180) {
          // 0  90  180 270 360
          // | x---x |   |   |
          //   2   1
          // East to west, dateline is not crossed
          currentBounds.mMinLon = std::min(currentBounds.mMinLon, screenBounds[i % 4][0] + offset);
          lonDiffs[i - 1]       = screenBounds[i % 4][0] - screenBounds[i - 1][0];
        } else {
          // 0  90  180 270 360
          // --x |   |   | x--
          //   2           1
          // West to East, dateline is crossed
          currentBounds.mMaxLon = std::max(currentBounds.mMaxLon, screenBounds[i % 4][0] + 360);
          offset                = 360;
          lonDiffs[i - 1]       = (screenBounds[i % 4][0] + 360) - screenBounds[i - 1][0];
        }
      }
    }
    if (currentBounds.mMaxLon > 360) {
      currentBounds.mMinLon -= 360;
      currentBounds.mMaxLon -= 360;
    }

    std::array<double, 4> lats{};
    std::transform(screenBounds.begin(), screenBounds.end(), lats.begin(),
        [](glm::dvec2 corner) { return corner[1]; });

    currentBounds.mMinLat = *std::min_element(lats.begin(), lats.end());
    currentBounds.mMaxLat = *std::max_element(lats.begin(), lats.end());

    // Check if the longitude range spans the whole earth, which would mean that one of the poles is
    // visible. >= 270 is used instead of >= 360 to prevent floating point errors.
    // As long as no pole is visible the maximum range should be 180 degrees, so this check can not
    // result in false positives.
    if (currentBounds.mMaxLon - currentBounds.mMinLon >= 270) {
      // 360 degree ranges other than [-180, 180] result in problems on some servers.
      currentBounds.mMinLon = -180.;
      currentBounds.mMaxLon = 180.;
      if (std::all_of(lonDiffs.begin(), lonDiffs.end(), [](double diff) { return diff > 0; })) {
        // West to east => north pole is visible
        currentBounds.mMaxLat = 90;
      } else if (std::all_of(
                     lonDiffs.begin(), lonDiffs.end(), [](double diff) { return diff < 0; })) {
        // East to west => south pole is visible
        currentBounds.mMinLat = -90;
      } else {
        logger().warn("Could not determine which pole is visible");
      }
    }

    pBounds = currentBounds;
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Bounds TextureOverlayRenderer::getBounds() const {
  return pBounds.get();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

WebCoverageTextureLoader::Request TextureOverlayRenderer::getRequest() {
  WebCoverageTextureLoader::Request request;
  request.mMaxSize = mPluginSettings->mMaxTextureSize.get();
  request.mBounds  = getBounds();
  request.layer    = mActiveLayer;
  request.mFormat  = mPluginSettings->mWcsRequestFormat.get();
  return request;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void TextureOverlayRenderer::getTimeIndependentTexture(
    WebCoverageTextureLoader::Request const& request) {
  if (mActiveWCSCoverage) {
    std::thread(std::function([this, request]() {
      mGuiManager->getGui()->callJavascript(
          "CosmoScout.wcsOverlays.setCoverageSelectDisabled", true);
      std::optional<GDALReader::GreyScaleTexture> texture = mTextureLoader.loadTexture(*mActiveWCS,
          *mActiveWCSCoverage, request, mPluginSettings->mMapCache.get(),
          request.mBounds == mActiveWCSCoverage->getSettings().mBounds);
      if (texture.has_value()) {
        mUpdateTexture = true;
        mTexture       = texture.value();
        mGuiManager->getGui()->callJavascript(
            "CosmoScout.wcsOverlays.setNumberOfLayers", mTexture.layers, request.layer.value_or(1));
      }
      mGuiManager->getGui()->callJavascript(
          "CosmoScout.wcsOverlays.setCoverageSelectDisabled", false);
    })).detach();
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void TextureOverlayRenderer::requestUpdateBounds() {
  mUpdateLonLatRange = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool TextureOverlayRenderer::Do() {
  if (mUpdateLonLatRange) {
    updateLonLatRange();
    mUpdateLonLatRange = false;
  }

  if (!mTexture.buffer) {
    return false;
  }

  // get active planet
  if (mSolarSystem->pActiveBody.get() == nullptr ||
      mSolarSystem->pActiveBody.get()->getCenterName() != "Earth") {
    return false;
  }

  if (mActiveWCSCoverage && !mActiveWCSCoverage->getSettings().mTimeIntervals.empty()) {
    // Get the current time. Pre-fetch times are related to this.
    auto time = cs::utils::convert::time::toPosix(mTimeControl->pSimulationTime.get());

    // Select WCS textures to be downloaded. If no pre-fetch is set, only sellect the texture for
    // the current timestep.
    for (int preFetch = -mPluginSettings->mPrefetchCount.get();
         preFetch <= mPluginSettings->mPrefetchCount.get(); preFetch++) {
      // Get the start time of the WCS sample.
      auto sampleStartTime =
          utils::addDurationToTime(time, mCurrentInterval.mSampleDuration, preFetch);
      sampleStartTime -= boost::posix_time::microseconds(time.time_of_day().fractional_seconds());
      bool inInterval = utils::timeInIntervals(
          sampleStartTime, mActiveWCSCoverage->getSettings().mTimeIntervals, mCurrentInterval);

      // Create identifier for the sample start time.
      std::string timeString = utils::timeToString(mCurrentInterval.mFormat, sampleStartTime);

      auto requestedTexture = mTexturesBuffer.find(timeString);
      auto loadedTexture    = mTextures.find(timeString);
      auto wrongTexture     = std::find(mWrongTextures.begin(), mWrongTextures.end(), timeString);

      // Only load textures that aren't stored yet.
      if (requestedTexture == mTexturesBuffer.end() && loadedTexture == mTextures.end() &&
          wrongTexture == mWrongTextures.end() && inInterval) {

        // Load WCS texture.
        WebCoverageTextureLoader::Request request;
        request.mTime    = timeString;
        request.mBounds  = getBounds();
        request.mMaxSize = mPluginSettings->mMaxTextureSize.get();
        request.layer    = mActiveLayer;

        mTexturesBuffer.insert(
            std::pair<std::string, std::future<std::optional<GDALReader::GreyScaleTexture>>>(
                timeString, mTextureLoader.loadTextureAsync(*mActiveWCS, *mActiveWCSCoverage,
                                request, mPluginSettings->mMapCache.get(),
                                request.mBounds == mActiveWCSCoverage->getSettings().mBounds)));
      }
    }

    // Check whether the WCS textures are loaded to the memory.
    auto texIt = mTexturesBuffer.begin();
    while (texIt != mTexturesBuffer.end()) {
      if (texIt->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::optional<GDALReader::GreyScaleTexture> texture = texIt->second.get();

        if (texture.has_value()) {
          mTextures.insert(
              std::pair<std::string, GDALReader::GreyScaleTexture>(texIt->first, texture.value()));
        } else {
          mWrongTextures.emplace_back(texIt->first);
        }

        texIt = mTexturesBuffer.erase(texIt);
      } else {
        ++texIt;
      }
    }

    // Get the current time.
    time = cs::utils::convert::time::toPosix(mTimeControl->pSimulationTime.get());
    boost::posix_time::ptime sampleStartTime =
        time - boost::posix_time::microseconds(time.time_of_day().fractional_seconds());
    bool inInterval = utils::timeInIntervals(
        sampleStartTime, mActiveWCSCoverage->getSettings().mTimeIntervals, mCurrentInterval);

    // Create identifier for the sample start time.
    std::string timeString = utils::timeToString(mCurrentInterval.mFormat, sampleStartTime);

    // Find the current texture.
    auto tex = mTextures.find(timeString);

    // Use WCS texture inside the interval.
    if (inInterval && tex != mTextures.end()) {
      // Only update if we have a new texture.
      if (mCurrentTexture != timeString) {
        mUpdateTexture  = true;
        mTexture        = tex->second;
        mCurrentTexture = timeString;
      }
    } // Use default planet texture instead.
    else {
      mCurrentTexture = "";
    }
  }

  // save current lighting and material state of the OpenGL state machine
  glPushAttrib(GL_POLYGON_BIT | GL_ENABLE_BIT);
  glEnable(GL_TEXTURE_2D);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);

  double nearClip = NAN;
  double farClip  = NAN;
  GetVistaSystem()
      ->GetDisplayManager()
      ->GetCurrentRenderInfo()
      ->m_pViewport->GetProjection()
      ->GetProjectionProperties()
      ->GetClippingRange(nearClip, farClip);

  // copy depth buffer from previous rendering
  // -------------------------------------------------------
  std::array<GLint, 4> iViewport{};
  glGetIntegerv(GL_VIEWPORT, iViewport.data());

  auto* viewport = GetVistaSystem()->GetDisplayManager()->GetCurrentRenderInfo()->m_pViewport;
  auto& data     = mGBufferData[viewport];

  data.mDepthBuffer->Bind();
  glCopyTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_DEPTH_COMPONENT, iViewport[0], iViewport[1],
      iViewport[2], iViewport[3], 0);

  if (mUpdateTexture) {
    data.mColorBuffer->Bind();

    nlohmann::json sampleJson;

    switch (mTexture.type) {
    case 1: // UInt8
    {
      std::vector<uint8_t> textureData(
          (uint8_t*)mTexture.buffer, (uint8_t*)(mTexture.buffer) + (mTexture.x * mTexture.y));
      sampleJson = textureData;

      glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, mTexture.x, mTexture.y, 0, GL_RED, GL_UNSIGNED_BYTE,
          (void*)mTexture.buffer);
      break;
    }

    case 2: // UInt16
    {
      std::vector<uint16_t> textureData(
          (uint16_t*)mTexture.buffer, (uint16_t*)(mTexture.buffer) + (mTexture.x * mTexture.y));
      sampleJson = textureData;

      glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, mTexture.x, mTexture.y, 0, GL_RED, GL_UNSIGNED_SHORT,
          (void*)mTexture.buffer);
      break;
    }

    case 3: // Int16
    {
      std::vector<int16_t> textureData(
          (int16_t*)mTexture.buffer, (int16_t*)(mTexture.buffer) + (mTexture.x * mTexture.y));
      sampleJson = textureData;

      glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, mTexture.x, mTexture.y, 0, GL_RED, GL_SHORT,
          (void*)mTexture.buffer);
      break;
    }

    case 4: // UInt32
    {
      std::vector<uint32_t> textureData(
          (uint32_t*)mTexture.buffer, (uint32_t*)(mTexture.buffer) + (mTexture.x * mTexture.y));
      sampleJson = textureData;

      glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, mTexture.x, mTexture.y, 0, GL_RED, GL_UNSIGNED_INT,
          (void*)mTexture.buffer);
      break;
    }

    case 5: // Int32
    {
      std::vector<int32_t> textureData(
          (int32_t*)mTexture.buffer, (int32_t*)(mTexture.buffer) + (mTexture.x * mTexture.y));
      sampleJson = textureData;

      glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, mTexture.x, mTexture.y, 0, GL_RED, GL_INT,
          (void*)mTexture.buffer);
      break;
    }

    case 6: // Float32
    case 7: // Float64
    {
      std::vector<float> textureData(
          (float*)mTexture.buffer, (float*)(mTexture.buffer) + (mTexture.x * mTexture.y));
      sampleJson = textureData;
      glTexImage2D(
          GL_TEXTURE_2D, 0, GL_R32F, mTexture.x, mTexture.y, 0, GL_RED, GL_FLOAT, mTexture.buffer);
      break;
    }

    default:
      logger().error("Texture has no known data type.");
    }

    if (sampleJson) {
      // Texture encoded as json. Used to generate the histogram
      mGuiManager->getGui()->callJavascript(
          "CosmoScout.wcsOverlays._transferFunction.setData", sampleJson);
    }

    mUpdateTexture = false;
  }

  // get matrices and related values -----------------------------------------
  GLfloat glMatP[16];
  GLfloat glMatMV[16];
  glGetFloatv(GL_PROJECTION_MATRIX, &glMatP[0]);
  glGetFloatv(GL_MODELVIEW_MATRIX, &glMatMV[0]);

  std::string closestPlanet     = mSolarSystem->pActiveBody.get()->getCenterName();
  auto        activeBody        = mSolarSystem->pActiveBody.get();
  glm::dmat4  matWorldTransform = activeBody->getWorldTransform();

  VistaTransformMatrix matM(glm::value_ptr(matWorldTransform), true);
  VistaTransformMatrix matMV(matM);
  VistaTransformMatrix matInvMV(matMV.GetInverted());
  VistaTransformMatrix matInvP(VistaTransformMatrix(glMatP, true).GetInverted());
  VistaTransformMatrix matInvMVP(matInvMV * matInvP);
  // get matrices and related values -----------------------------------------

  // Bind shader before draw
  m_pSurfaceShader->Bind();

  data.mDepthBuffer->Bind(GL_TEXTURE0);
  data.mColorBuffer->Bind(GL_TEXTURE1);

  mTransferFunction->bind(GL_TEXTURE2);

  m_pSurfaceShader->SetUniform(
      m_pSurfaceShader->GetUniformLocation("uDataTypeSize"), mTexture.typeSize);

  m_pSurfaceShader->SetUniform(m_pSurfaceShader->GetUniformLocation("uDepthBuffer"), 0);
  m_pSurfaceShader->SetUniform(m_pSurfaceShader->GetUniformLocation("uSimBuffer"), 1);

  m_pSurfaceShader->SetUniform(m_pSurfaceShader->GetUniformLocation("uTransferFunction"), 2);

  glm::dmat4 InverseWorldTransform = glm::inverse(matWorldTransform);
  GLint      loc                   = m_pSurfaceShader->GetUniformLocation("uMatInvMV");
  glUniformMatrix4dv(loc, 1, GL_FALSE, glm::value_ptr(InverseWorldTransform));
  loc = m_pSurfaceShader->GetUniformLocation("uMatInvMVP");
  glUniformMatrix4fv(loc, 1, GL_FALSE, matInvMVP.GetData());
  loc = m_pSurfaceShader->GetUniformLocation("uMatInvP");
  glUniformMatrix4fv(loc, 1, GL_FALSE, matInvP.GetData());
  loc = m_pSurfaceShader->GetUniformLocation("uMatMV");
  glUniformMatrix4fv(loc, 1, GL_FALSE, matMV.GetData());

  m_pSurfaceShader->SetUniform(
      m_pSurfaceShader->GetUniformLocation("uFarClip"), static_cast<float>(farClip));

  // Double precision bounds
  loc = m_pSurfaceShader->GetUniformLocation("uLatRange");
  glUniform2dv(loc, 1,
      glm::value_ptr(
          cs::utils::convert::toRadians(glm::dvec2(getBounds().mMinLat, getBounds().mMaxLat))));
  loc = m_pSurfaceShader->GetUniformLocation("uLonRange");
  glUniform2dv(loc, 1,
      glm::value_ptr(
          cs::utils::convert::toRadians(glm::dvec2(getBounds().mMinLon, getBounds().mMaxLon))));

  m_pSurfaceShader->SetUniform(m_pSurfaceShader->GetUniformLocation("uRange"),
      static_cast<float>(mTexture.dataRange[0]), static_cast<float>(mTexture.dataRange[1]));

  // From Application.cpp
  auto*               pSG = GetVistaSystem()->GetGraphicsManager()->GetSceneGraph();
  VistaTransformNode* pTrans =
      dynamic_cast<VistaTransformNode*>(pSG->GetNode("Platform-User-Node"));

  auto vWorldPos = glm::vec4(1);
  pTrans->GetWorldPosition(vWorldPos.x, vWorldPos.y, vWorldPos.z);

  auto sunDirection =
      glm::normalize(glm::inverse(matWorldTransform) *
                     (mSolarSystem->getSun()->getWorldTransform()[3] - matWorldTransform[3]));
  m_pSurfaceShader->SetUniform(m_pSurfaceShader->GetUniformLocation("uSunDirection"),
      (float)sunDirection[0], (float)sunDirection[1], (float)sunDirection[2]);

  // provide radii to shader
  auto mRadii = cs::core::SolarSystem::getRadii(mSolarSystem->pActiveBody.get()->getCenterName());
  m_pSurfaceShader->SetUniform(m_pSurfaceShader->GetUniformLocation("uRadii"),
      static_cast<float>(mRadii[0]), static_cast<float>(mRadii[1]), static_cast<float>(mRadii[2]));

  int depthBits = 0;
  glGetIntegerv(GL_DEPTH_BITS, &depthBits);

  // Dummy draw
  glDrawArrays(GL_POINTS, 0, 1);

  data.mDepthBuffer->Unbind(GL_TEXTURE0);
  data.mColorBuffer->Unbind(GL_TEXTURE1);

  mTransferFunction->unbind(GL_TEXTURE2);

  // Release shader
  m_pSurfaceShader->Release();

  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glPopAttrib();
  return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool TextureOverlayRenderer::GetBoundingBox(VistaBoundingBox& oBoundingBox) {
  oBoundingBox.SetBounds(mMinBounds.data(), mMaxBounds.data());
  return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void TextureOverlayRenderer::setTransferFunction(std::string json) {
  mTransferFunction = std::make_unique<cs::graphics::ColorMap>(json);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void TextureOverlayRenderer::setLayer(int layer) {
  mActiveLayer = layer;
  getTimeIndependentTexture(getRequest());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace csp::wcsoverlays