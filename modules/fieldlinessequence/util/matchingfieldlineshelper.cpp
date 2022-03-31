#ifndef OPENSPACE_MODULE_KAMELEON_ENABLED
#error "CDF inputs provided but Kameleon module is deactivated"
#endif // OPENSPACE_MODULE_KAMELEON_ENABLED

#include <modules/fieldlinessequence/util/matchingfieldlineshelper.h>

#include <modules/fieldlinessequence/util/movingfieldlinehelper.cpp>
#include <modules/fieldlinessequence/util/commons.h>
#include <modules/fieldlinessequence/util/fieldlinesstate.h>


namespace openspace::fls {

    // ALIASES

    using seedPointPair = std::pair<glm::vec3, glm::vec3>;

    struct SplitPathlines{
        FieldlinesState::MatchingFieldlines imfAndClosed;
        FieldlinesState::MatchingFieldlines open;
    };

    // DECLARATIONS

    ccmc::Fieldline traceAndCreateMappedPathLine(const std::string& tracingVar,
        ccmc::Tracer& tracer,
        const glm::vec3& seedPoint,
        const size_t nPointsOnPathLine,
        ccmc::Tracer::Direction direction = ccmc::Tracer::Direction::FOWARD);

    std::vector<glm::vec3> concatenatePathLines(
        const std::vector<ccmc::Point3f>& firstPart,
        const std::vector<ccmc::Point3f>& secondPart);

    bool traceAndAddMatchingLinesToState(ccmc::Kameleon* kameleon,
        const std::vector<seedPointPair>& matchingSeedPoints,
        const std::vector<double>& birthTimes,
        const std::string& tracingVar,
        FieldlinesState& state, 
        const size_t nPointsOnPathLine,
        const size_t nPointsOnFieldlines
    );

    void traceAndCreateKeyFrame(std::vector<glm::vec3>& keyFrame,
        const glm::vec3& seedPoint,
        ccmc::Kameleon* kameleon,
        float innerbounds,
        size_t nPointsOnFieldlines
    );

    // DEFINITIONS

    bool openspace::fls::convertCdfToMatchingFieldlinesState(
        FieldlinesState& state,
        ccmc::Kameleon* kameleon,
        const std::vector<glm::vec3>& seedPoints,
        const std::vector<double>& birthTimes,
        double manualTimeOffset,
        const std::string& tracingVar,
        std::vector<std::string>& extraVars,
        std::vector<std::string>& extraMagVars,
        const size_t nPointsOnPathLine,
        const size_t nPointsOnFieldLines) 
    {

        // TODO: Check if an even amount of seed points
        std::vector<seedPointPair> matchingSeedPoints;
        for (size_t i = 0; i < seedPoints.size(); i += 2) {
            matchingSeedPoints.push_back({ seedPoints[i], seedPoints[i + 1] });
        }

        bool isSuccessful = openspace::fls::traceAndAddMatchingLinesToState(
            kameleon,
            matchingSeedPoints, 
            birthTimes,
            tracingVar, 
            state,
            nPointsOnPathLine, 
            nPointsOnFieldLines
        );

        return isSuccessful;
    }

    /**
    * Uses the tracer to trace and create a ccmc::Fieldline and returns.
    * Default direction is forward tracing
    */ 
    ccmc::Fieldline traceAndCreateMappedPathLine(const std::string& tracingVar,
        ccmc::Tracer &tracer, 
        const glm::vec3& seedPoint,
        const size_t nPointsOnPathLine,
        ccmc::Tracer::Direction direction) {
        
        ccmc::Fieldline uPerpBPathLine;
        uPerpBPathLine = tracer.unidirectionalTrace(
            tracingVar,
            seedPoint.x,
            seedPoint.y,
            seedPoint.z,
            direction
        );

        if (direction == ccmc::Tracer::Direction::REVERSE) {
            uPerpBPathLine = uPerpBPathLine.reverseOrder();
        }

        uPerpBPathLine.getDs();
        uPerpBPathLine.measure();
        uPerpBPathLine.integrate();

        ccmc::Fieldline mappedPath = uPerpBPathLine.interpolate(1, nPointsOnPathLine);

        return mappedPath;
    }
    
    /**
    * Concatenates the two vectors of pathline vertices into a new vector.
    * Converts from point3f to glm::vec3.
    */
    std::vector<glm::vec3> concatenatePathLines(
        const std::vector<ccmc::Point3f>& firstPart,
        const std::vector<ccmc::Point3f>& secondPart) {

        std::vector<glm::vec3> concatenated;
        for (const ccmc::Point3f& p : firstPart) {
            concatenated.emplace_back(p.component1, p.component2, p.component3);
        }
        for (const ccmc::Point3f& p : secondPart) {
            concatenated.emplace_back(p.component1, p.component2, p.component3);
        }

        return concatenated;
    }

    bool traceAndAddMatchingLinesToState(ccmc::Kameleon* kameleon,
        const std::vector<seedPointPair>& matchingSeedPoints,
        const std::vector<double>& birthTimes,
        const std::string& tracingVar,
        FieldlinesState& state,
        const size_t nPointsOnPathLine,
        const size_t nPointsOnFieldlines) {

        float innerBoundaryLimit;   // Defines the endpoint distance from Earth's center
        switch (state.model()) {
        case fls::Model::Batsrus:
            innerBoundaryLimit = 0.5f;  // TODO specify in Lua?
            break;
        default:
            LERROR(
                "OpenSpace's moving fieldlines currently only supports CDFs "
                "from the BATSRUS model"
            );
            return false;
        }

        // For each seedpoint, one line gets created, tracked with u perpendicular b.
        // then for each, and at each, vertex on that pathline, fieldlines are tracked
        if (tracingVar != "u_perp_b") {
            return false;
        }
        if (!kameleon->loadVariable("b")) {
            LERROR("Failed to load tracing variable: b");
            return false;
        }
        if (!kameleon->loadVariable("u")) {
            LERROR("Failed to load tracing variable: u");
            return false;
        }


        for (size_t i = 0; i < matchingSeedPoints.size() / 2; i++) {
            std::unique_ptr<ccmc::Interpolator> interpolator =
                std::make_unique<ccmc::KameleonInterpolator>(kameleon->model);
            ccmc::Tracer tracer(kameleon, interpolator.get());
            tracer.setInnerBoundary(innerBoundaryLimit);

            // Create pathlines (IMF and CF) for matching fieldlines
            // 11 is first part of first path line, 12 is the second part.
            // same for the second path line with 21 and 22
            // 
            size_t firstSeedId = i * 2;
            size_t secondSeedId = i * 2 + 1;
            ccmc::Fieldline mappedPath11 = traceAndCreateMappedPathLine(tracingVar, tracer,
                matchingSeedPoints[firstSeedId].first, nPointsOnPathLine, ccmc::Tracer::Direction::REVERSE);
            ccmc::Fieldline mappedPath12 = traceAndCreateMappedPathLine(tracingVar, tracer,
                matchingSeedPoints[secondSeedId].first, nPointsOnPathLine);

            ccmc::Fieldline mappedPath21 = traceAndCreateMappedPathLine(tracingVar, tracer,
                matchingSeedPoints[firstSeedId].second, nPointsOnPathLine, ccmc::Tracer::Direction::REVERSE);
            ccmc::Fieldline mappedPath22 = traceAndCreateMappedPathLine(tracingVar, tracer,
                matchingSeedPoints[secondSeedId].second, nPointsOnPathLine);


            // Get the vertex positions from the mapped pathlines
            std::vector<ccmc::Point3f> pathPositions11 = mappedPath11.getPositions();
            std::vector<ccmc::Point3f> pathPositions12 = mappedPath12.getPositions();

            std::vector<ccmc::Point3f> pathPositions21 = mappedPath21.getPositions();
            std::vector<ccmc::Point3f> pathPositions22 = mappedPath22.getPositions();

            // compute time
            size_t lengthToConcatenation1 = pathPositions11.size();
            size_t lengthToConcatenation2 = pathPositions21.size();
            
            // Here we concatenate the pathline pairs 11 + 12 and 21 + 22
            std::vector<glm::vec3> pathLine1 = concatenatePathLines(pathPositions11, pathPositions12);
            std::vector<glm::vec3> pathLine2 = concatenatePathLines(pathPositions21, pathPositions22);
            
            std::vector<glm::vec3>::const_iterator concatenationPointPathLine1 =
                pathLine1.begin() + lengthToConcatenation1;

            std::vector<glm::vec3>::const_iterator concatenationPointPathLine2 =
                pathLine2.begin() + lengthToConcatenation2;

            /*std::vector<glm::vec3> pathLine1;
            for (const ccmc::Point3f& p : pathPositions11) {
                pathLine1.emplace_back(p.component1, p.component2, p.component3);
            }*/

            //std::vector<glm::vec3> pathLine2;
            //for (const ccmc::Point3f& p : pathPositions21) {
            //    pathLine2.emplace_back(p.component1, p.component2, p.component3);
            //}

            // Elon: optimizing trimming could go here
            // std::vector<float> velocities = computeVelocities(pathLine, kameleon);
            // std::vector<float> times = computeTimes(pathLine, velocities);
            // seed? - trimPathFindLastVertex(pathLine, times, velocities, cdfLength);

            double birthTime = birthTimes[i];
            double deathTime1 = birthTime; // accumulates inside the loop
            double deathTime2 = birthTime;

            // Here all points on the pathLine will be used at seedpoints for 
            // the actual fieldlines (traced with "b" by default)
            state.addMatchingPathLines(std::move(pathLine1), lengthToConcatenation1,
                std::move(pathLine2), lengthToConcatenation2, birthTime);
            //state.addMatchingPathLines(std::move(pathLine1), concatenationPointPathLine1,
            //    std::move(pathLine2), concatenationPointPathLine2);

            for (size_t j = 0; j < pathLine1.size(); ++j) {

                std::vector<glm::vec3> keyFrame1;
                traceAndCreateKeyFrame(keyFrame1, pathLine1[j], kameleon, innerBoundaryLimit, nPointsOnFieldlines);
                std::vector<glm::vec3> keyFrame2;
                traceAndCreateKeyFrame(keyFrame2, pathLine2[j], kameleon, innerBoundaryLimit, nPointsOnFieldlines);

                // timeToNextKeyFrame is -1 if at last path line vertex
                // Vi vill tracea tid baklänges för den pathline del som räknats ut baklänges
                // Vi tror vi kan byta plats på this och next vertex
                float timeToNextKeyFrame1;
                float timeToNextKeyFrame2;
                if (j <= pathLine1.size()) {
                    timeToNextKeyFrame1 = j + 1 == pathLine1.size() ?
                        -1.f : openspace::fls::computeTime(pathLine1[j + 1], pathLine1[j], kameleon);
                }
                else {
                    timeToNextKeyFrame1 = j + 1 == pathLine1.size() ?
                        -1.f : openspace::fls::computeTime(pathLine1[j], pathLine1[j + 1], kameleon);
                }

                if (j <= pathLine2.size()) {
                    timeToNextKeyFrame2 = j + 1 == pathLine2.size() ?
                        -1.f : openspace::fls::computeTime(pathLine2[j + 1], pathLine2[j], kameleon);
                }
                else {
                    timeToNextKeyFrame2 = j + 1 == pathLine2.size() ?
                        -1.f : openspace::fls::computeTime(pathLine2[j], pathLine2[j + 1], kameleon);
                }

                state.addMatchingKeyFrames(std::move(keyFrame1), std::move(keyFrame2), 
                    timeToNextKeyFrame1, timeToNextKeyFrame2, i);

                deathTime1 += timeToNextKeyFrame1;
                deathTime2 += timeToNextKeyFrame2;
            }

            state.setDeathTimes(-26000 + i*50, -26000 + i*50 , i);
        }
        bool isSuccessful = state.getAllMatchingFieldlines().size() > 0;
        return isSuccessful;
    }

    /// <summary>
    /// Will trace and create a keyframe
    /// </summary>
    /// <param name="keyFrame">The keyframe that will be constructed</param>
    /// <param name="seedPoint">From where the trace starts</param>
    /// <param name="kameleon"></param>
    /// <param name="innerBoundaryLimit"></param>
    /// <param name="nPointsOnFieldlines">how many points to be created in the trace</param>
    void traceAndCreateKeyFrame(std::vector<glm::vec3>& keyFrame,
        const glm::vec3& seedPoint,
        ccmc::Kameleon* kameleon,
        float innerBoundaryLimit,
        size_t nPointsOnFieldlines) {
        std::unique_ptr<ccmc::Interpolator> newInterpolator =
            std::make_unique<ccmc::KameleonInterpolator>(kameleon->model);

        ccmc::Tracer tracer(kameleon, newInterpolator.get());
        tracer.setInnerBoundary(innerBoundaryLimit);

        //Elon: replace "secondary trace var"
        std::string tracingVar = "b";
        ccmc::Fieldline fieldline = tracer.bidirectionalTrace(
            tracingVar,
            seedPoint.x,
            seedPoint.y,
            seedPoint.z
        );

        fieldline.getDs();
        fieldline.measure();
        fieldline.integrate();

        ccmc::Fieldline mappedFieldline =
            fieldline.interpolate(1, nPointsOnFieldlines);
        const std::vector<ccmc::Point3f>& fieldlinePositions =
            mappedFieldline.getPositions();
        for (const ccmc::Point3f& pt : fieldlinePositions) {
            keyFrame.emplace_back(pt.component1, pt.component2, pt.component3);
        }
    }
} // openspace::fls
