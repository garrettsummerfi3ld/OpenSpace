/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2021                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#include <modules/fieldlinessequence/rendering/renderablemovingfieldlines.h>

#include <modules/fieldlinessequence/fieldlinessequencemodule.h>
#include <modules/kameleon/include/kameleonhelper.h>
#include <openspace/documentation/documentation.h>
#include <openspace/engine/globals.h>
#include <openspace/rendering/renderengine.h>
#include <openspace/util/updatestructures.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/opengl/openglstatecache.h>
#include <ghoul/opengl/textureunit.h>

#include <ccmc/Kameleon.h>
#include <ccmc/KameleonInterpolator.h>

#include <fstream>
#pragma optimize("", off)

namespace {
    constexpr const char* _loggerCat = "RenderableMovingFieldlines";

    constexpr openspace::properties::Property::PropertyInfo LineWidthInfo = {
    "LineWidth",
    "Line Width",
    "This value specifies the line width of the fieldlines"
    };
    constexpr openspace::properties::Property::PropertyInfo RenderFlowLineInfo = {
    "RenderFlowLine",
    "Render Flow Line",
    "Checked means flow line will be rendered"
    };
    constexpr openspace::properties::Property::PropertyInfo ColorMethodInfo = {
    "ColorMethod",
    "Color Method",
    "Color lines uniformly or using color tables based on extra quantities like, for "
    "examples, temperature or particle density."
    };
    constexpr openspace::properties::Property::PropertyInfo ColorUniformInfo = {
    "Uniform",
    "Uniform Line Color",
    "The uniform color of lines shown when 'Color Method' is set to 'Uniform'."
    };
    constexpr openspace::properties::Property::PropertyInfo ColorQuantityInfo = {
    "ColorQuantity",
    "Quantity to Color By",
    "Quantity used to color lines if the 'By Quantity' color method is selected."
    };
    constexpr openspace::properties::Property::PropertyInfo ColorMinMaxInfo = {
    "ColorQuantityMinMax",
    "ColorTable Min Value",
    "Value to map to the lowest and highest end of the color table."
    };
    constexpr openspace::properties::Property::PropertyInfo ColorTablePathInfo = {
    "ColorTablePath",
    "Path to Color Table",
    "Color Table/Transfer Function to use for 'By Quantity' coloring."
    };

    struct [[codegen::Dictionary(RenderableMovingFieldlines)]] Parameters {
        // Path to folder containing the input .cdf files
        std::filesystem::path sourceFolder [[codegen::directory()]];
        // Path to file with seed points
        std::filesystem::path seedPointFile;
        // Extra variables such as rho, p or t
        std::optional<std::vector<std::string>> extraVariables;
        // Which variable in CDF file to trace. 'u_perp_b' is default. 
        // u is for flow, b is for magnetic field. perp meaning perpendicular
        std::optional<std::string> tracingVariable;
        // The number of points on the 'path line' which fieldlines will be moving
        std::optional<int> numberOfPointsOnPathLine;
        // The number of points on the fieldlines that uses the 'path line' vertecis
        // as seed points
        std::optional<int> numberOfPointsOnFieldlines;
        // [[codegen::verbatim(LineWidthInfo.description)]]
        std::optional<float> lineWidth;
        // [[codegen::verbatim(RenderFlowLineInfo.description)]]
        std::optional<bool> renderFlowLine;
        // [[codegen::verbatim(ColorMethodInfo.description)]]
        std::optional<std::string> colorMethod;
        // List of ranges for which their corresponding parameters values will be 
        // colorized by. Should be entered as {min value, max value} per range
        std::optional<std::vector<glm::vec2>> colorTableRanges;
        // [[codegen::verbatim(ColorQuantityInfo.description)]]
        std::optional<int> colorQuantity;
        // A list of paths to transferfunction .txt files containing color tables
        // used for colorizing the fieldlines according to different parameters
        std::optional<std::vector<std::string>> colorTablePaths;
        // If data sets parameter start_time differ from start of run,
        // elapsed_time_in_seconds might be in relation to start of run.
        // ManuelTimeOffset will be added to trigger time.
        // TODO should not be any need for manual time offset because 
        // we only want one state
        std::optional<double> manualTimeOffset;
    };
#include "renderablemovingfieldlines_codegen.cpp"
} // namespace

namespace openspace {
    std::tuple<std::vector<glm::vec3>, std::vector<double>> extractSeedPointsFromFile(
        std::filesystem::path filePath,
        std::vector<glm::vec3>& seedPoints,
        std::vector<double>& birthTimes,
        double startTime
    );

    std::vector<std::string> extractMagnitudeVarsFromStrings(std::vector<std::string> extrVars);

    documentation::Documentation RenderableMovingFieldlines::Documentation() {
        return codegen::doc<Parameters>("fieldlinessequence_renderablemovingfieldlines");
    }

    glm::vec3 lerp(glm::vec3 current, glm::vec3 next, float t) {
        return current * (1.f - t) + next * t;
    }

    RenderableMovingFieldlines::RenderableMovingFieldlines(
        const ghoul::Dictionary& dictionary)
        :Renderable(dictionary)
        , _colorGroup({ "Color" })
        , _lineWidth(LineWidthInfo, 1.f, 1.f, 20.f)
        , _renderFlowLine(RenderFlowLineInfo, false)
        , _colorMethod(ColorMethodInfo, properties::OptionProperty::DisplayType::Radio)
        , _colorUniform(
            ColorUniformInfo,
            glm::vec4(0.3f, 0.57f, 0.75f, 0.5f),
            glm::vec4(0.f),
            glm::vec4(1.f)
        )
        , _colorQuantity(ColorQuantityInfo, properties::OptionProperty::DisplayType::Dropdown)
        , _colorQuantityMinMax(
            ColorMinMaxInfo,
            glm::vec2(-0.f, 100.f),
            glm::vec2(-5000.f),
            glm::vec2(5000.f)
        )
        , _colorTablePath(ColorTablePathInfo) {
        const Parameters p = codegen::bake<Parameters>(dictionary);

        std::filesystem::path sourceFolder = p.sourceFolder;
        if (!std::filesystem::is_directory(sourceFolder)) {
            LERROR(fmt::format(
                "MovingFieldlines {} is not a valid directory",
                sourceFolder.string()
            ));
        }
        namespace fs = std::filesystem;
        for (const fs::directory_entry& e : fs::directory_iterator(sourceFolder)) {
            if (e.is_regular_file() && e.path().extension() == ".cdf") {
                std::string eStr = e.path().string();
                _sourceFiles.push_back(eStr);
            }
        }
        if (_sourceFiles.empty()) {
            throw ghoul::RuntimeError(fmt::format(
                "{} contains no .cdf files",
                sourceFolder.string()
            ));
        }
        std::sort(_sourceFiles.begin(), _sourceFiles.end());

        _seedFilePath = p.seedPointFile;
        _extraVars = p.extraVariables.value_or(_extraVars);
        _manualTimeOffset = p.manualTimeOffset.value_or(_manualTimeOffset);
        _tracingVariable = p.tracingVariable.value_or(_tracingVariable);
        if (p.numberOfPointsOnPathLine.has_value()) {
            //TODO Check if nPointsOnPathLine = 100 if not specified
            if (p.numberOfPointsOnPathLine.value() < 0) {
                LWARNING(fmt::format("Number of points on path line: {}, must be positive "
                    "and have therefor been replaced with the default 200 points",
                    p.numberOfPointsOnPathLine.value()
                ));
            }
            else {
                _nPointsOnPathLine = static_cast<size_t>(p.numberOfPointsOnPathLine.value());
            }
        }

        if (p.numberOfPointsOnFieldlines.has_value()) {
            //TODO Check if nPointsOnFieldlines = 100 if not specified
            if (p.numberOfPointsOnFieldlines.value() < 0) {
                LWARNING(fmt::format("Number of points on fieldlines: {}, must be positive "
                    "and have therefor been replaced with the default 100 points",
                    p.numberOfPointsOnFieldlines.value()
                ));
            }
            else {
                _nPointsOnFieldlines =
                    static_cast<size_t>(p.numberOfPointsOnFieldlines.value());
            }
        }

        _lineWidth = p.lineWidth.value_or(_lineWidth);
        _renderFlowLine = p.renderFlowLine.value_or(_renderFlowLine);
        _colorTablePaths = p.colorTablePaths.value_or(_colorTablePaths);

        _colorMethod.addOption(static_cast<int>(ColorMethod::Uniform), "Uniform");
        _colorMethod.addOption(static_cast<int>(ColorMethod::ByQuantity), "By Quantity");
        if (p.colorMethod.has_value()) {
            if (p.colorMethod.value() == "Uniform") {
                _colorMethod = static_cast<int>(ColorMethod::Uniform);
            }
            else {
                _colorMethod = static_cast<int>(ColorMethod::ByQuantity);
            }
        }
        else {
            _colorMethod = static_cast<int>(ColorMethod::Uniform);
        }

        if (p.colorTableRanges.has_value()) {
            _colorTableRanges = *p.colorTableRanges;
        }
        else {
            _colorTableRanges.push_back(glm::vec2(0.f, 1.f));
        }

        if (p.colorQuantity.has_value()) {
            _colorMethod = static_cast<int>(ColorMethod::ByQuantity);
            _colorQuantityTemp = *p.colorQuantity;
        }
    }

    void RenderableMovingFieldlines::initialize() {
        // Set a default color table, just in case the (optional) user defined paths are
        // corrupt or not provided
        _colorTablePaths.push_back(FieldlinesSequenceModule::DefaultTransferFunctionFile);
        _transferFunction = std::make_unique<TransferFunction>(
            absPath(_colorTablePaths[0]).string()
            );

        bool stateSuccuess = getStateFromCdfFiles();
        if (!stateSuccuess) {
            throw ghoul::RuntimeError("Trying to read cdf file failed");
        }

        size_t traverserIndex = 0;

        for (const FieldlinesState::MatchingFieldlines& mf : _fieldlineState.getAllMatchingFieldlines()) {
            // calculate time here and chose start position for _traverser
            _traversers.push_back(PathLineTraverser{ const_cast<std::vector<openspace::FieldlinesState::Fieldline>&>(mf.pathLines.first.keyFrames) });
            double timeToReconTrav1 = _traversers[traverserIndex].getTimeToReconnectionPoint(mf.pathLines.first.daysideReconnectionStart);

            _traversers.push_back(PathLineTraverser{ const_cast<std::vector<openspace::FieldlinesState::Fieldline>&>(mf.pathLines.second.keyFrames) });
            double timeToReconTrav2 = _traversers[traverserIndex + 1].getTimeToReconnectionPoint(mf.pathLines.second.daysideReconnectionStart);

            // find out which traverser has the longest traveling time to
            // point of reconnection and set the startpoint of it
            if (timeToReconTrav1 > timeToReconTrav2) {
                _traversers[traverserIndex].setStartPoint(timeToReconTrav2, mf.pathLines.first.daysideReconnectionStart);
            }
            else {
                _traversers[traverserIndex + 1].setStartPoint(timeToReconTrav1, mf.pathLines.second.daysideReconnectionStart);
            }

            // initialize the temporary key frame for each traverser
            _traversers[traverserIndex].temporaryInterpolationKeyFrame.vertices = std::vector(_nPointsOnFieldlines, glm::vec3{});
            _traversers[traverserIndex + 1].temporaryInterpolationKeyFrame.vertices = std::vector(_nPointsOnFieldlines, glm::vec3{});
            //_traversers[traverserIndex + 1].temporaryInterpolationKeyFrame.vertices.reserve(_nPointsOnFieldlines);

            traverserIndex += 2;
        }

        _renderedLines = _fieldlineState.vertexPositions();
        _debugTopologyColor = std::vector<float>(_renderedLines.size(), -1.f);
        size_t nExtraQuantities = _fieldlineState.nExtraQuantities();

        // Each fieldline starts unrendered; changes depending on lifetime in update()
        _renderedLinesAlpha = std::vector<float>(_renderedLines.size(), 0.0f);

        addProperty(_lineWidth);
        addProperty(_renderFlowLine);
        addPropertySubOwner(_colorGroup);
        _colorUniform.setViewOption(properties::Property::ViewOptions::Color);
        _colorGroup.addProperty(_colorUniform);

        if (nExtraQuantities > 0) {
            _colorQuantity = _colorQuantityTemp;
            _colorQuantityMinMax = _colorTableRanges[_colorQuantity];
            _colorQuantityMinMax.setViewOption(
                properties::Property::ViewOptions::MinMaxRange
            );
            _colorGroup.addProperty(_colorQuantityMinMax);
            _colorGroup.addProperty(_colorMethod);
            _colorGroup.addProperty(_colorQuantity);
            _colorGroup.addProperty(_colorTablePath);

            const std::vector<std::string>& extraNames = _fieldlineState.extraQuantityNames();
            for (int i = 0; i < static_cast<int>(nExtraQuantities); ++i) {
                _colorQuantity.addOption(i, extraNames[i]);
            }

            _colorTableRanges.resize(nExtraQuantities, _colorTableRanges.back());
            _colorTablePaths.resize(nExtraQuantities, _colorTablePaths.back());

            _colorTablePath = _colorTablePaths[0];

            _colorQuantity.onChange([this]() {
                _shouldUpdateColorBuffer = true;
                _colorQuantityMinMax = _colorTableRanges[_colorQuantity];
                _colorTablePath = _colorTablePaths[_colorQuantity];
                });
            _colorTablePath.onChange([this]() {
                _transferFunction->setPath(_colorTablePath);
                _colorTablePaths[_colorQuantity] = _colorTablePath;
                });
            _colorQuantityMinMax.onChange([this]() {
                _colorTableRanges[_colorQuantity] = _colorQuantityMinMax;
                });
        }
    }

    void RenderableMovingFieldlines::initializeGL() {
        _shaderProgram = global::renderEngine->buildRenderProgram(
            "MovingFieldlines",
            absPath("${MODULE_FIELDLINESSEQUENCE}/shaders/movingfieldlines_vs.glsl"),
            absPath("${MODULE_FIELDLINESSEQUENCE}/shaders/movingfieldlines_fs.glsl")
        );
        glGenVertexArrays(1, &_vertexArrayObjectFlow);
        glGenBuffers(1, &_vertexPositionBufferFlow);
        glGenVertexArrays(1, &_vertexArrayObject);
        glGenBuffers(1, &_vertexPositionBuffer);
        glGenBuffers(1, &_vertexColorBuffer);
        glGenBuffers(1, &_vertexAlphaBuffer);

        // Needed for additive blending
        setRenderBin(Renderable::RenderBin::Overlay);
    }

    bool RenderableMovingFieldlines::getStateFromCdfFiles() {

        std::vector<std::string> extraMagVars = extractMagnitudeVarsFromStrings(_extraVars);

        std::vector<glm::vec3> seedPoints;
        std::vector<double> birthTimes;

        bool isSuccessful = false;
        bool shouldExtractSeedPoints = true;
        double startTime = 0.0;

        for (const std::filesystem::path entry : _sourceFiles) {
            const std::string& cdfPath = entry.string();

            std::unique_ptr<ccmc::Kameleon> kameleon =
                kameleonHelper::createKameleonObject(cdfPath);

            _fieldlineState.setModel(fls::stringToModel(kameleon->getModelName()));

            // We only need to extract seed points once, but we need the start time of the first
            // CDF-file to create correct birth times for the points.
            if (shouldExtractSeedPoints) {

                // Get start_time from cdf and convert it to double; needed to give seed points
                // give in date-format their proper birthTime.
                bool hasStartTime = kameleon->doesAttributeExist("start_time");
                if (hasStartTime) {
                    std::string startTimeStr =
                        kameleon->getGlobalAttribute("start_time").getAttributeString();
                    startTime = Time::convertTime(startTimeStr.substr(0, startTimeStr.length() - 2));
                }
                else {
                    LWARNING(fmt::format(
                        "The first CDF-file at path {} does not have a \"start_time\"-attribute",
                        cdfPath
                    ));
                }

                extractSeedPointsFromFile(_seedFilePath, seedPoints, birthTimes, startTime);
                shouldExtractSeedPoints = false;
            }

            /************ SWITCH MOVING/MATCHING HERE ****************/
            //isSuccessful = fls::convertCdfToMovingFieldlinesState(
            isSuccessful = fls::convertCdfToMatchingFieldlinesState(
            /*********************************************************/
                _fieldlineState,
                kameleon.get(),
                seedPoints,
                birthTimes,
                _manualTimeOffset,
                _tracingVariable,
                _extraVars,
                extraMagVars,
                _nPointsOnPathLine,
                _nPointsOnFieldlines
            );
        }
        //_fieldlineState.addLinesToBeRendered();
        _fieldlineState.initializeRenderedMatchingFieldlines();

        if (isSuccessful) {
            switch (_fieldlineState.model()) {
            case fls::Model::Batsrus:
                _fieldlineState.scalePositions(fls::ReToMeter);
                break;
            default:
                throw ghoul::MissingCaseException();
            }
        }

        return isSuccessful;
    }


    void RenderableMovingFieldlines::deinitializeGL() {
        glDeleteVertexArrays(1, &_vertexArrayObject);
        _vertexArrayObject = 0;

        glDeleteVertexArrays(1, &_vertexArrayObjectFlow);
        _vertexArrayObjectFlow = 0;

        glDeleteBuffers(1, &_vertexPositionBuffer);
        _vertexPositionBuffer = 0;

        glDeleteBuffers(1, &_vertexColorBuffer);
        _vertexColorBuffer = 0;

        glDeleteBuffers(1, &_vertexAlphaBuffer);
        _vertexAlphaBuffer = 0;

        if (_shaderProgram) {
            global::renderEngine->removeRenderProgram(_shaderProgram.get());
            _shaderProgram = nullptr;
        }
    }

    /**
    * Extracts seed points from .txt file. Each matching fieldline pair is created
    * by giving a time and four seed points on five separate lines. The time states
    * when the fieldline should start, the two first seed points gives the matching
    * IMF and closed fieldline and the last two gives their respective open fieldlines.
    */
    std::tuple<std::vector<glm::vec3>, std::vector<double>> extractSeedPointsFromFile(
        std::filesystem::path filePath,
        std::vector<glm::vec3>& seedPoints,
        std::vector<double>& birthTimes,
        double startTime
    ) {

        if (!std::filesystem::is_regular_file(filePath) ||
            filePath.extension() != ".txt")
        {
            // TODO: add support for whatever actual seepoint files are being used
            throw ghoul::RuntimeError(fmt::format("SeedPointFile needs to be a .txt file"));
        }
        std::string seedFile = filePath.string();
        std::ifstream seedFileStream(seedFile);
        if (!seedFileStream.good()) {
            throw ghoul::RuntimeError(fmt::format("Could not read from {}", seedFile));
        }

        std::string line;
        while (std::getline(seedFileStream, line)) {
            try {
                std::stringstream ss(line);

                bool isPoint = ss.str().find_first_of(' ') != std::string::npos;
                bool isDate = ss.str().find_first_of(':') != std::string::npos;

                // Interprets line as point if it includes space
                if (isPoint) {

                    glm::vec3 point;
                    ss >> point.x;
                    ss >> point.y;
                    ss >> point.z;
                    seedPoints.push_back(point);

                    bool isEmpty = ss.eof();
                    if (!isEmpty) {
                        throw std::invalid_argument("stringstream was not empty"
                            " after reading point");
                    }
                    bool hasFailed = ss.fail();
                    if (hasFailed) {
                        throw std::invalid_argument("failed to read float value "
                            "from stringstream when reading point");
                    }
                }
                // Time in date format
                else if (isDate) {

                    double t = Time::convertTime(ss.str().substr(0, ss.str().length() - 2));

                    birthTimes.push_back(t);
                }
                // Time in float format
                else {

                    double t;
                    ss >> t;
                    birthTimes.push_back(startTime + t);

                    bool isEmpty = ss.eof();
                    if (!isEmpty) {
                        throw std::invalid_argument("stringstream was not empty after "
                            "reading time in float format");
                    }
                    bool hasFailed = ss.fail();
                    if (hasFailed) {
                        throw std::invalid_argument("failed to read float value from "
                            "stringstream when reading time");
                    }
                }

                // add birth time if none was given
                if (std::floor(seedPoints.size() / 4) > birthTimes.size()) {
                    birthTimes.push_back(startTime);
                }
            }
            catch (std::invalid_argument& e) {
                LERROR(fmt::format(
                    "Incorrect format of seed point file, got error: {}",
                    e.what()
                ));
                throw ghoul::RuntimeError(
                    fmt::format(
                        "Incorrect format of seed point file, got error: {}",
                        e.what()
                    ));
            }
        }
        if (seedPoints.empty()) {
            throw ghoul::RuntimeError(fmt::format(
                "Found no seed points in: {}",
                filePath
            ));
        }

        return std::make_tuple(seedPoints, birthTimes);
    }

    bool RenderableMovingFieldlines::isReady() const {
        return _shaderProgram != nullptr;
    }

    void RenderableMovingFieldlines::render(const RenderData& data, RendererTasks&) {
        _shaderProgram->activate();

        // Calculate Model View MatrixProjection
        const glm::dmat4 rotMat = glm::dmat4(data.modelTransform.rotation);
        const glm::dmat4 modelMat =
            glm::translate(glm::dmat4(1.0), data.modelTransform.translation) *
            rotMat *
            glm::dmat4(glm::scale(glm::dmat4(1), glm::dvec3(data.modelTransform.scale)));
        const glm::dmat4 modelViewMat = data.camera.combinedViewMatrix() * modelMat;

        _shaderProgram->setUniform("modelViewProjection",
            data.camera.sgctInternal.projectionMatrix() * glm::mat4(modelViewMat));
        _shaderProgram->setUniform("lineColor", _colorUniform);
        _shaderProgram->setUniform("colorMethod", _colorMethod);

        if (_colorMethod == static_cast<int>(ColorMethod::ByQuantity)) {
            ghoul::opengl::TextureUnit textureUnit;
            textureUnit.activate();
            _transferFunction->bind(); // Calls update internally
            _shaderProgram->setUniform("colorTable", textureUnit);
            _shaderProgram->setUniform("colorTableRange", _colorTableRanges[_colorQuantity]);
        }

        glBindVertexArray(_vertexArrayObject);
#ifndef __APPLE__
        glLineWidth(_lineWidth);
#else
        glLineWidth(1.f);
#endif

        std::vector<int> lineLengths = _fieldlineState.lineCount();
        std::vector<int> lineStarts = _fieldlineState.lineStart();

        // code used for matching fieldlines
        if (_renderFlowLine) {
            // start is the index for the starting vertex of the next added
            // pathline found in the vector containing all vertices
            int start = lineStarts.back() + _nPointsOnFieldlines;

            for (int i = 0; i < _fieldlineState.getAllMatchingFieldlines().size(); i++) {
                lineLengths.push_back(_nPointsOnPathLine);
                lineStarts.push_back(start);
                start += _nPointsOnPathLine;

                lineLengths.push_back(_nPointsOnPathLine);
                lineStarts.push_back(start);
                start += _nPointsOnPathLine;
            }
        }

        // Something like this is needed for fieldline fade to work. This doesn't though...
        //glEnable(GL_BLEND); 
        //glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);  

        glMultiDrawArrays(
            GL_LINE_STRIP,
            lineStarts.data(),
            lineLengths.data(),
            static_cast<GLsizei>(lineStarts.size())
        );

        glBindVertexArray(0);
        _shaderProgram->deactivate();

        // Restores OpenGL Rendering State
        global::renderEngine->openglStateCache().resetBlendState();
        global::renderEngine->openglStateCache().resetDepthState();
    }

    void RenderableMovingFieldlines::update(const UpdateData& data) {
        if (_shaderProgram->isDirty()) {
            _shaderProgram->rebuildFromFile();
        }

        const double previousTime = data.previousFrameTime.j2000Seconds();
        const double currentTime = data.time.j2000Seconds();

        moveLines(currentTime, previousTime);

        updateVertexPositionBuffer();
        if (_fieldlineState.nExtraQuantities() > 0) {
            updateVertexColorBuffer();
        }

        updateVertexAlphaBuffer(currentTime);
    }

    void RenderableMovingFieldlines::moveLine(const double dt,
        const FieldlinesState::PathLine& pathLine,
        PathLineTraverser& traverser, GLint lineStart,
        GLsizei nVertices) 
    {
        bool forward = std::signbit(dt) ? false : true;
        traverser.forward = forward;
        traverser.timeSinceInterpolation += dt;

        // if passing next or previous key frame
        bool passNext = forward ?
            traverser.timeSinceInterpolation >
            traverser.timeInterpolationDenominator :
        traverser.timeSinceInterpolation < 0.0;

        if (passNext) {

            if (traverser.hasTemporaryKeyFrame) {
                traverser.hasTemporaryKeyFrame = false;
                // Empty the vector without deallocating memory
                //traverser.temporaryInterpolationKeyFrame.vertices.resize(0);
            }

            traverser.advanceKeyFrames();
        }

        auto debugColor = [](FieldlinesState::Fieldline::Topology topology) {
            switch (topology)
            {
            case FieldlinesState::Fieldline::Topology::Closed:
                return 0.f;
            case FieldlinesState::Fieldline::Topology::Open:
                return 1.f;
            case FieldlinesState::Fieldline::Topology::Imf:
                return -0.5f;
            }
        };

        
        double normalizedTime = 0.0;
        normalizedTime = traverser.timeSinceInterpolation /
            traverser.timeInterpolationDenominator;

        for (size_t fieldlineVertex = 0; fieldlineVertex < nVertices; ++fieldlineVertex) {
            glm::vec3 currentPosition, nextPosition;
            if (forward) {
                currentPosition = traverser.hasTemporaryKeyFrame ?
                    traverser.temporaryInterpolationKeyFrame.vertices[fieldlineVertex] :
                    traverser.backKeyFrame->vertices[fieldlineVertex];

                nextPosition = 
                    traverser.frontKeyFrame->vertices[fieldlineVertex];
            }
            else {
                currentPosition = 
                    traverser.backKeyFrame->vertices[fieldlineVertex];

                nextPosition = traverser.hasTemporaryKeyFrame ?
                    traverser.temporaryInterpolationKeyFrame.vertices[fieldlineVertex] :
                    traverser.frontKeyFrame->vertices[fieldlineVertex];
            }

            _renderedLines[fieldlineVertex + lineStart] =
                lerp(currentPosition, nextPosition, normalizedTime);

            _debugTopologyColor[fieldlineVertex + lineStart] =
                glm::mix(debugColor(traverser.backKeyFrame->topology),
                    debugColor(traverser.frontKeyFrame->topology), normalizedTime);
        }
    }


    void RenderableMovingFieldlines::moveLines(const double currentTime, const double previousTime) {

        double dt = currentTime - previousTime;
        bool isValidTime = abs(dt) > DBL_EPSILON;
        if (!isValidTime) {
            return;
        }

        const std::vector<FieldlinesState::MatchingFieldlines>& allMatchingFieldlines =
            _fieldlineState.getAllMatchingFieldlines();
        // used to place fieldline at correct positions at birth/death
        double newTime = currentTime + dt;

        size_t lineIndex = 0;
        GLint lineStart1;
        GLint lineStart2;
        GLsizei nVertices1;
        GLsizei nVertices2;
        // line pairs

        for (size_t i = 0; i < allMatchingFieldlines.size(); ++i) {
            lineStart1 = _fieldlineState.lineStart()[lineIndex];
            lineStart2 = _fieldlineState.lineStart()[lineIndex + 1];
            nVertices1 = _fieldlineState.lineCount()[lineIndex];
            nVertices2 = _fieldlineState.lineCount()[lineIndex + 1];

            // Booleans to see if either of the traversers reached a topology change
            bool isNewTopology1 = _traversers[lineIndex].backKeyFrame->topology !=
                _traversers[lineIndex].frontKeyFrame->topology;
            bool isNewTopology2 = _traversers[lineIndex + 1].backKeyFrame->topology !=
                _traversers[lineIndex + 1].frontKeyFrame->topology;
            bool isNewTopology = isNewTopology1 || isNewTopology2;

            bool hasTemporaryKeyFrame1 = _traversers[lineIndex].hasTemporaryKeyFrame;
            bool hasTemporaryKeyFrame2 = _traversers[lineIndex + 1].hasTemporaryKeyFrame;
            bool hasTemporaryKeyFrame = hasTemporaryKeyFrame1 || hasTemporaryKeyFrame2;

            // if there is a topology change vertices should be swapped
            if (isNewTopology && !hasTemporaryKeyFrame) {

                bool isMovingForward = _traversers[lineIndex].forward;

                // The traverser that has not reached the reconnection point
                // should advance its next key frame and change the time to next
                // key frame accordingly
                if (isNewTopology1 && !hasTemporaryKeyFrame1) {
                    FieldlinesState::Fieldline::Topology desiredTopology = _traversers[lineIndex].decideTopology();
                    _traversers[lineIndex + 1].skipKeyFrame(desiredTopology);
                }
                else if (isNewTopology2 && !hasTemporaryKeyFrame2) {
                    FieldlinesState::Fieldline::Topology desiredTopology = _traversers[lineIndex + 1].decideTopology();
                    _traversers[lineIndex].skipKeyFrame(desiredTopology);
                }

                if (isMovingForward) {

                    std::vector<glm::vec3>::iterator renderIt = _renderedLines.begin();

                    updateTemporaryKeyFrame(
                        renderIt + lineStart1,
                        renderIt + (lineStart1 + _nPointsOnFieldlines / 2),
                        renderIt + (lineStart2 + _nPointsOnFieldlines / 2),
                        renderIt + (lineStart2 + _nPointsOnFieldlines),
                        _traversers[lineIndex].temporaryInterpolationKeyFrame
                    );

                    updateTemporaryKeyFrame(
                        renderIt + (lineStart2),
                        renderIt + (lineStart2 + _nPointsOnFieldlines / 2),
                        renderIt + (lineStart1 + _nPointsOnFieldlines / 2),
                        renderIt + (lineStart2),
                        _traversers[lineIndex + 1].temporaryInterpolationKeyFrame
                    );

                    _traversers[lineIndex].timeInterpolationDenominator -= _traversers[lineIndex].timeSinceInterpolation;
                    _traversers[lineIndex + 1].timeInterpolationDenominator -= _traversers[lineIndex + 1].timeSinceInterpolation;

                    _traversers[lineIndex].hasTemporaryKeyFrame = true;
                    _traversers[lineIndex + 1].hasTemporaryKeyFrame = true;
                }
            }

            // individual delta time for each traverser
            // since it might be modified depending on birth and death
            double dt1 = dt;

            // checks if it will with current dt move after birth
            // changes dt so that fieldlines stop at birth/death time           
            bool isAfterBirth1 = currentTime >= allMatchingFieldlines[i].pathLines.first.birthTime;
            bool movesBeforeBirth1 = newTime < allMatchingFieldlines[i].pathLines.first.birthTime;
            if (isAfterBirth1 && movesBeforeBirth1) {
                dt1 += allMatchingFieldlines[i].pathLines.first.birthTime - newTime;
            }

            // checks if it will with current dt move after death
            // changes dt so that fieldlines stop at birth/death time
            bool isBeforeDeath1 = currentTime <= allMatchingFieldlines[i].pathLines.first.deathTime;
            bool movesAfterDeath1 = newTime > allMatchingFieldlines[i].pathLines.first.deathTime;
            if (isBeforeDeath1 && movesAfterDeath1) {
                dt1 += allMatchingFieldlines[i].pathLines.first.deathTime - newTime;
            }

            if (isAfterBirth1 && isBeforeDeath1) {
                lineStart1 = _fieldlineState.lineStart()[lineIndex];
                nVertices1 = _fieldlineState.lineCount()[lineIndex];
                moveLine(dt1, allMatchingFieldlines[i].pathLines.first,
                    _traversers[lineIndex], lineStart1, nVertices1);
            }

            // individual delta time for each traverser
            // since it might be modified depending on birth and death
            double dt2 = dt;
            ++lineIndex;

            // checks if it will with current dt move after birth
            // changes dt so that fieldlines stop at birth/death time
            bool isAfterBirth2 = currentTime >= allMatchingFieldlines[i].pathLines.second.birthTime;
            bool movesBeforeBirth2 = newTime < allMatchingFieldlines[i].pathLines.second.birthTime;
            if (isAfterBirth2 && movesBeforeBirth2) {
                dt2 += allMatchingFieldlines[i].pathLines.second.birthTime - newTime;
            }

            // checks if it will with current dt move after death
            // changes dt so that fieldlines stop at birth/death time
            bool isBeforeDeath2 = currentTime <= allMatchingFieldlines[i].pathLines.second.deathTime;
            bool movesAfterDeath2 = newTime > allMatchingFieldlines[i].pathLines.second.deathTime;
            if (isBeforeDeath2 && movesAfterDeath2) {
                dt2 += allMatchingFieldlines[i].pathLines.second.deathTime - newTime;
            }

            if (isAfterBirth2 && isBeforeDeath2) {
                lineStart2 = _fieldlineState.lineStart()[lineIndex];
                nVertices2 = _fieldlineState.lineCount()[lineIndex];
                moveLine(dt2, allMatchingFieldlines[i].pathLines.second,
                    _traversers[lineIndex], lineStart2, nVertices2);
            }
            ++lineIndex;
        }
    }

    void RenderableMovingFieldlines::updateVertexPositionBuffer() {
        glBindVertexArray(_vertexArrayObject);
        glBindBuffer(GL_ARRAY_BUFFER, _vertexPositionBuffer);

        const std::vector<glm::vec3>& vertPos = _renderedLines;

        std::vector<glm::vec4> data;
        for (int i = 0; i < vertPos.size(); ++i) {
            //data.push_back({ vertPos[i], -1.f });
            data.push_back({ vertPos[i], -1.f });
            data[i].w = _debugTopologyColor[i];
        }

        if (_renderFlowLine) {

            // convert all vertices from Re to meter and add to data
            for (const FieldlinesState::MatchingFieldlines& mf : _fieldlineState.getAllMatchingFieldlines()) {
                std::for_each(mf.pathLines.first.line.begin(), mf.pathLines.first.line.end(),
                    [&data](const glm::vec3& element) {
                        data.push_back({ element * fls::ReToMeter, -1.f });
                    });

                std::for_each(mf.pathLines.second.line.begin(), mf.pathLines.second.line.end(),
                    [&data](const glm::vec3& element) {
                        data.push_back({ element * fls::ReToMeter, -1.f });
                    });
            }
        }

        glBufferData(
            GL_ARRAY_BUFFER,
            data.size() * sizeof(glm::vec4),
            data.data(),
            GL_STATIC_DRAW
        );

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    void RenderableMovingFieldlines::updateVertexColorBuffer() {
        glBindVertexArray(_vertexArrayObject);
        glBindBuffer(GL_ARRAY_BUFFER, _vertexColorBuffer);

        bool isSuccessful;
        const std::vector<float>& quantities = _fieldlineState.extraQuantity(
            _colorQuantity,
            isSuccessful
        );

        if (isSuccessful) {
            glBufferData(
                GL_ARRAY_BUFFER,
                quantities.size() * sizeof(float),
                quantities.data(),
                GL_STATIC_DRAW
            );

            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 0, 0);

            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);
        }
    }

    /**
    * Updates OpenGL-buffer with a float value for each field- and path line vertex.
    * The float value is multiplied with the vetex color alpha in the vertex shader.
    */
    void RenderableMovingFieldlines::updateVertexAlphaBuffer(const double currentTime) {

        constexpr const double fieldlineFadeTime = 5.0;    // seconds

        glBindVertexArray(_vertexArrayObject);
        glBindBuffer(GL_ARRAY_BUFFER, _vertexAlphaBuffer);

        std::vector<float> alpha = _renderedLinesAlpha;

        for (size_t i = 0; i < _fieldlineState.getAllMatchingFieldlines().size(); i++) {
            double birth = _fieldlineState.getAllMatchingFieldlines()[i].pathLines.first.birthTime;
            double death = _fieldlineState.getAllMatchingFieldlines()[i].pathLines.first.deathTime;

            double birthAlpha = std::min(
                std::max(currentTime - birth, 0.0) / fieldlineFadeTime,
                1.0);
            double deathAlpha = std::min(
                std::max(death - currentTime - fieldlineFadeTime, 0.0) / fieldlineFadeTime,
                1.0);
            double fadeAlpha = birthAlpha * deathAlpha;

            auto startIt = alpha.begin() + _fieldlineState.lineStart()[i * 2];
            auto endIt = alpha.begin() + _fieldlineState.lineStart()[i * 2 + 1];
            std::fill(startIt, endIt, static_cast<float>(fadeAlpha));

            birth = _fieldlineState.getAllMatchingFieldlines()[i].pathLines.second.birthTime;
            death = _fieldlineState.getAllMatchingFieldlines()[i].pathLines.second.deathTime;

            birthAlpha = std::min(
                std::max(currentTime - birth, 0.0) / fieldlineFadeTime,
                1.0);
            deathAlpha = std::min(
                std::max(death - currentTime - fieldlineFadeTime, 0.0) / fieldlineFadeTime,
                1.0);
            fadeAlpha = birthAlpha * deathAlpha;

            startIt = alpha.begin() + _fieldlineState.lineStart()[i * 2 + 1];
            endIt = alpha.begin() + _fieldlineState.lineStart()[i * 2 + 1] + _nPointsOnFieldlines;
            std::fill(startIt, endIt, static_cast<float>(fadeAlpha));
        }

        if (_renderFlowLine) {
            int nFlowVertices =
                _fieldlineState.getAllMatchingFieldlines().size() * 2 * _nPointsOnPathLine;
            alpha.insert(alpha.end(), nFlowVertices, 1.0f);
        }

        glBufferData(
            GL_ARRAY_BUFFER,
            alpha.size() * sizeof(float),
            alpha.data(),
            GL_STATIC_DRAW
        );

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 0, 0);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }


    /**
    * Constructor
    * initialize a traverser from a Fieldlinesstate fieldline to traverse the vertices
    */
    RenderableMovingFieldlines::PathLineTraverser::PathLineTraverser(std::vector<FieldlinesState::Fieldline>& fieldlines_) :
        keyFrames(fieldlines_),
        backKeyFrame(keyFrames.begin()),
        frontKeyFrame{ backKeyFrame + 1 } {}

    void RenderableMovingFieldlines::PathLineTraverser::advanceKeyFrames() {
        if (forward) {
            //timeSinceInterpolation -= backKeyFrame->timeToNextKeyFrame;
            timeSinceInterpolation -= timeInterpolationDenominator;
            timeInterpolationDenominator = frontKeyFrame->timeToNextKeyFrame;
            backKeyFrame = frontKeyFrame;
            ++frontKeyFrame;
        }
        else {
            frontKeyFrame = backKeyFrame;
            --backKeyFrame;
            //timeSinceInterpolation += backKeyFrame->timeToNextKeyFrame;
            timeSinceInterpolation += timeInterpolationDenominator;
            timeInterpolationDenominator = backKeyFrame->timeToNextKeyFrame;
        }
    }

    /**
    * Advances the frontKeyFrame (forward == true) or backKeyFrame (forward == false)
    * to create to correct interpolation when the topology for it's matching fieldline
    * has changed.
    */
    void RenderableMovingFieldlines::PathLineTraverser::skipKeyFrame(
        FieldlinesState::Fieldline::Topology desiredTopology) {
        //timeInterpolationNominator = 0.0;
        if (forward) {
            while ( frontKeyFrame->topology != desiredTopology) {
                ++frontKeyFrame;
                if (frontKeyFrame == this->keyFrames.end()) break;
                timeInterpolationDenominator += (frontKeyFrame - 1)->timeToNextKeyFrame;
            }
        }
        else {
            while ( backKeyFrame->topology != desiredTopology) {
                --backKeyFrame;
                if (backKeyFrame == this->keyFrames.begin()) break;
                timeInterpolationDenominator += backKeyFrame->timeToNextKeyFrame;
            }
        }
    }

    FieldlinesState::Fieldline::Topology 
        RenderableMovingFieldlines::PathLineTraverser::decideTopology() {

            FieldlinesState::Fieldline::Topology newTopology = this->forward ?
            frontKeyFrame->topology : backKeyFrame->topology;

            switch (newTopology) {
            case FieldlinesState::Fieldline::Topology::Imf:
                return FieldlinesState::Fieldline::Topology::Closed;
            case FieldlinesState::Fieldline::Topology::Closed:
                return FieldlinesState::Fieldline::Topology::Imf;
            case FieldlinesState::Fieldline::Topology::Open:
                return FieldlinesState::Fieldline::Topology::Open;
            }
    }

    // Will be phased out in the future
    // when birth and death times are correct
    bool RenderableMovingFieldlines::PathLineTraverser::isAtEnd() const {
        return forward ?
            (backKeyFrame == keyFrames.end() - 2) :
            (isAtStart());
    }

    bool RenderableMovingFieldlines::PathLineTraverser::isAtStart() const {

        if (backKeyFrame == keyFrames.begin())
            return true;

        return false;
    }

    /**
    * Updates vertex position in the traverser's temporary key frame.
    * The temporary key frame consists of two halfs of different rendered lines.
    * 
    * firstLineBeginIt and firstLineEndIt spans half a rendered fieldline.
    * secondLineBeginIt and secondLineEndIt spans half of another rendered fieldline
    * with a different topology than the first.
    * temporaryInterpolationKeyFrame is a reference to the traverser's variable and 
    * holds the resulting temporary key frame.
    */
    void RenderableMovingFieldlines::updateTemporaryKeyFrame(
        std::vector<glm::vec3>::iterator firstLineBeginIt, 
        std::vector<glm::vec3>::iterator firstLineEndIt,
        std::vector<glm::vec3>::iterator secondLineBeginIt,
        std::vector<glm::vec3>::iterator secondLineEndIt,
        FieldlinesState::Fieldline& temporaryInterpolationKeyFrame)
    {
        // calculate mean distance between vertices
        float firstLength = calculateFieldlineLength(firstLineBeginIt, firstLineEndIt);
        float secondLength = calculateFieldlineLength(secondLineBeginIt, secondLineEndIt);
        float topologyChangeGapLength = glm::distance(*(firstLineEndIt-1), *secondLineBeginIt);
        float totalLineLength = firstLength + secondLength + topologyChangeGapLength;

        float desiredVertexSpacing = totalLineLength / _nPointsOnFieldlines;

        // key frames starts and end at set positions
        temporaryInterpolationKeyFrame.vertices[0] = *firstLineBeginIt;
        temporaryInterpolationKeyFrame.vertices[_nPointsOnFieldlines - 1] = *(secondLineEndIt - 1);
        
        std::vector<glm::vec3>::iterator it = (firstLineBeginIt + 1);
        size_t temporaryKeyFrameIndex = 1;

        float traversedDistanceTemporaryKeyFrame = 0.0f;
        float traversedDistanceLine = 0.0f;

        // interpolate to find new vertex positions
        // sets one new temporary key frame position per iteration
        while (it != secondLineEndIt && temporaryKeyFrameIndex < _nPointsOnFieldlines - 1) {

            std::vector<glm::vec3>::iterator backIt = it == secondLineBeginIt ?
                backIt = (firstLineEndIt - 1) : (it - 1);   // needed for the gap

            float distanceSinceLastLineVertex = 
                traversedDistanceTemporaryKeyFrame + desiredVertexSpacing - traversedDistanceLine;
            float lineVertexDistance = glm::distance(*backIt, *it);
            float interpolationParameter = distanceSinceLastLineVertex / lineVertexDistance;

            traversedDistanceTemporaryKeyFrame += desiredVertexSpacing;

            // moves the line iterator
            while (interpolationParameter > 1.f) {
                traversedDistanceLine += lineVertexDistance;
                distanceSinceLastLineVertex -= lineVertexDistance;
                ++it;
                backIt = (it - 1);

                if (it == secondLineEndIt) {
                    goto exit;
                }

                // traverses through the gap
                if (it == firstLineEndIt) {
                    it = secondLineBeginIt;
                }

                lineVertexDistance = glm::distance(*backIt, *it);
                interpolationParameter = distanceSinceLastLineVertex / lineVertexDistance;
            }

            // sets temporary key frame vertex position
            temporaryInterpolationKeyFrame.vertices[temporaryKeyFrameIndex] = 
                lerp(*backIt, *it, interpolationParameter);

            ++temporaryKeyFrameIndex;
        }
    exit:;
    }

    float RenderableMovingFieldlines::calculateFieldlineLength(
        std::vector<glm::vec3>::iterator beginIt,
        std::vector<glm::vec3>::iterator endIt) 
    {
        std::vector<glm::vec3>::iterator it = beginIt + 1;
        float length = 0.0f;
        while (it != endIt) {
            length += glm::distance(*(it - 1), *it);
            ++it;
        }
        return length;
    }

    double RenderableMovingFieldlines::PathLineTraverser::getTimeToReconnectionPoint(size_t indexOfReconnection) {
        double totalTime = 0.0;
        // iterates over vertices on pathline to indexOfReconnection 
        // and sums up the time to get there
        for (size_t i = 0; i < indexOfReconnection; ++i) {
            totalTime += keyFrames[i].timeToNextKeyFrame;
        }
        
        return totalTime;
    }

    /**
    * sets the start point of a traverser, called from initialize
    */
    void RenderableMovingFieldlines::PathLineTraverser::setStartPoint(double otherTraverserTimeToReconnection, size_t indexOfReconnection) {
        double currentTraverserTimeSinceRecon = 0;
        for (size_t i = 0; i < indexOfReconnection; ++i) {
            size_t ind = indexOfReconnection - i - 1;
            currentTraverserTimeSinceRecon += keyFrames[ind].timeToNextKeyFrame;

            if (currentTraverserTimeSinceRecon >= otherTraverserTimeToReconnection) {
                // To get the proper position between keyframes
                timeSinceInterpolation = currentTraverserTimeSinceRecon - otherTraverserTimeToReconnection;
                backKeyFrame = keyFrames.begin() + ind - 1; // -1 because we go backwards?
                frontKeyFrame = backKeyFrame + 1;

                break;
            }
        }
    }

    /**
    * iterates through all keyframes and sums up time
    */
    double RenderableMovingFieldlines::PathLineTraverser::getTimeToEndKeyFrame() {
        double totalTime = 0;
        for (auto it = backKeyFrame; it != keyFrames.end() - 1; ++it) {
            totalTime += it->timeToNextKeyFrame;
        }

        return totalTime;
    }
} // namespace openspace
