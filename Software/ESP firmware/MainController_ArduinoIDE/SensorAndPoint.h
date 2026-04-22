#pragma once
#include <Arduino.h>

// =========================
// Basic Math Types
// =========================
struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Mat3
{
    // Row-major 3x3
    float m[3][3] = {0};
};

// =========================
// Math Helper Functions
// =========================
static Vec3 addVec3(const Vec3 &a, const Vec3 &b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

static Vec3 multiplyVec3ByScalar(const Vec3 &v, float s)
{
    return {v.x * s, v.y * s, v.z * s};
}

static Vec3 multiplyMat3ByVec3(const Mat3 &R, const Vec3 &v)
{
    return {
        R.m[0][0] * v.x + R.m[0][1] * v.y + R.m[0][2] * v.z,
        R.m[1][0] * v.x + R.m[1][1] * v.y + R.m[1][2] * v.z,
        R.m[2][0] * v.x + R.m[2][1] * v.y + R.m[2][2] * v.z};
}

static Mat3 multiplyMat3(const Mat3 &A, const Mat3 &B)
{
    Mat3 C{};
    for (int r = 0; r < 3; r++)
    {
        for (int c = 0; c < 3; c++)
        {
            float sum = 0.0f;
            for (int k = 0; k < 3; k++)
                sum += A.m[r][k] * B.m[k][c];
            C.m[r][c] = sum;
        }
    }
    return C;
}

static float degreesToRadians(float degrees)
{
    return degrees * (3.1415926f / 180.0f);
}

// =========================
// Rotation Matrices
// =========================
static Mat3 rotationX(float alphaRad)
{
    float c = cosf(alphaRad);
    float s = sinf(alphaRad);

    Mat3 R{};
    R.m[0][0] = 1.0f;
    R.m[1][1] = c;
    R.m[1][2] = -s;
    R.m[2][1] = s;
    R.m[2][2] = c;
    return R;
}

static Mat3 rotationY(float betaRad)
{
    float c = cosf(betaRad);
    float s = sinf(betaRad);

    Mat3 R{};
    R.m[0][0] = c;
    R.m[0][2] = s;
    R.m[1][1] = 1.0f;
    R.m[2][0] = -s;
    R.m[2][2] = c;
    return R;
}

static Mat3 rotationZ(float gammaRad)
{
    float c = cosf(gammaRad);
    float s = sinf(gammaRad);

    Mat3 R{};
    R.m[0][0] = c;
    R.m[0][1] = -s;
    R.m[1][0] = s;
    R.m[1][1] = c;
    R.m[2][2] = 1.0f;
    return R;
}

// Convention used:
// R = Rz(gamma) * Ry(beta) * Rx(alpha)
static Mat3 makeSensorRotation(float alphaRad, float betaRad, float gammaRad)
{
    return multiplyMat3(rotationZ(gammaRad),
                        multiplyMat3(rotationY(betaRad), rotationX(alphaRad)));
}

// =========================
// Point Object
// =========================
struct Point
{
    // World coordinates (units match your offsets & distances; if you use mm, these are mm)
    float worldX = 0.0f;
    float worldY = 0.0f;
    float worldZ = 0.0f;

    int sensorId = -1;
    int cellId = -1;

    // Raw measurement from pod
    uint16_t measuredCellDistanceMm = 0;
    uint8_t targetStatus = 0;
    uint8_t numTargets = 0;

    bool isValid = false;
};

// =========================
// Sensor Object
// =========================
class Sensor
{
public:
    static const int kNumCells = 16;

    int sensorId = -1;

    // Pose (sensor -> world)
    Mat3 sensorRotationMatrix{};
    Vec3 sensorTranslationVector{}; // Use mm if distances are in mm

    // Precomputed unit rays in SENSOR frame
    Vec3 cellDirectionVector[kNumCells]{};

    // Latest incoming per-cell data from pod
    uint16_t cellDistanceMm[kNumCells]{};
    uint8_t targetStatus[kNumCells]{};
    uint8_t numTargets[kNumCells]{};

    Sensor() = default;

    void configureSensor(int newSensorId,
                         float xOffset, float yOffset, float zOffset,
                         float alphaRad, float betaRad, float gammaRad)
    {
        sensorId = newSensorId;

        sensorTranslationVector = {xOffset, yOffset, zOffset};
        sensorRotationMatrix = makeSensorRotation(alphaRad, betaRad, gammaRad);

        computeCellDirectionVectors_();

        for (int i = 0; i < kNumCells; i++)
        {
            cellDistanceMm[i] = 0;
            targetStatus[i] = 0;
            numTargets[i] = 0;
        }
    }

    void setAllCellData(const uint16_t newDistancesMm[kNumCells],
                        const uint8_t newTargetStatus[kNumCells],
                        const uint8_t newNumTargets[kNumCells])
    {
        for (int i = 0; i < kNumCells; i++)
        {
            cellDistanceMm[i] = newDistancesMm[i];
            targetStatus[i] = newTargetStatus[i];
            numTargets[i] = newNumTargets[i];
        }
    }

    Point createWorldPointFromCell(int cellId, uint16_t minimumValidDistanceMm = 0) const
    {
        Point point{};
        point.sensorId = sensorId;
        point.cellId = cellId;

        if (cellId < 0 || cellId >= kNumCells)
            return point;

        point.measuredCellDistanceMm = cellDistanceMm[cellId];
        point.targetStatus = targetStatus[cellId];
        point.numTargets = numTargets[cellId];

        if (point.measuredCellDistanceMm <= minimumValidDistanceMm)
            return point;

        // Convert distance to float (still mm) for math
        float distanceMm = (float)point.measuredCellDistanceMm;

        // pointInSensorFrame = distance * unitRay
        Vec3 pointInSensorFrame = multiplyVec3ByScalar(cellDirectionVector[cellId], distanceMm);

        // pointInWorldFrame = R * pointInSensorFrame + t
        Vec3 pointInWorldFrame =
            addVec3(multiplyMat3ByVec3(sensorRotationMatrix, pointInSensorFrame),
                    sensorTranslationVector);

        point.worldX = pointInWorldFrame.x;
        point.worldY = pointInWorldFrame.y;
        point.worldZ = pointInWorldFrame.z;

        point.isValid = true;
        return point;
    }

private:
    void computeCellDirectionVectors_()
    {
        // 60x60 deg FOV, 4x4 cells, use center-of-cell angles
        const float fullFovDeg = 60.0f;
        const float halfFovDeg = fullFovDeg * 0.5f; // 30 deg
        const float stepDeg = fullFovDeg / 4.0f;    // 15 deg

        for (int cell = 0; cell < kNumCells; cell++)
        {
            int row = cell / 4; // 0 bottom -> 3 top
            int col = cell % 4; // 0 left -> 3 right

            // ✅ VL53L7CX effective orientation flips the image horizontally and vertically
            row = 3 - row; // flip vertical
            col = 3 - col; // flip horizontal

            float thetaDeg = -halfFovDeg + (col + 0.5f) * stepDeg; // left(-) to right(+)
            float phiDeg   = -halfFovDeg + (row + 0.5f) * stepDeg; // down(-) to up(+)

            float thetaRad = degreesToRadians(thetaDeg);
            float phiRad   = degreesToRadians(phiDeg);

            // Unit direction in SENSOR frame (+x forward, +y right, +z up)
            Vec3 dir{};
            dir.x = cosf(phiRad) * cosf(thetaRad);
            dir.y = cosf(phiRad) * sinf(thetaRad);
            dir.z = sinf(phiRad);

            cellDirectionVector[cell] = dir;
        }
    }
};
